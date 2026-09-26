"""Market statistics from LOBSTER-format files, real or simulated.

    python3 analysis/lob_stats.py NAME=MESSAGE_FILE [NAME=MESSAGE_FILE ...]

The order book file is found by replacing "message" with "orderbook" in the
path. Prints the statistics side by side and saves them to results/lob_stats.json.

Everything is measured in ticks (one tick = 100 LOBSTER price units = $0.01),
so real and simulated markets are compared in the same units. Time-averaged
quantities are weighted by how long each book state lasted, not by message.
"""
import json
import sys

import numpy as np
import pandas as pd

TICK = 100
TRIM = 15 * 60     # drop the first and last 15 minutes of a trading day (open/close effects)


def load(msg_path):
    msg = pd.read_csv(msg_path, header=None, usecols=range(6),
                      names=["time", "type", "id", "size", "price", "dir"])
    ob = pd.read_csv(msg_path.replace("message", "orderbook"), header=None).to_numpy(dtype=float)
    keep = msg.type.isin([1, 2, 3, 4, 5]).to_numpy()          # drop halts and auction crosses
    msg, ob = msg[keep].reset_index(drop=True), ob[keep]
    t0, t1 = msg.time.iloc[0], msg.time.iloc[-1]
    if t1 - t0 > 4 * 3600:                                     # a full trading day: trim the ends
        w = ((msg.time >= t0 + TRIM) & (msg.time <= t1 - TRIM)).to_numpy()
        msg, ob = msg[w].reset_index(drop=True), ob[w]
    return msg, ob


def book_arrays(ob):
    levels = ob.shape[1] // 4
    ask_p = ob[:, 0::4] / TICK; ask_s = ob[:, 1::4]
    bid_p = ob[:, 2::4] / TICK; bid_s = ob[:, 3::4]
    ask_s = np.where(ask_p > 9e7, np.nan, ask_s)             # dummy levels -> missing
    bid_s = np.where(bid_p < -9e7, np.nan, bid_s)
    return levels, ask_p, ask_s, bid_p, bid_s


def mid_at(times, mids, t):
    """Mid in force at each time in t (last book state at or before it)."""
    i = np.searchsorted(times, t, side="right") - 1
    return mids[np.clip(i, 0, len(mids) - 1)]


