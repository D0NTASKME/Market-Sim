# Market Microstructure Simulator

An event-driven limit order book and agent-based market simulator in C++20, calibrated
against real NASDAQ order-by-order data and validated on statistics held back from
calibration.

The question it was built to answer: **what does informed trading cost a market maker,
and does a simulated market actually resemble a real one?**

The second half of that question is the interesting one. Most agent-based market models
are never checked against data, so their conclusions are true about the model and
unverified about anything else. This one is calibrated to Microsoft's order book on
2012-06-21 and then tested on statistics the calibration never saw.

**Status: active work in progress.** The simulator, calibration pipeline and validation
framework are complete and tested. The model reproduces a real book's shape but not the
speed of its price response — a gap that is measured, explained, and being worked on.
See [Roadmap](#roadmap).

Live demo (the C++ engine compiled to WebAssembly, running in the browser): *[link]*

---

## Quick start

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Iinclude src/*.cpp src/agents/*.cpp -o sim

./sim makers       # market maker comparison across informed-trading intensities
./sim gamma        # risk aversion sweep for the Avellaneda–Stoikov maker
./sim facts        # return series for the stylised-facts analysis
./sim lobster      # export a simulated session in LOBSTER format
./sim run cfg.txt  # run any market from a config file
```

See [Data](#data) to fetch the NASDAQ sample, then:

```bash
python3 analysis/pipeline.py MSFT data/.../MSFT_..._message_10.csv
python3 analysis/pipeline.py MSFT_reactive data/.../MSFT_..._message_10.csv --reactive
```

Each pipeline run calibrates the model to the data, simulates a matching session, and
writes a validation report.

---

## The model

Continuous-time, event-driven, with a priority queue over agent actions. Every agent
draws its own inter-arrival times, so nothing is synchronised to a tick.

| Agent | Behaviour |
|---|---|
| **Uninformed traders** | Trade for reasons unrelated to future prices. Order sizes, placement and lifetimes are drawn from distributions measured on real data. |
| **Market maker** | Quotes both sides. Three strategies: fixed quotes, quotes skewed against inventory, and Avellaneda–Stoikov. |
| **Informed trader** | Sees the fundamental value and takes liquidity when the mid is wrong. |
| **Reactive liquidity providers** | Algorithmic quoters that pull their quotes on the side a trade just hit, and reprice continuously. |

The order book is a price-indexed structure with O(1) cancellation via an order-id index;
`std::list` is used at each level because iterator stability is what makes cancellation
O(1).

Everything the simulator does is driven by a plain-text config file, so a market
calibrated to real data and the original toy market are the same code with different
numbers.

---

## Results

### 1. Adverse selection scales with informed intensity

`./sim makers`, 30 seeds per cell, 95% confidence intervals:

| Informed rate | Maker | Total P&L | vs informed | mean abs inventory |
|---:|---|---:|---:|---:|
| 0 | fixed | −12,291 ± 23,798 | 0 | 1,197 |
| 0 | skewed | **515 ± 424** | 0 | 27 |
| 0 | A–S | 365 ± 495 | 0 | 27 |
| 5 | fixed | −13,654 ± 22,696 | −10,817 | 1,494 |
| 5 | skewed | 901 ± 1,572 | −6,385 | 35 |
| 5 | A–S | 1,251 ± 1,346 | −6,105 | 47 |
| 10 | fixed | −17,656 ± 20,276 | −15,141 | 1,451 |
| 10 | skewed | −4,322 ± 3,909 | −11,869 | 37 |
| 10 | A–S | −5,109 ± 3,884 | −12,804 | 46 |
| 20 | fixed | 2,866 ± 26,875 | −23,827 | 1,375 |
| 20 | skewed | −11,067 ± 5,373 | −19,315 | 38 |
| 20 | A–S | −10,081 ± 5,792 | −18,596 | 49 |

Income from uninformed flow is roughly flat across intensities; losses to informed flow
grow steadily with it. The maker's profitability is decided almost entirely by how much
informed flow it faces — not by how much it trades.

### 2. Inventory control is what keeps a market maker solvent

The fixed-quote maker's confidence intervals are 20,000–27,000 wide and its average
absolute inventory is around 1,400: its position is an uncontrolled random walk, and its
P&L is dominated by where that walk happened to end. Leaning quotes against inventory
cuts average inventory to under 40 and shrinks the intervals by an order of magnitude.

### 3. Avellaneda–Stoikov is not better here, and the reason is instructive

A–S is statistically indistinguishable from a hand-tuned linear skew in this market. Its
optimality is derived under the assumption of **no informed flow**; when informed traders
are present, the assumption its proof rests on is exactly the thing being tested.

A risk-aversion sweep (`./sim gamma`) shows catastrophic failure at γ ≥ 1e-4 — the
reservation-price shift exceeds the spread, so the "maker" crosses the book and becomes a
liquidity taker. A real, findable limit of the model's applicable range.

### 4. Stylised facts emerge from the mechanism

Pooled over 80 runs of 300 simulated seconds, with a Gaussian fundamental as input:

- **Fat tails** — return kurtosis 4.3 without informed flow, 6.3 with (normal = 3)
- **Volatility clustering** — autocorrelation of absolute returns +0.23 → +0.34 at lag 1, still +0.058 at lag 20
- **Negative return autocorrelation** at lag 1, from spread dynamics

None of this is put in; it comes out of the market mechanism.

---

## Calibration to real data

Feed the calibrator a LOBSTER file and it writes a config. Parameters are handled in
three ways, and the config file labels each one:

**Measured directly** — order arrival rate, the aggressive share, order size
distribution, placement relative to the best price, and order lifetimes by distance from
the touch.

**Tuned by simulation** — the informed trading rate and the fundamental's volatility.
Neither is observable, so the simulator is run across a grid and the values whose
**permanent price impact** and **60-second volatility** match the real stock are chosen
(method of simulated moments). The two interact, so the search alternates between them
until both settle.

**Held back** — spread, depth, depth profile, zero-return share, return autocorrelation,
kurtosis and short-horizon price impact are never used. They are the test.

**Fixed by assumption** — the market maker's parameters and the informed trader's
threshold cannot be identified from public order book data. The config file lists them
under `# --- ASSUMED ---`.

### What Microsoft's order book looks like

| | Touch | 1–2 ticks back | 3+ ticks back |
|---|---:|---:|---:|
| Median lifetime of cancelled orders | **0.02 s** | 9.69 s | 15.51 s |
| Cancelled within 100 ms | **55.5%** | 9.8% | 8.4% |

More than half the liquidity at the best price is cancelled within a tenth of a second.
Those are not traders changing their minds; they are algorithms repricing. This
measurement motivated the reactive agents below.

### Validation: calibrated model vs Microsoft

Held out — never used in calibration:

| | Real | Simulated |
|---|---:|---:|
| Mean spread (ticks) | 1.007 | 1.366 |
| Spread at 1 tick (share of time) | 99.3% | 64.3% |
| Depth at the touch | 22,420 | 29,890 |
| Depth, level 5 ÷ level 1 | 1.61 | 0.75 |
| 1s returns exactly zero | 94.8% | 97.0% |
| 1s return sd (ticks) | 0.240 | 0.094 |
| 1s return kurtosis | 30.9 | 48.0 |
| **Price impact after 1s** | **0.525** | **0.039** |
| Price impact after 60s | 0.538 | 0.749 |

The order flow matches. The price dynamics do not. **Microsoft's price finishes moving
within one second of a trade; the simulated price takes a minute to get there.**

---

## Reactive liquidity providers

A liquidity provider that withdraws its quotes on the side a trade just hit, and reprices
continuously. Its two parameters — the share of touch liquidity that behaves this way
(55.5%) and its quote lifetime (0.02 s) — are **measured from the data, not tuned**.
Nothing else in the calibration changes, so this is a clean test of one mechanism.

Predictions were written down before running it. Results:

| Held out | Without | With | Microsoft | |
|---|---:|---:|---:|---|
| 1s returns exactly zero | 97.0% | **94.9%** | 94.8% | ✓ |
| Return kurtosis | 48.0 | **28.5** | 30.9 | ✓ |
| Depth, level 5 ÷ level 1 | 0.75 | **1.15** | 1.61 | ✓ |
| 1s return sd | 0.094 | **0.127** | 0.240 | partial |
| Spread at 1 tick | 64.3% | 52.4% | 99.3% | ✗ worse |
| **Price impact after 1s** | 0.039 | 0.073 | **0.525** | ✗ |

Four statistics improve markedly, one gets worse, and the headline prediction fails.

**Why it fails, and it is a real explanation.** These agents only *withdraw* liquidity.
To move the price up a tick, someone must *post* at a better price, and in a one-tick
market nobody can until the entire queue ahead clears — most of which belongs to ordinary
traders who ignore the trade completely. Fading is half the mechanism; the other half is
repricing around a belief that updates on order flow, which is Glosten–Milgrom proper.

**The current finding:**

> Calibrated zero-intelligence order flow reproduces a real market's book — spread,
> depth and shape — but not the speed of its price response. Adding liquidity providers
> that withdraw on trades recovers the permanent price impact, the 100 ms order
> lifetimes, the return distribution and the depth profile, but not the speed. Prices
> respond that fast only if liquidity providers actively requote around an updated
> belief.

---

## How it's tested

- **Regression on every change** — all experiment outputs are byte-compared before and after refactors; the phase 1–6 results reproduce exactly
- **Determinism** — same seed, byte-identical output
- **Exporter consistency** — `analysis/verify_lobster.py` rebuilds the book by replaying the simulator's own LOBSTER messages and compares against the order book file (0 mismatches over 8,419 checks)
- **Incremental stepping equals batch** — stepping in 0.37 s increments gives results identical to one 300 s call, which is what allows interactive use
- **Parameter recovery** — the calibrator is run against simulated data with known parameters. On a market where prices track fair value it recovers a hidden informed rate of 1.5 as 1.64, with a stated range of 1–2 containing the truth
- **Self-diagnosis** — run against the Microsoft-calibrated model's own output, the calibrator reports `NOT IDENTIFIED`: in that regime, price impact never reaches the target at any informed rate, so the rate is a lower bound rather than a measurement. A calibration tool that knows when it cannot measure something is more useful than one that always returns a number

---

## What I got wrong

The bugs worth reading, because each one was invisible until something specific exposed
it.

**Pop before peek.** The event loop popped an event, then discarded it if it was past the
end time — destroying that agent's next scheduled action. Harmless in one batch run,
fatal when the loop is called repeatedly for interactive use.

**Silent string replacement.** A scripted edit to `submit()` and `cancel()` didn't match
the file text, so it changed nothing and reported success. Every LOBSTER export came out
containing only executions. Now every scripted edit asserts that it applied.

**A fallback that never fell back.** Emscripten's loader throws *synchronously* when
WebAssembly is unavailable, so a `.catch()` handler never fires. Only found by disabling
WebAssembly in a test browser.

**A canvas that doubled every frame.** Setting `canvas.height` for a high-resolution
display also rewrites the `height` attribute — which the code was reading as the intended
size. At device pixel ratio 1 this is multiplying by one; on a Retina display the canvas
grew to millions of pixels within a second.

**Two tuning stages undoing each other.** The calibrator tuned the informed rate, then the
volatility — but volatility also changes impact, so the first result was invalid by the
end. Fixed by alternating and accepting a step only when it improves the worse of both
errors.

**Embind picks constructors by argument count, not type.** Two three-argument
constructors collided and aborted the WebAssembly module during static initialisation,
with a stack trace pointing only at the runtime initialiser.

**A market that couldn't discover prices.** The original noise traders never cancelled, so
the book grew too deep for informed trading to move the price. Adding realistic order
lifetimes tripled the measured cost of adverse selection. Cancellation is not a detail; it
is what makes price discovery possible.

---

## Roadmap

**Next: belief-updating liquidity providers.** Quotes centred on a belief that jumps on
trade direction (Glosten–Milgrom), rather than merely withdrawing. The test is whether
1-second price impact rises toward 0.52 and the impact curve becomes jump-then-flat,
*without* retuning anything else.

**More stocks.** The pipeline is stock-agnostic; only Microsoft has been run so far. Intel
is another large-tick stock and should replicate. Apple in 2012 traded near $580 with a
multi-tick spread — a different regime entirely, and a real test of where the model
applies.

**Out-of-sample calibration.** Calibrate on the morning, validate on the afternoon.

**Identification study.** The informed rate is identified when prices track fair value and
not otherwise. Mapping that boundary is a result in its own right.

**Learned market making.** Order-flow imbalance predicts mispricing in this simulator, and
unlike a real market the ground truth is known, so a learned predictor can be evaluated
honestly. Deferred deliberately until the market it trades in is believable.

---

## Data

Order book data is not included in this repository. Download the LOBSTER MSFT sample:

```bash
pip install huggingface_hub
python3 -c "from huggingface_hub import snapshot_download; \
  snapshot_download('totalorganfailure/lobster-data', repo_type='dataset', \
                    allow_patterns=['*MSFT*_10*'], local_dir='data')"
```

Source: LOBSTER (lobsterdata.com), sample day 2012-06-21, via a Hugging Face mirror.

---

## Repository layout

```
include/, src/          simulator: order book, market, agents, config
  agents/               noise traders, market makers, informed, calibrated, reactive
analysis/
  calibrate.py          LOBSTER data -> model config
  validate.py           real vs simulated, held-out and calibrated statistics
  lob_stats.py          shared measurement code, run identically on both
  pipeline.py           calibrate + simulate + validate, one command per stock
  verify_lobster.py     consistency check on the simulator's own export
  stylised_facts.py     return distribution analysis
web/                    Emscripten bindings and build script
results/                configs and validation reports
```

---

## References

- Glosten & Milgrom (1985), *Bid, Ask and Transaction Prices in a Specialist Market*
- Kyle (1985), *Continuous Auctions and Insider Trading*
- Avellaneda & Stoikov (2008), *High-frequency Trading in a Limit Order Book*
- Roll (1984), *A Simple Implicit Measure of the Effective Bid-Ask Spread*
- Cont, Kukanov & Stoikov (2014), *The Price Impact of Order Book Events*
