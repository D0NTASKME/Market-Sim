"""Calibrate, simulate and validate one stock, end to end.

    python3 analysis/pipeline.py TICKER MESSAGE_FILE

Writes results/TICKER.cfg, results/TICKER.json, results/TICKER_validation.{png,md}
and the simulated LOBSTER files. Run it once per stock; nothing in the
pipeline is specific to any one of them.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(__file__)


def sh(cmd):
    print("\n$ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ticker")
    ap.add_argument("message_file")
    ap.add_argument("--grid", default="0,0.2,0.5,1,2,5,10")
    ap.add_argument("--reactive", action="store_true")
    a = ap.parse_args()

    cfg = f"results/{a.ticker}.cfg"
    prefix = f"results/{a.ticker}SIM_2012-06-21_34200000_57600000"
    py = sys.executable

    cal = [py, f"{HERE}/calibrate.py", a.message_file, "--out", cfg, "--grid", a.grid]
    if a.reactive:
        cal.append("--reactive")
    sh(cal)
    sh(["./sim", "run", cfg, prefix])
    sh([py, f"{HERE}/validate.py", a.message_file, prefix + "_message_10.csv", "--name", a.ticker])

    for src, dst in [("results/validation.png", f"results/{a.ticker}_validation.png"),
                     ("results/validation.md", f"results/{a.ticker}_validation.md")]:
        shutil.move(src, dst)
    print(f"\ndone: {cfg}, results/{a.ticker}_validation.png, results/{a.ticker}_validation.md")


if __name__ == "__main__":
    main()