def stats(msg, ob):
    s = {}
    times = msg.time.to_numpy()
    dur = times[-1] - times[0]
    s["hours analysed"] = dur / 3600
    levels, ask_p, ask_s, bid_p, bid_s = book_arrays(ob)
    valid = (ask_p[:, 0] < 9e7) & (bid_p[:, 0] > -9e7)
    w = np.append(np.diff(times), 0.0)                        # time each book state lasted
    mids = np.where(valid, (ask_p[:, 0] + bid_p[:, 0]) / 2, np.nan)
    mids = pd.Series(mids).ffill().bfill().to_numpy()

    # --- order flow rates --------------------------------------------------
    typ = msg.type.to_numpy()
    s["new orders / s"] = (typ == 1).sum() / dur
    s["cancels / s"] = np.isin(typ, [2, 3]).sum() / dur
    # executions at the same instant on the same side come from one aggressive order
    ex = msg[np.isin(typ, [4, 5])]
    trades = ex.groupby(["time", "dir"], sort=True).agg(size=("size", "sum")).reset_index()
    s["trades / s"] = len(trades) / dur
    s["cancels per new order"] = np.isin(typ, [2, 3]).sum() / max(1, (typ == 1).sum())

    # --- sizes --------------------------------------------------------------
    new = msg[typ == 1]
    s["new order size, median"] = float(new["size"].median())
    s["new order size, mean"] = float(new["size"].mean())
    s["trade size, median"] = float(trades["size"].median())

    # --- spread and depth, time-weighted ----------------------------------
    spread = (ask_p[:, 0] - bid_p[:, 0])[valid]
    wv = w[valid]
    s["spread mean (ticks)"] = float(np.average(spread, weights=wv))
    for k in (1, 2, 3):
        s[f"spread = {k} tick{'s' if k > 1 else ''} (share of time)"] = float(wv[spread == k].sum() / wv.sum())
    touch = np.nan_to_num(ask_s[:, 0]) + np.nan_to_num(bid_s[:, 0])
    s["depth at touch, both sides"] = float(np.average(touch[valid], weights=wv))
    prof = []
    for l in range(levels):
        d = (np.nan_to_num(ask_s[:, l]) + np.nan_to_num(bid_s[:, l])) / 2
        prof.append(float(np.average(d[valid], weights=wv)))
    s["depth profile, levels 1-10 (per side)"] = [round(x, 1) for x in prof]
    s["depth level 5 / level 1"] = prof[4] / prof[0] if prof[0] else float("nan")

    # --- where new orders are placed, relative to the same-side best -------
    # uses the book BEFORE the message, i.e. the previous row
    idx = np.flatnonzero(typ == 1)
    idx = idx[idx > 0]
    prev = idx - 1
    px = msg.price.to_numpy()[idx] / TICK
    d = msg.dir.to_numpy()[idx]
    dist = np.where(d == 1, bid_p[prev, 0] - px, px - ask_p[prev, 0])
    ok = valid[prev]
    dist = dist[ok]
    s["placement: inside spread"] = float((dist < 0).mean())
    s["placement: at the touch"] = float((dist == 0).mean())
    s["placement: 1-2 ticks behind"] = float(((dist >= 1) & (dist <= 2)).mean())
    s["placement: 3+ ticks behind"] = float((dist >= 3).mean())

    # --- order lifetimes -----------------------------------------------------
    first = new.drop_duplicates("id").set_index("id").time
    dele = msg[typ == 3].drop_duplicates("id").set_index("id").time
    life = (dele - first.reindex(dele.index)).dropna()
    life = life[life >= 0]
    s["cancelled orders: median lifetime (s)"] = float(life.median())
    s["cancelled within 1 s"] = float((life < 1).mean())
    s["cancelled within 10 s"] = float((life < 10).mean())

    # --- 1-second mid returns ----------------------------------------------
    grid = np.arange(np.ceil(times[0]), np.floor(times[-1]))
    m = mid_at(times, mids, grid)
    r = np.diff(m)
    s["1s return sd (ticks)"] = float(r.std())
    s["1s returns exactly zero"] = float((r == 0).mean())
    rc = r - r.mean()
    s["1s return autocorr lag 1"] = float((rc[1:] * rc[:-1]).sum() / (rc * rc).sum())
    s["1s return kurtosis"] = float((rc ** 4).mean() / (rc ** 2).mean() ** 2)
    # Volatility from 60-second returns, scaled to per-sqrt-second. Long enough
    # that bid-ask flicker doesn't inflate it, so it estimates the underlying drift.
    g60 = np.arange(np.ceil(times[0]), np.floor(times[-1]), 60.0)
    r60 = np.diff(mid_at(times, mids, g60))
    s["volatility (ticks per sqrt s, from 60s returns)"] = float(r60.std() / np.sqrt(60.0))

    # --- price impact: how far the mid moves after a trade, signed ---------
    # direction -1 means a resting SELL was hit, i.e. a buyer-initiated trade
    sign = np.where(trades.dir.to_numpy() == -1, 1.0, -1.0)
    tt = trades.time.to_numpy()
    first_row = np.searchsorted(times, tt, side="left")
    before = mids[np.clip(first_row - 1, 0, len(mids) - 1)]
    for h in (0.0, 1.0, 5.0, 30.0, 60.0):
        ok = tt + h <= times[-1]
        after = mid_at(times, mids, tt[ok] + h + (1e-9 if h == 0 else 0))
        s[f"impact after {int(h)}s (ticks)"] = float(np.mean(sign[ok] * (after - before[ok])))
    return s


def main():
    runs = []
    for arg in sys.argv[1:]:
        name, path = arg.split("=", 1)
        runs.append((name, stats(*load(path))))
    names = [n for n, _ in runs]
    keys = list(runs[0][1].keys())
    width = max(len(k) for k in keys) + 2
    print(" " * width + "".join(f"{n:>16}" for n in names))
    for k in keys:
        cells = []
        for _, s in runs:
            v = s[k]
            if isinstance(v, list):
                cells.append(f"{' '.join(str(int(round(x))) for x in v[:5])} ...")
            else:
                cells.append(f"{v:16.3f}" if abs(v) < 1000 else f"{v:16.0f}")
        if isinstance(runs[0][1][k], list):
            print(f"{k:<{width}}" + "   |   ".join(cells))
        else:
            print(f"{k:<{width}}" + "".join(cells))
    with open("results/lob_stats.json", "w") as f:
        json.dump({n: s for n, s in runs}, f, indent=2)


if __name__ == "__main__":
    main()
