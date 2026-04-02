# Phase 7 Rollout Report (2026-04-02)

## Scope
Close-out evidence for evaluation refactor rollout after Phase 6, covering:
- Provenance integrity (true old-vs-new binaries)
- Tactical reliability non-regression
- Effective node/qNode growth budget
- Search stability checks
- 100-game paired-color self-play strength check

## Artifacts
- Regression CSV: `bench/phase7_results_20260402_110811.csv`
- Regression summary: `bench/phase7_summary_20260402_110811.txt`
- Provenance: `bench/phase7_provenance_20260402_110811.txt`
- Self-play PGN (100 games): `bench/phase7_selfplay_20260402_100g.pgn`
- Self-play summary: `bench/phase7_selfplay_summary_20260402_100g.txt`
- Regression suite: `bench/phase7_suite.txt`
- Self-play openings: `bench/phase7_openings.txt`

## Binary Provenance
- Baseline path: `/home/ksohail-/Documents/chess-engine/bench/bin/phase7_baseline_3560d8c`
- Baseline SHA256: `ee5abfee33040d62415c78d72d8e136154323927514a969ecc34461a2cda071c`
- Candidate path: `/home/ksohail-/Documents/chess-engine/bench/bin/phase7_candidate`
- Candidate SHA256: `d07009ca4508dca522673333ee717894b622feb31307044ca97165530b21cf9b`
- Suite SHA256: `989b24f8d149431faf2edfeb9eeb203123543a4ce007f3a85820f6f4e147ad3e`

## Run Configuration
### Regression harness
- Script: `scripts/phase7_regression.sh`
- Depth: 6 default (case overrides allowed)
- Movetime: 300 ms
- Engine args: `--mode=gui --book=__NO_BOOK__`
- Cases run: 29
- Expected tactical assertions: 2

### Self-play harness
- Script: `scripts/phase7_selfplay.py`
- Games: 100
- Pairing: paired colors by opening (A/B swap each pair)
- Openings: fixed list from `bench/phase7_openings.txt`
- Movetime: 40 ms/move
- Max plies: 120
- Engine args: `--mode=gui --book=__NO_BOOK__`

## Gate Results
| Gate | Target | Result | Status |
|---|---|---|---|
| Provenance integrity | baseline and candidate binaries must differ (path/checksum) | different paths and SHA256 values | PASS |
| Tactical reliability | candidate expected-pass >= baseline expected-pass | baseline 2/2, candidate 2/2 | PASS |
| Effective node growth budget | candidate growth <= 15% | -7.13% (`4,125,935` vs `4,442,551`) | PASS |
| Search stability | no major depth/pruning anomalies | major depth drops: 0/29, delta spikes: 0/29 | PASS |
| Self-play neutral-to-positive | candidate score >= 50% | 43.0/100 (28W/30D/42L) | FAIL |

## Additional Observations
- qNodes trend improved: `-6.87%` (`3,849,712` vs `4,133,685`).
- Delta pruning skips decreased: `578,419` vs `638,837`.
- Regression harness overall gate: PASS.
- Strength gate blocks promotion.

## Recommendation
- **Decision: HOLD**
- Rationale: despite clean tactical/non-regression and search-budget behavior, the 100-game paired-color self-play batch is negative for candidate (43%).

## Promote Conditions
Promote only after a re-tuned candidate achieves all of the following in a repeat run:
1. Provenance integrity PASS
2. Tactical reliability PASS
3. Node budget PASS (<= 15%)
4. Search stability PASS
5. Self-play neutral-to-positive PASS (>= 50% score)
