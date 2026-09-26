"""Calibrate the market simulator to a real stock, from its LOBSTER files.

    python3 analysis/calibrate.py MESSAGE_FILE [--out results/NAME.cfg]

Two kinds of parameter:

  Measured directly    arrival rates, order sizes, where orders are placed,
                       how long they live (by distance from the touch), and
                       volatility. Read straight off the data.

  Found by simulation  the informed trader's rate. It can't be observed, so the
                       simulator is run at a range of rates and the one whose
                       permanent price impact matches the real stock is kept
                       (the method of simulated moments).

Spread, depth, the share of zero returns and return autocorrelation are never
used. They're held back to test whether the calibrated model is realistic.

Some parameters can't be identified from this data and are fixed by
assumption; they're listed under ASSUMED in the config file.
"""
import argparse
import json
import os
import re
import subprocess
import sys

import numpy as np
import pandas as pd

sys.path.insert(0, os.path.dirname(__file__))
from lob_stats import TICK, book_arrays, load, stats   # noqa: E402

N_Q = 1001            # quantile resolution: fine enough that heavy tails don't inflate the mean
N_PLACE = 21          # placement buckets: inside spread, then 0..18 ticks behind, then 19+
SIM = "./sim"


def quantiles(x):
    x = np.asarray(x, dtype=float)
    return np.quantile(x, np.linspace(0, 1, N_Q)) if len(x) >= 20 else np.array([])


def measure(msg, ob):
    """Everything directly measurable, from one trading day."""
    times = msg.time.to_numpy()
    dur = times[-1] - times[0]
    typ = msg.type.to_numpy()
    _, ask_p, _, bid_p, _ = book_arrays(ob)
    valid = (ask_p[:, 0] < 9e7) & (bid_p[:, 0] > -9e7)

    new = msg[typ == 1]
    ex = msg[np.isin(typ, [4, 5])]
    trades = ex.groupby(["time", "dir"]).agg(size=("size", "sum")).reset_index()

    # placement: distance from the same-side best just BEFORE the order arrived
    idx = np.flatnonzero(typ == 1)
    idx = idx[idx > 0]
    prev = idx - 1
    px = msg.price.to_numpy()[idx] / TICK
    d = msg.dir.to_numpy()[idx]
    dist = np.where(d == 1, bid_p[prev, 0] - px, px - ask_p[prev, 0])
    ok = valid[prev]
    dist, oid = dist[ok], msg.id.to_numpy()[idx][ok]
    bucket = np.where(dist < 0, 0, np.minimum(dist, N_PLACE - 2) + 1).astype(int)
    place = np.bincount(bucket, minlength=N_PLACE).astype(float)
    place /= place.sum()

    # lifetimes of fully cancelled orders, grouped by where they were placed
    sub = pd.DataFrame({"id": oid, "bucket": bucket,
                        "t_sub": msg.time.to_numpy()[idx][ok]}).drop_duplicates("id")
    dele = msg[typ == 3].drop_duplicates("id")[["id", "time"]].rename(columns={"time": "t_del"})
    life = sub.merge(dele, on="id")
    life["life"] = life.t_del - life.t_sub
    life = life[life.life >= 0]
    # Orders that vanish within 100 ms are algorithmic quote updates, not
    # ordinary trading interest. Their share is what Phase 8 needs: it is the
    # amount of liquidity that reacts to order flow.
    groups = {"touch": life[life.bucket <= 1].life,
              "near": life[(life.bucket >= 2) & (life.bucket <= 3)].life,
              "deep": life[life.bucket >= 4].life}

    mids = np.where(valid, (ask_p[:, 0] + bid_p[:, 0]) / 2, np.nan)
    start = float(pd.Series(mids).dropna().iloc[0])
    s = stats(msg, ob)
    return {
        "hours": dur / 3600,
        "new_rate": (typ == 1).sum() / dur,
        "trade_rate": len(trades) / dur,
        "sizes": quantiles(new["size"]),
        "trade_sizes": quantiles(trades["size"]),
        "life_all": quantiles(life.life),
        "life": {k: quantiles(v) for k, v in groups.items()},
        "life_median": {k: float(v.median()) if len(v) else float("nan") for k, v in groups.items()},
        "life_n": {k: int(len(v)) for k, v in groups.items()},
        "fleeting": {k: float((v < 0.1).mean()) if len(v) else float("nan") for k, v in groups.items()},
        "placement": place,
        "vol": s["volatility (ticks per sqrt s, from 60s returns)"],
        "start": start,
        "median_size": float(new["size"].median()),
        "stats": s,
    }


