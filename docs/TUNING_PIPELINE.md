# Unified tuning pipeline

The Phase 21 pipeline runs Bitboard's existing PGN derivation, Stockfish annotation, evaluation fitting and validation, search tuning, time-safety checks, opening-book checks, regressions, promotion inspection, and a versioned candidate build as one resumable workflow. It uses deterministic checksums rather than file existence to decide whether a stage may be reused.

Tuning never promotes automatically. Match validation is also explicit because the promotion policy requires at least 1,000 games. A normal run can finish with a validated private executable while promotion remains blocked.

## Prerequisites

Create `.venv` and install `tools/tuning/requirements.txt`. The pipeline expects an executable Stockfish at `tuning/engines/stockfish`, an existing production engine at `engine/chess-engine`, the tracked opening book, Make and a C++ compiler, plus Node/npm for protocol and Fair Play checks.

Local inputs and outputs use this layout:

```text
tuning/
├── pgn/
│   ├── games-1.pgn
│   └── games-2.pgn
├── engines/
│   └── stockfish
└── runs/
```

`tuning/pgn/`, `tuning/engines/`, and `tuning/runs/` are ignored private directories. Every regular direct child ending in `.pgn` is included in stable lexical order; non-PGN files are ignored.

## Run and resume

The standard full-corpus run needs only a release identifier:

```bash
make tune RELEASE_ID=v2
```

The identifier names the run, candidates, staged profile, and private executable. It accepts values such as `v2`, `2026.1`, and `full-corpus-v1`; unsafe paths and shell characters are rejected.

Use the lower-cost policy for a trial:

```bash
make tune RELEASE_ID=test-v1 TUNE_MODE=prototype
```

Resume an interrupted or failed run with the same effective options:

```bash
make tune-resume RELEASE_ID=v2
```

Completed stages are reused only when their input, configuration, tool, dependency, and output checksums still match. A changed PGN invalidates dataset derivation and downstream work; a changed Stockfish binary invalidates annotation and downstream work.

Inspect without mutation:

```bash
make tune-inspect RELEASE_ID=v2
```

Verify all completed artifacts and the release identity without repeating annotation or matches:

```bash
make tune-verify RELEASE_ID=v2
```

Override Stockfish or its fixed-node budget when intentional:

```bash
make tune RELEASE_ID=v2 \
  STOCKFISH=/path/to/stockfish

make tune RELEASE_ID=v2 STOCKFISH_NODES=200000
```

Time-parameter tuning is opt-in; time-policy safety validation always runs:

```bash
make tune RELEASE_ID=v2 TUNE_TIME=1
```

## Match validation

Normal tuning does not launch the 1,000-game promotion match. Run or resume it explicitly:

```bash
make tune-match RELEASE_ID=v2
```

Each completed game has an indexed record and PGN checksum. Candidate, baseline, suite, or match-configuration changes make the stage stale. Until an accepted match exists, the summary reports `match_validation_missing` and promotion stays blocked.

## Outputs and promotion

The run lives under `tuning/runs/<release-id>/`. Important files are:

- `manifest.json`: deterministic inputs, configuration, graph, candidates, and artifact checksums.
- `state.json`: operational attempts and stage status.
- `logs/<stage>.log`: subprocess output and failure detail.
- `evaluation/`, `search/`, and `time/`: candidate profiles and validation artifacts.
- `summary.json`: current candidate, gate results, and promotion blockers.
- `release/chess-engine-<release-id>`: private versioned executable for an ineligible run.

For example, `v2` produces `tuning/runs/v2/release/chess-engine-v2`. The production `engine/chess-engine`, canonical profile, canonical header, and `.bin` opening book remain unchanged.

Inspect and request preparation after required gates pass:

```bash
make tune-inspect RELEASE_ID=v2
make tune-promote-prepare RELEASE_ID=v2
make tune-verify RELEASE_ID=v2
```

Preparation still does not promote. Final promotion remains the separate Phase 20 hash-locked command and requires the exact promotion ID, expected candidate and staged hashes, and explicit approval.

## Cleaning

Cleanup is scoped to exactly one normalized run and requires confirmation:

```bash
make tune-clean RELEASE_ID=v2 CONFIRM=1
```

It never removes PGNs, engines, canonical profiles, another release, or the production engine.

## Common failures

- **No PGNs found / empty or duplicate PGN:** add readable, non-empty, distinct `.pgn` files directly under `tuning/pgn/`.
- **Stockfish missing:** install it at the default path or pass `STOCKFISH=...`.
- **Stale annotations:** resume; the pipeline regenerates them when the binary, selection, or node policy changes.
- **Insufficient balance:** add corpus coverage for both sides to move, all phases, results, and all three game-level splits.
- **Candidate rejected:** inspect the evaluation/search report; rejection is not silently bypassed.
- **Match validation missing:** run `make tune-match RELEASE_ID=<id>`.
- **Promotion blocked:** inspect `summary.json` and `promotion/inspection.json`; every blocker must pass independently.
- **Interrupted run:** use `make tune-resume`; checkpointed annotation and per-game match records are preserved.

To add data, copy more `.pgn` files into `tuning/pgn/` and resume the same release to invalidate affected stages, or choose a new release ID for an independent run.

Canonical manifests exclude timestamps, absolute paths, elapsed time, and machine-specific temporary paths. Operational state and logs may include wall-clock fields. Compiled bytes can vary by toolchain, but the embedded profile ID/hash and behavior are verified.
