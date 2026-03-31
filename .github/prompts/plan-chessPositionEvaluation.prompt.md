## Plan: Stronger Chess Position Evaluation

Keep the current tapered MG/EG framework and incremental score cache, then improve in safe phases: remove duplicated eval logic first, add high-impact handcrafted terms (mobility, king safety, richer pawn play), and only then tune weights with regression gates so search behavior stays stable.

**Steps**
1. Phase 1: Capture baseline and invariants.
- Run current tests and record baseline tactical outcomes plus qNodes and deltaPruneSkips.
- Lock invariants before edits: White-relative static score, side-to-move sign flip path, mate/draw conventions.

2. Phase 2: Refactor to one evaluation source of truth. depends on 1
- Consolidate duplicated logic used by both evaluate paths into one shared internal scorer.
- Keep incremental updates unchanged, but route both call sites through the same feature pipeline.
- Reuse precomputed masks where possible to reduce duplicate loops and drift risk.

3. Phase 3: Add quick-win terms with MG/EG weights. depends on 2
- Add piece mobility terms using low-cost bitboard attack counts.
- Extend rook logic to semi-open files and 7th-rank pressure.
- Replace brittle square-specific patterns with generalized bitboard heuristics where possible.

4. Phase 4: Improve king safety and pawn structure depth. depends on 2, parallel with 3 after shared scorer exists
- Add king-zone attack pressure (attacker count/weight near king).
- Add connected passers, candidate passers, backward pawns, and pawn-island structure terms.
- Keep feature cost low to protect quiescence stand-pat throughput.

5. Phase 5: Endgame scaling refinement. depends on 3 and 4
- Improve scaling in low-material positions to avoid overestimating winning chances.
- Keep insufficient-material handling and refine mop-up triggering with material/phase context.

6. Phase 6: Weight tuning and search-coupled calibration. depends on 5
- Centralize coefficients for easier iteration.
- Tune weights in fixed suites (manual first, automated next).
- Recalibrate search constants coupled to eval scale (delta pruning margin, aspiration window).

7. Phase 7: Regression-gated rollout. depends on 6
- Require all current tests to pass.
- Compare before/after on the same tactical/endgame set.
- Accept only if tactical reliability holds and node growth is acceptable.

**Relevant files**
- [src/evaluatePosition.cpp#L477](src/evaluatePosition.cpp "src/evaluatePosition.cpp#L477") — main evaluator entry point and feature accumulation.
- [src/evaluatePosition.cpp#L587](src/evaluatePosition.cpp "src/evaluatePosition.cpp#L587") — duplicate static recomputation path to unify.
- [src/evaluatePosition.cpp#L440](src/evaluatePosition.cpp "src/evaluatePosition.cpp#L440") — incremental MG/EG update hooks used by move making.
- [src/search.cpp#L146](src/search.cpp "src/search.cpp#L146") — quiescence stand-pat usage of evaluation.
- [src/search.cpp#L218](src/search.cpp "src/search.cpp#L218") — negamax and search assumptions tied to eval scale/sign.
- [src/search.cpp#L360](src/search.cpp "src/search.cpp#L360") — iterative deepening and aspiration behavior.
- [include/board.hpp#L98](include/board.hpp "include/board.hpp#L98") — evaluation API surface.
- [tests.cpp#L230](tests.cpp "tests.cpp#L230") — incremental evaluation drift test.
- [tests.cpp#L328](tests.cpp "tests.cpp#L328") — horizon-effect tactical guard.
- [tests.cpp#L413](tests.cpp "tests.cpp#L413") — quiescence counter regression checks.
- [Makefile#L27](Makefile "Makefile#L27") — existing test command flow.

**Verification**
1. Run make test before any changes and after each phase.
2. Keep evaluate parity checks passing in existing make/undo and incremental tests.
3. Track qNodes and deltaPruneSkips before/after to detect over-pruning or scale drift.
4. Re-run tactical tests (mate in 1, hanging material, horizon case) after each feature batch.
5. Run manual UCI spot checks on passed-pawn races, king attacks, and drawish endgames.
6. Run a small fixed-depth self-play batch for final sanity.

**Decisions**
- Included: handcrafted evaluation improvements inside current architecture.
- Excluded: NNUE/tablebases and major search rewrites in the first iteration.
- Assumption: current sign and mate conventions remain unchanged to avoid negamax instability.

**Further considerations**
1. Tuning path recommendation: manual staged tuning first, then Texel/SPSA once features stabilize.
2. Performance target recommendation: keep eval cost increase roughly within 10-15 percent per node.
3. Instrumentation recommendation: add a temporary eval-breakdown output mode during tuning.
