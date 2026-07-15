# Unified tuning pipeline

The Phase 21.1 pipeline runs Bitboard's PGN derivation, Stockfish annotation, evaluation fitting and validation, search tuning, time-safety checks, opening-book checks, regressions, promotion inspection, and a versioned candidate build as one resumable workflow. Corpus derivation is bounded and disk-backed: PGNs are scanned sequentially, each game contributes only a deterministic phase-aware sample, and SQLite provides deduplication, top-k retention, and durable checkpoints.

Tuning never promotes automatically. Match validation is also explicit because the promotion policy requires at least 1,000 games. A normal run can finish with a validated private executable while promotion remains blocked.

## Prerequisites

Create `.venv` and install `tools/tuning/requirements.txt`. The pipeline expects an executable Stockfish at `tuning/engines/stockfish`, an existing production engine at `engine/chess-engine`, the tracked opening book, Make and a C++ compiler, plus Node/npm for protocol and Fair Play checks.

The tracked laptop policy targets 32 GB RAM. Dataset RSS is warned at 6,144 MiB and aborted at 8,192 MiB; preflight also requires at least 4,096 MiB available memory and 20 GiB free disk after estimated SQLite/export working space. The pipeline does not rely on swap. Stockfish remains one process with one thread and a 256 MiB hash by default.

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

Estimate the bounded work before starting it:

```bash
make tune-estimate \
  RELEASE_ID=modern-otb-v1 \
  TUNE_MODE=full
```

The estimator samples complete games at deterministic offsets and reports measured parser throughput separately from estimates for games, retained positions, SQLite, export space, duration, annotations, RAM, and disk. It does not build a dataset or candidate.

The standard full-corpus run needs only a release identifier:

```bash
make tune RELEASE_ID=v2
```

The identifier names the run, candidates, staged profile, and private executable. It accepts values such as `v2`, `2026.1`, and `full-corpus-v1`; unsafe paths and shell characters are rejected.

Use the lower-cost policy for a trial:

```bash
make tune RELEASE_ID=test-v1 TUNE_MODE=prototype
```

Prototype mode stops after at most 10,000 accepted games, retains at most 100,000 positions, samples at most six per game (opening 2, middlegame 3, endgame 1), and selects 700/150/150 fitting records plus a 60-position audit. It does not scan the remaining million-game corpus.

Full mode scans all PGNs but retains at most 1,000,000 balanced positions, with at most eight per game (opening 2, middlegame 4, endgame 2). It annotates only the explicit 50,000/5,000/5,000 fitting selection plus the 2,000-position audit. Its search suite is 300 development plus 150 holdout positions. The 36-position time suite intentionally remains a structural safety check, not strength evidence.

Resume an interrupted or failed run with the same effective options:

```bash
make tune-resume RELEASE_ID=v2
```

Completed stages are reused only when their input, configuration, tool, dependency, and output checksums still match. A changed PGN invalidates dataset derivation and downstream work; a changed Stockfish binary invalidates annotation and downstream work.

Dataset construction also resumes internally. Its private `dataset/work/dataset.sqlite` records source checksums, safe byte offsets, game indexes, counters, and committed transactions. Ctrl+C, SIGTERM, parser failure, disk failure, or an RSS guard failure rolls back only the active batch; resume continues from the last committed game boundary. Changed inputs or sampling configuration are rejected rather than mixed into an existing database.

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

Dataset progress reports current file, bytes, games, accepted/rejected/duplicate counts, considered and retained positions, RSS, SQLite size, elapsed time, and ETA. “Games scanned” is corpus coverage; “positions retained” is the bounded balanced pool. The dataset publishes one canonical `dataset/positions.jsonl`; it does not duplicate every record into split JSONLs.

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

## Recommended real-corpus workflow

First review the estimate:

```bash
make tune-estimate \
  RELEASE_ID=modern-otb-v1 \
  TUNE_MODE=full
```

Then, when the machine can remain awake, run:

```bash
systemd-inhibit \
  --what=sleep:idle \
  --why="Bitboard full tuning run" \
  make tune \
    RELEASE_ID=modern-otb-v1 \
    TUNE_MODE=full \
    STOCKFISH_NODES=50000
```

The GPU does not accelerate this CPU-bound workflow. Final promotion remains the separate, explicitly approved, hash-locked Phase 20 operation.
