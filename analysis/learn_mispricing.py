"""How much of the informed trader's private information leaks into order flow?

Trains predictors of the hidden mispricing (fundamental - mid) from features a
market maker can observe, and reports out-of-sample R^2 at each informed rate.

Train/test split is BY SEED. Each seed is an independent market. Splitting
rows at random within a seed would let the model see the same price path in
training and test, and report skill it doesn't have.
"""
import sys
import numpy as np
import pandas as pd
from sklearn.ensemble import HistGradientBoostingRegressor
from sklearn.linear_model import LinearRegression
from sklearn.metrics import r2_score

FEATURES = ["ofi_1s", "ofi_5s", "trade_signs_10", "book_imbalance",
            "spread", "dmid_1s", "dmid_5s", "maker_inventory"]

df = pd.read_csv(sys.argv[1] if len(sys.argv) > 1 else "results/features.csv")
train_seeds = set(range(1, 31))          # seeds 1-30 train, 31-40 test

print(f"{'rate':>5} {'n_test':>7} {'linear R2':>10} {'boosted R2':>11}   target sd")
rows = []
for rate, g in df.groupby("rate"):
    tr, te = g[g.seed.isin(train_seeds)], g[~g.seed.isin(train_seeds)]
    lin = LinearRegression().fit(tr[FEATURES], tr.target)
    gb = HistGradientBoostingRegressor(max_iter=300, learning_rate=0.05,
                                       random_state=0).fit(tr[FEATURES], tr.target)
    r_lin = r2_score(te.target, lin.predict(te[FEATURES]))
    r_gb = r2_score(te.target, gb.predict(te[FEATURES]))
    rows.append((rate, r_lin, r_gb, lin))
    print(f"{rate:5.0f} {len(te):7d} {r_lin:10.3f} {r_gb:11.3f}   {te.target.std():8.2f}")

# Which features carry the signal? Standardised linear coefficients at rate 10.
rate, _, _, lin = [r for r in rows if r[0] == 10.0][0]
g = df[df.rate == 10.0]
sd = g[FEATURES].std()
print("\nstandardised linear coefficients, informed rate 10 (effect of a 1-sd move, in ticks):")
for f, c in sorted(zip(FEATURES, lin.coef_ * sd.values), key=lambda x: -abs(x[1])):
    print(f"  {f:16s} {c:+7.2f}")
