## Calibrated simulation vs MSFT_reactive

### Held out: never used in calibration

| | Real | Simulated |
|---|---:|---:|
| Mean spread (ticks) | 1.007 | 1.794 |
| Spread at 1 tick (share of time) | 99.3% | 26.7% |
| Depth at the touch | 22,420 | 18,301 |
| Depth, level 5 / level 1 | 1.61 | 1.04 |
| 1s returns exactly zero | 94.8% | 93.0% |
| 1s return sd (ticks) | 0.240 | 0.150 |
| 1s return autocorrelation | +0.007 | +0.006 |
| 1s return kurtosis | 30.9 | 21.6 |
| Price impact after 1s | 0.525 | 0.064 |
| Price impact after 60s | 0.538 | 0.552 |
| Median cancelled-order lifetime (s) | 1.51 | 0.10 |

### Calibrated: measured or tuned to match

Agreement here is supposed to be guaranteed; a mismatch means calibration failed.

| | Real | Simulated | |
|---|---:|---:|---|
| New orders per second | 13.17 | 13.19 | matched |
| Trades per second | 0.346 | 0.408 | **NOT MATCHED** |
| Median order size | 300 | 300 | matched |
| Orders placed at the touch | 60.6% | 62.3% | matched |
| Volatility, 60s (ticks/sqrt s) | 0.234 | 0.174 | **NOT MATCHED** |
| Permanent impact (mean of 30s and 60s) | 0.554 | 0.440 | **NOT MATCHED** |