def fmt(a):
    return " ".join(f"{x:.6g}" for x in a)


def write_config(path, p, informed_rate, aggressive_rate, vol, seed=1, reactive=False):
    maker_rate = 0.5
    maker_size = int(round(p["median_size"]))
    max_inv = 20 * maker_size
    lines = [
        "# Calibrated by analysis/calibrate.py",
        "# MEASURED from the data: rates, sizes, placement, lifetimes",
        "# TUNED by simulation: informed_rate (to match permanent price impact) and",
        "#   fundamental_vol (so simulated 60-second volatility matches the real one)",
        "# ASSUMED, not identifiable from this data: the market maker and the",
        "#   informed trader's threshold, listed at the end",
        f"seed {seed}",
        f"duration {p['hours'] * 3600:.0f}",
        f"fundamental_start {p['start']:.2f}",
        f"fundamental_vol {vol:.6g}",
        "noise_model calibrated",
        "noise_count 10",
        # the maker posts up to two new orders each time it acts
        f"passive_rate {max(0.0, p['new_rate'] - 2 * maker_rate):.6g}",
        f"aggressive_rate {aggressive_rate:.6g}",
        f"size_quantiles {fmt(p['sizes'])}",
        f"trade_size_quantiles {fmt(p['trade_sizes'])}",
        f"lifetime_quantiles {fmt(p['life_all'])}",
    ]
    for k in ("touch", "near", "deep"):
        if len(p["life"][k]):
            lines.append(f"lifetime_{k}_quantiles {fmt(p['life'][k])}")
    lines += [
        f"placement_probs {fmt(p['placement'])}",
        "informed_model calibrated",
        f"informed_rate {informed_rate:.6g}",
        "informed_cap 1000000",
        "# --- ASSUMED ---",
        "informed_threshold 1",
        "maker skewed",
        f"maker_rate {maker_rate}",
        "maker_half_spread 0.5",
        f"maker_size {maker_size}",
        f"maker_max_inventory {max_inv}",
        f"maker_skew {2.0 / max_inv:.6g}",
    ]
    if reactive:
        # Measured, not tuned: the share of orders at the touch cancelled within
        # 100 ms, and the median lifetime of orders there.
        lines += [
            "# --- reactive liquidity providers (measured from the data) ---",
            f"reactive_share {p['fleeting']['touch']:.4g}",
            f"reactive_quote_life {max(0.005, p['life_median']['touch']):.4g}",
            "reactive_count 5",
            "reactive_wake_rate 50",
        ]
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


REACTIVE = False
VOL_KEY = "volatility (ticks per sqrt s, from 60s returns)"
SEEDS = (1, 2, 3)


def simulate(cfg, prefix):
    out = subprocess.run([SIM, "run", cfg, prefix], capture_output=True, text=True, check=True).stdout
    tps = float(re.search(r"informed_trades_per_s ([\d.eE+-]+)", out).group(1))
    return tps, stats(*load(prefix + "_message_10.csv"))


def permanent(s):
    return 0.5 * (s["impact after 30s (ticks)"] + s["impact after 60s (ticks)"])


TMP_CFG, TMP_PREFIX = "/tmp/calib.cfg", "/tmp/CALIB_2012-06-21_34200000_57600000"


