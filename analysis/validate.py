"""Compare a real market with its calibrated simulation, on statistics that
were NOT used in calibration.

    python3 analysis/validate.py REAL_MESSAGE_FILE SIM_MESSAGE_FILE [--name MSFT]

Writes results/validation.png and results/validation.md.
"""
import argparse
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from lob_stats import load, stats   # noqa: E402

INK, REAL, SIM = "#15202B", "#15202B", "#1C6E75"

HELD_OUT = [
    ("spread mean (ticks)", "Mean spread (ticks)", "{:.3f}"),
    ("spread = 1 tick (share of time)", "Spread at 1 tick (share of time)", "{:.1%}"),
    ("depth at touch, both sides", "Depth at the touch", "{:,.0f}"),
    ("depth level 5 / level 1", "Depth, level 5 / level 1", "{:.2f}"),
    ("1s returns exactly zero", "1s returns exactly zero", "{:.1%}"),
    ("1s return sd (ticks)", "1s return sd (ticks)", "{:.3f}"),
    ("1s return autocorr lag 1", "1s return autocorrelation", "{:+.3f}"),
    ("1s return kurtosis", "1s return kurtosis", "{:.1f}"),
    ("impact after 1s (ticks)", "Price impact after 1s", "{:.3f}"),
    ("impact after 60s (ticks)", "Price impact after 60s", "{:.3f}"),
    # Not a calibration input: the calibrator is given separate lifetime tables
    # for touch, near and deep orders. The overall median depends on which
    # orders end up cancelled rather than filled, which the simulation decides.
    ("cancelled orders: median lifetime (s)", "Median cancelled-order lifetime (s)", "{:.2f}"),
]
CALIBRATED = [
    ("new orders / s", "New orders per second", "{:.2f}"),
    ("trades / s", "Trades per second", "{:.3f}"),
    ("new order size, median", "Median order size", "{:,.0f}"),
    ("placement: at the touch", "Orders placed at the touch", "{:.1%}"),
    ("volatility (ticks per sqrt s, from 60s returns)", "Volatility, 60s (ticks/sqrt s)", "{:.3f}"),
    # Exactly the quantity calibrate.py tunes to: the mean of the 30s and 60s impacts.
    ("permanent impact", "Permanent impact (mean of 30s and 60s)", "{:.3f}"),
]


def table(real, sim, rows, check=False):
    # For calibrated quantities, agreement is supposed to be guaranteed, so a
    # mismatch means the calibration itself failed. Say so rather than letting
    # the heading "by construction" imply a match that didn't happen.
    head = "| | Real | Simulated |" + (" |" if check else "")
    out = [head, "|---|---:|---:|" + ("---|" if check else "")]
    for key, label, f in rows:
        row = f"| {label} | {f.format(real[key])} | {f.format(sim[key])} |"
        if check:
            r, s = real[key], sim[key]
            ok = abs(s - r) <= 0.15 * max(abs(r), 1e-12)
            row += " matched |" if ok else " **NOT MATCHED** |"
        out.append(row)
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("real"); ap.add_argument("sim")
    ap.add_argument("--name", default="Real")
    a = ap.parse_args()
    real, sim = stats(*load(a.real)), stats(*load(a.sim))
    for s in (real, sim):
        s["permanent impact"] = 0.5 * (s["impact after 30s (ticks)"] + s["impact after 60s (ticks)"])

    plt.rcParams.update({"font.size": 10, "axes.spines.top": False, "axes.spines.right": False})
    fig, ax = plt.subplots(1, 3, figsize=(15, 4.2))

    lv = np.arange(1, 11)
    ax[0].plot(lv, real["depth profile, levels 1-10 (per side)"], "o-", color=REAL, label=a.name)
    ax[0].plot(lv, sim["depth profile, levels 1-10 (per side)"], "s--", color=SIM, label="Simulated")
    ax[0].set(title="Depth at each price level  (held out)", xlabel="Level from the best price",
              ylabel="Average resting volume per side", xticks=lv)
    ax[0].legend(frameon=False)

    ks = ["spread = 1 tick (share of time)", "spread = 2 ticks (share of time)", "spread = 3 ticks (share of time)"]
    x = np.arange(3)
    ax[1].bar(x - 0.18, [real[k] for k in ks], 0.36, color=REAL, label=a.name)
    ax[1].bar(x + 0.18, [sim[k] for k in ks], 0.36, color=SIM, label="Simulated")
    ax[1].set(title="Spread  (held out)", xticks=x, xticklabels=["1 tick", "2 ticks", "3 ticks"],
              ylabel="Share of time", ylim=(0, 1))
    ax[1].legend(frameon=False)

    hs = [0, 1, 5, 30, 60]
    ax[2].plot(hs, [real[f"impact after {h}s (ticks)"] for h in hs], "o-", color=REAL, label=a.name)
    ax[2].plot(hs, [sim[f"impact after {h}s (ticks)"] for h in hs], "s--", color=SIM, label="Simulated")
    ax[2].set(title="Price impact  (60s calibrated, earlier held out)",
              xlabel="Seconds after the trade", ylabel="Mid move in the trade's direction (ticks)")
    ax[2].axhline(0, color=INK, lw=0.6)
    ax[2].legend(frameon=False)

    fig.tight_layout()
    fig.savefig("results/validation.png", dpi=150)

    md = [f"## Calibrated simulation vs {a.name}", "",
          "### Held out: never used in calibration", "", table(real, sim, HELD_OUT), "",
          "### Calibrated: measured or tuned to match", "",
          "Agreement here is supposed to be guaranteed; a mismatch means calibration failed.", "",
          table(real, sim, CALIBRATED, check=True), ""]
    with open("results/validation.md", "w") as f:
        f.write("\n".join(md))
    print("\n".join(md))
    print("wrote results/validation.png and results/validation.md")


if __name__ == "__main__":
    main()
