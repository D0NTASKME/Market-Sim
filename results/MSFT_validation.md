## Calibrated simulation vs MSFT

### Held out: never used in calibration

| | Real | Simulated |
|---|---:|---:|
| Mean spread (ticks) | 1.007 | 1.366 |
| Spread at 1 tick (share of time) | 99.3% | 64.3% |
| Depth at the touch | 22,420 | 29,890 |
| Depth, level 5 / level 1 | 1.61 | 0.75 |
| 1s returns exactly zero | 94.8% | 97.0% |
| 1s return sd (ticks) | 0.240 | 0.094 |
| 1s return autocorrelation | +0.007 | +0.034 |
| 1s return kurtosis | 30.9 | 48.0 |
| Price impact after 1s | 0.525 | 0.039 |
| Price impact after 60s | 0.538 | 0.749 |
| Median cancelled-order lifetime (s) | 1.51 | 1.05 |

### Calibrated: measured or tuned to match

Agreement here is supposed to be guaranteed; a mismatch means calibration failed.

| | Real | Simulated | |
|---|---:|---:|---|
| New orders per second | 13.17 | 13.16 | matched |
| Trades per second | 0.346 | 0.390 | matched |
| Median order size | 300 | 300 | matched |
| Orders placed at the touch | 60.6% | 61.4% | matched |
| Volatility, 60s (ticks/sqrt s) | 0.234 | 0.121 | **NOT MATCHED** |
| Permanent impact (mean of 30s and 60s) | 0.554 | 0.584 | matched |