def run_at(p, rate, vol):
    """Run at one informed rate and volatility, averaged over several seeds.

    The TOTAL trade rate is held at the real value: whatever informed trading
    the rate produces is taken out of the noise traders' aggressive orders.
    One correction pass is enough, since informed trading barely depends on
    how much noise trading there is."""
    target = p["trade_rate"]
    write_config(TMP_CFG, p, rate, target, vol, seed=SEEDS[0], reactive=REACTIVE)
    tps0, _ = simulate(TMP_CFG, TMP_PREFIX)
    agg = max(0.0, target - tps0)
    imp, tps, vols = [], [], []
    for sd in SEEDS:
        write_config(TMP_CFG, p, rate, agg, vol, seed=sd, reactive=REACTIVE)
        t, s = simulate(TMP_CFG, TMP_PREFIX)
        imp.append(permanent(s)); tps.append(t); vols.append(s[VOL_KEY])
        last = s
    imp = np.array(imp)
    return {"rate": rate, "vol_in": vol, "aggressive_rate": agg,
            "informed_tps": float(np.mean(tps)), "impact": float(imp.mean()),
            "impact_se": float(imp.std(ddof=1) / np.sqrt(len(imp))),
            "sim_vol": float(np.mean(vols)), "feasible": float(np.mean(tps)) <= target,
            "stats": last}


