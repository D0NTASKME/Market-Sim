"""Stylised facts of the simulated market.

Reads results/returns_*.csv produced by the simulator and checks the three
classic empirical properties of real asset returns:

  1. Fat tails            - kurtosis well above the normal distribution's 3
  2. Bid-ask bounce       - negative lag-1 autocorrelation of returns
  3. Volatility clustering - positive, slowly decaying autocorrelation of |r|

The fundamental value is Gaussian with constant volatility by construction.
Anything non-Gaussian in the traded price therefore comes from the market
mechanism, not from the input.
"""
import csv
import math
import sys
from collections import defaultdict

import matplotlib.pyplot as plt

LAGS = list(range(1, 31))


def load(path):
    by_seed = defaultdict(list)
    with open(path) as f:
        for row in csv.DictReader(f):
            by_seed[row["seed"]].append(float(row["r"]))
    return by_seed


def kurtosis(xs):
    n = len(xs)
    m = sum(xs) / n
    v = sum((x - m) ** 2 for x in xs) / n
    return sum((x - m) ** 4 for x in xs) / n / v ** 2


def trimmed_kurtosis(xs, keep):
    s = sorted(xs, key=abs)
    return kurtosis(s[: int(len(s) * keep)])


def pooled_acf(by_seed, lag, f):
    # Autocorrelation computed within each independent run and pooled, so we
    # never correlate the last return of one seed with the first of the next.
    allx = [f(r) for v in by_seed.values() for r in v]
    mu = sum(allx) / len(allx)
    den = sum((x - mu) ** 2 for x in allx)
    num = 0.0
    for v in by_seed.values():
        x = [f(r) for r in v]
        num += sum((x[i] - mu) * (x[i - lag] - mu) for i in range(lag, len(x)))
    return num / den


def summarise(label, by_seed):
    allr = [r for v in by_seed.values() for r in v]
    zero = sum(1 for r in allr if r == 0) / len(allr)
    print(f"\n{label}  (n = {len(allr)} one-second returns)")
    print(f"  kurtosis           {kurtosis(allr):8.2f}   (normal = 3)")
    print(f"  kurtosis, 99.9%    {trimmed_kurtosis(allr, 0.999):8.2f}")
    print(f"  kurtosis, 99%      {trimmed_kurtosis(allr, 0.99):8.2f}")
    print(f"  returns exactly 0  {zero:8.2%}   (tick discreteness)")
    print(f"  acf(r)   lag 1     {pooled_acf(by_seed, 1, lambda r: r):+8.3f}   bid-ask bounce")
    for L in (1, 5, 20):
        print(f"  acf(|r|) lag {L:<2}    {pooled_acf(by_seed, L, abs):+8.3f}")


def main():
    runs = [("no informed flow", "results/returns_0.csv"),
            ("informed flow", "results/returns_5.csv")]
    data = [(label, load(path)) for label, path in runs]
    for label, d in data:
        summarise(label, d)

    fig, axes = plt.subplots(1, 3, figsize=(16, 4.5))

    # Tails on a log scale: a normal distribution is a parabola here, so fat
    # tails show up as the empirical curve sitting above it at the edges.
    ax = axes[0]
    for label, d in data:
        allr = [r for v in d.values() for r in v]
        sd = math.sqrt(sum(r * r for r in allr) / len(allr))
        z = [r / sd for r in allr]
        ax.hist(z, bins=121, range=(-15, 15), density=True, histtype="step",
                log=True, label=label)
    xs = [x / 10 for x in range(-150, 151)]
    ax.plot(xs, [math.exp(-x * x / 2) / math.sqrt(2 * math.pi) for x in xs],
            "k--", lw=1, label="normal")
    ax.set_ylim(1e-6, 1)
    ax.set_xlabel("standardised return")
    ax.set_title("Fat tails")
    ax.legend()

    for ax, (name, f) in zip(axes[1:], [("returns", lambda r: r),
                                         ("|returns|", abs)]):
        for label, d in data:
            ax.plot(LAGS, [pooled_acf(d, L, f) for L in LAGS], marker="o",
                    ms=3, label=label)
        ax.axhline(0, color="k", lw=0.8)
        ax.set_xlabel("lag (seconds)")
        ax.set_title(f"Autocorrelation of {name}")
        ax.legend()
    axes[1].set_ylabel("autocorrelation")

    plt.tight_layout()
    out = sys.argv[1] if len(sys.argv) > 1 else "results/stylised_facts.png"
    plt.savefig(out, dpi=150)
    print(f"\nplot saved to {out}")


if __name__ == "__main__":
    main()