def show(r, note=""):
    print(f"  {r['rate']:6.2f} {r['vol_in']:7.3f} {r['informed_tps']:10.3f} "
          f"{r['impact']:8.3f} +/- {r['impact_se']:.3f} {r['sim_vol']:9.3f}   "
          f"{'yes' if r['feasible'] else 'NO '}  {note}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("message_file")
    ap.add_argument("--out", default="results/calibrated.cfg")
    ap.add_argument("--grid", default="0,0.2,0.5,1,2,5,10")
    ap.add_argument("--reactive", action="store_true",
                    help="include reactive liquidity providers (Phase 8); their share and "
                         "quote lifetime are measured from the data, not tuned")
    a = ap.parse_args()
    global REACTIVE
    REACTIVE = a.reactive

    print("measuring", a.message_file)
    msg, ob = load(a.message_file)
    p = measure(msg, ob)
    target, target_vol = permanent(p["stats"]), p["vol"]
    print(f"  {p['hours']:.1f} h | {p['new_rate']:.2f} new orders/s | {p['trade_rate']:.3f} trades/s")
    print("  median lifetime of cancelled orders by placement:",
          ", ".join(f"{k} {p['life_median'][k]:.2f}s (n={p['life_n'][k]})" for k in ("touch", "near", "deep")))
    print("  share cancelled within 100 ms (algorithmic quote updates):",
          ", ".join(f"{k} {p['fleeting'][k]:.1%}" for k in ("touch", "near", "deep")))
    print(f"  targets: permanent impact {target:.3f} ticks, 60s volatility {target_vol:.3f} ticks/sqrt(s)")

    hdr = f"  {'rate':>6} {'vol in':>7} {'inf tr/s':>10} {'impact':>8}   {'':5} {'sim vol':>9}   fits"

    def tune_rate(vol, label):
        """Lowest informed rate whose permanent impact reaches the target.

        Impact rises with the rate and then flattens: once prices track fair
        value there is no extra mispricing to find. So take the FIRST crossing
        on the way up, not the closest point anywhere -- on the flat part,
        "closest" is decided by simulation noise."""
        print(f"\n{label}")
        print(hdr)
        runs = [run_at(p, float(x), vol) for x in a.grid.split(",")]
        for r in runs:
            show(r)
        rs = sorted(runs, key=lambda x: x["rate"])
        for lo, hi in zip(rs, rs[1:]):
            if lo["impact"] <= target <= hi["impact"]:
                r = lo["rate"] + (target - lo["impact"]) * (hi["rate"] - lo["rate"]) / (hi["impact"] - lo["impact"])
                out = run_at(p, r, vol)
                show(out, "<- first crossing, interpolated")
                return out, rs, True
        print("  NOT IDENTIFIED: impact never reaches the target at any rate on this grid,"
              " so the informed rate is not pinned down by the data. Keeping the closest run.")
        return min(runs, key=lambda x: abs(x["impact"] - target)), rs, False

    # The two unknowns interact: raising the fundamental's volatility also
    # raises price impact, so a step that helps one target can wreck the other.
    # Score a candidate by the WORSE of its two relative errors and only keep a
    # step that improves that, otherwise the two stages take turns undoing each
    # other and the search runs away.
    gap = lambda r: abs(r["sim_vol"] - target_vol)

    def score(r):
        return max(abs(r["impact"] - target) / max(target, 1e-9),
                   abs(r["sim_vol"] - target_vol) / max(target_vol, 1e-9))

    vol = target_vol
    chosen, rs, identified = tune_rate(vol, "1. informed rate, averaged over %d seeds each" % len(SEEDS))
    for sweep in range(2):
        print(f"\n{2 + sweep * 2}. volatility, at the chosen rate"
              " (a step is kept only if it improves BOTH targets' worst error)")
        print(hdr)
        moved = False
        for _ in range(3):
            trial_vol = vol * target_vol / chosen["sim_vol"]
            trial = run_at(p, chosen["rate"], trial_vol)
            if score(trial) < score(chosen) and trial["feasible"]:
                chosen, vol, moved = trial, trial_vol, True
                show(trial, "kept")
            else:
                show(trial, "rejected: worse on impact or volatility")
                break
        if not moved:
            break
        best_before = score(chosen)
        cand, rs_new, identified_new = tune_rate(
            vol, f"{3 + sweep * 2}. informed rate again, at the new volatility")
        if score(cand) < best_before:
            chosen, rs, identified = cand, rs_new, identified_new
        else:
            print("  keeping the previous rate: re-tuning did not improve the fit")
            break

    vol_ok = gap(chosen) <= 0.15 * target_vol
    if not vol_ok:
        print(f"  NOT MATCHED: simulated volatility {chosen['sim_vol']:.3f} vs real {target_vol:.3f}."
              " The simulated price cannot move as much as the real one does.")

    # Rates the data cannot rule out: impact within two standard errors of the target.
    ok = [r["rate"] for r in rs if abs(r["impact"] - target) <= 2 * max(r["impact_se"], 0.02)]
    if ok:
        hi_txt = "or more" if max(ok) == max(x["rate"] for x in rs) else f"to {max(ok):g}"
        print(f"\n  rates consistent with the real impact: {min(ok):g} {hi_txt}")

    write_config(a.out, p, chosen["rate"], chosen["aggressive_rate"], vol, reactive=REACTIVE)
    print(f"\nchosen: informed_rate {chosen['rate']:.3f}, fundamental_vol {vol:.3f} -> {a.out}")
    if not chosen["feasible"]:
        print("WARNING: matching the real impact needs more informed trades than the real market"
              " has trades in total. The model cannot match both.")

    trades_ok = abs(chosen["informed_tps"] + chosen["aggressive_rate"] - p["trade_rate"]) <= 0.15 * p["trade_rate"]
    impact_ok = abs(chosen["impact"] - target) <= 0.15 * target
    if not identified:
        print("\nWARNING: the informed rate is NOT identified by this data. Impact never"
              "\nreaches the real value at any rate tried, so the chosen rate is a floor,"
              "\nnot a measurement. This happens when the simulated price does not track"
              "\nfair value, which also shows up as the volatility mismatch below.")

    print("\nsummary of the tuned targets:")
    for name, ok in (("permanent impact", impact_ok), ("60s volatility", vol_ok), ("trade rate", trades_ok)):
        print(f"  {name:18s} {'matched' if ok else 'NOT MATCHED'}")

    report = {"target_impact": target, "target_vol": target_vol, "grid": [],
              "matched": {"impact": impact_ok, "volatility": vol_ok, "trade_rate": trades_ok},
              "informed_rate_identified": identified,
              "consistent_rates": ok,
              "chosen": {k: v for k, v in chosen.items() if k != "stats"},
              "real": p["stats"], "sim": chosen["stats"],
              "lifetime_median_by_placement": p["life_median"],
              "fleeting_share_by_placement": p["fleeting"]}
    with open(a.out.replace(".cfg", ".json"), "w") as f:
        json.dump(report, f, indent=2, default=float)


if __name__ == "__main__":
    main()
