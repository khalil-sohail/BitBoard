# Chess Engine Architecture Deep Dive

This document explains the engine architecture in detail, focused on board representation, move generation, search, and evaluation. Every concept block follows the same format:

- What it is and where it lives
- How it works and the math behind it
- Why it exists and why it is implemented this way

## 0. Module Map

- Public board API and state model: [include/board.hpp](include/board.hpp#L43)
- Board state loading, Polyglot hashing, repetition checks: [src/Board.cpp](src/Board.cpp#L28)
- Move generation, attack detection, perft: [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L133)
- Move application, undo, null move, input parsing: [src/piecesMovement.cpp](src/piecesMovement.cpp#L161)
- Search API: [include/search.hpp](include/search.hpp#L19)
- Search implementation: [src/search.cpp](src/search.cpp#L11)
- Evaluation implementation and tuning constants: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L28)

---

## 1. Board Representation

### 1.1 Concept: Bitboard State Model

- What it is and where it lives:
  - State storage is a 2 x 6 bitboard tensor in [include/board.hpp](include/board.hpp#L55).
  - Color and piece enums are declared in [include/board.hpp](include/board.hpp#L11) and [include/board.hpp](include/board.hpp#L16).
  - File/rank masks are in [include/board.hpp](include/board.hpp#L74).

- How it works and the math:
  - Each 64-bit integer represents one piece set on 64 squares.
  - Square indexing uses:
    $$\text{square} = 8 \cdot \text{rank} + \text{file}$$
    with file in [0, 7], rank in [0, 7].
  - Occupancy is bitwise union of per-piece bitboards, implemented in [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L168) and [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L177).

- Why and design decisions:
  - Bitboards enable branch-light, SIMD-like bit math (shifts, masks, popcount) for fast move generation/evaluation.
  - The 2 x 6 layout aligns naturally with color and piece loops, reducing conditionals.

### 1.2 Concept: Board Utility Accessors

- What it is and where it lives:
  - Occupancy and square query functions: [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L168), [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L181), [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L189).
  - Coordinate conversion: [src/piecesMovement.cpp](src/piecesMovement.cpp#L140), [src/piecesMovement.cpp](src/piecesMovement.cpp#L149).

- How it works and the math:
  - Square presence check uses mask test:
    $$\text{occupied}(s) \Leftrightarrow (\text{occ} \& (1 \ll s)) \neq 0$$
  - squareToString and squareFromString map between algebraic coordinates and square indices.

- Why and design decisions:
  - These helpers centralize conversions and avoid repeated ad hoc indexing logic.
  - pieceAt performs linear scan over color/piece sets; this is simple and acceptable for infrequent use in parsing/scoring paths.

### 1.3 Concept: Undo State and Incremental State Cache

- What it is and where it lives:
  - Undo snapshot structure: [include/board.hpp](include/board.hpp#L45).
  - Undo stack and hash history: [include/board.hpp](include/board.hpp#L64), [include/board.hpp](include/board.hpp#L66).
  - Move/undo logic: [src/piecesMovement.cpp](src/piecesMovement.cpp#L161), [src/piecesMovement.cpp](src/piecesMovement.cpp#L298).

- How it works and the math:
  - Before makeMove, current state is pushed to m_undoStack and hash to m_hashHistory.
  - undoMove restores full board state and hash in O(1) from snapshots.

- Why and design decisions:
  - Search repeatedly applies and reverts moves; O(1) undo is mandatory for speed.
  - Snapshot-based undo is robust and easy to reason about, at the cost of memory bandwidth.

### 1.4 Concept: Polyglot Zobrist Hashing and Repetition

- What it is and where it lives:
  - Full hash computation: [src/Board.cpp](src/Board.cpp#L28).
  - Incremental hash component helpers: [src/piecesMovement.cpp](src/piecesMovement.cpp#L33), [src/piecesMovement.cpp](src/piecesMovement.cpp#L39), [src/piecesMovement.cpp](src/piecesMovement.cpp#L48).
  - Repetition check: [src/Board.cpp](src/Board.cpp#L87).

- How it works and the math:
  - Hash is XOR over random keys for piece-square occupancy, castling rights, en-passant file (conditionally), and side-to-move.
  - Core identity:
    $$H = \bigoplus K_{piece,color,square} \oplus K_{castle} \oplus K_{ep} \oplus K_{stm}$$
  - Incremental makeMove updates hash by XORing out old components and XORing in new ones.

- Why and design decisions:
  - Zobrist hashing gives near-O(1) position identity for transposition table and repetition detection.
  - Polyglot-compatible key construction simplifies opening-book interoperability.

### 1.5 Concept: Move Application Pipeline

- What it is and where it lives:
  - Main mutation routine: [src/piecesMovement.cpp](src/piecesMovement.cpp#L161).
  - Legal wrapper: [src/piecesMovement.cpp](src/piecesMovement.cpp#L320).

- How it works and the math:
  - The pipeline handles, in order: snapshot push, hash cleanup, moving piece removal/addition, captures, en-passant capture square, promotion replacement, rook motion in castling, castling-right updates, en-passant update, side toggle.
  - Each board and hash transition is mirrored incrementally to keep state and hash coherent.

- Why and design decisions:
  - A single canonical move pipeline prevents state divergence between search and GUI/CLI paths.
  - Incremental eval/hash updates avoid expensive recomputation after each ply.

### 1.6 Concept: Null Move State Transition

- What it is and where it lives:
  - null move and undo: [src/piecesMovement.cpp](src/piecesMovement.cpp#L283), [src/piecesMovement.cpp](src/piecesMovement.cpp#L291).
  - Saved en-passant square field: [include/board.hpp](include/board.hpp#L59).

- How it works and the math:
  - Null move clears en-passant, flips side-to-move, updates hash accordingly, and does not alter piece placement.

- Why and design decisions:
  - Null move pruning in search needs a cheap "pass" operator.
  - En-passant handling is critical because ep rights are side-sensitive and hash-relevant.

---

## 2. Move Generation

### 2.1 Concept: Attack Primitives (Knight, King, Bishop, Rook)

- What it is and where it lives:
  - Primitive attack generators: [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L33), [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L50), [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L65), [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L94).

- How it works and the math:
  - Knight and king attacks are fixed shift/mask expressions.
  - Sliding attacks iterate ray directions until blocker intersection.
  - Pop-LSB iteration uses:
    $$s = \operatorname{ctz}(bb),\quad bb \leftarrow bb \& (bb - 1)$$

- Why and design decisions:
  - Fixed-mask leapers are fast and branch-minimal.
  - Ray stepping is straightforward and maintainable without magic bitboards.

### 2.2 Concept: Pseudo-Legal Move Generation

- What it is and where it lives:
  - Full pseudo-legal generator: [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L250).

- How it works and the math:
  - Builds moves by piece class using own occupancy, opponent occupancy, and all occupancy.
  - Pawns use directional shifts for push/capture/promotion.
  - Non-pawn pieces use attack masks intersected with not-own-occupancy.

- Why and design decisions:
  - Pseudo-legal first, legality later, simplifies generation logic and keeps hot loops compact.

### 2.3 Concept: Pawn Rules (Push, Double Push, Capture, Promotion, En Passant)

- What it is and where it lives:
  - Pawn logic lives inside pseudo generators: [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L262) and capture-only variant [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L487).

- How it works and the math:
  - White pushes via left shift, black via right shift.
  - Promotion is rank-mask gated (RANK_8 for white, RANK_1 for black).
  - Double push checks two-square emptiness and start rank masks.
  - En-passant target is matched by bitmask intersection with shifted pawn attack lanes.

- Why and design decisions:
  - Encodes all pawn irregularities in one place, avoiding special-case leakage into search.

### 2.4 Concept: Castling Rule Encoding

- What it is and where it lives:
  - Castling generation checks in [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L455).
  - Castling rights updates during makeMove in [src/piecesMovement.cpp](src/piecesMovement.cpp#L251).

- How it works and the math:
  - Requires rights bit present, transit squares empty, and king path squares unattacked.
  - Rights stored as bitmask WK/WQ/BK/BQ.

- Why and design decisions:
  - Bitmask rights are compact and easy to update on king/rook movement and rook capture squares.

### 2.5 Concept: Capture-Only Move Generation for Quiescence

- What it is and where it lives:
  - Capture-only generator: [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L487).

- How it works and the math:
  - Generates only capture and promotion-capture moves, plus en-passant captures.
  - For pieces, target set is intersected directly with opponent occupancy.

- Why and design decisions:
  - Quiescence needs tactical continuations only; generating quiet moves would explode tree size.

### 2.6 Concept: Legal Move Filtering

- What it is and where it lives:
  - Legal generator: [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L660).

- How it works and the math:
  - For each pseudo move, clone board, make move, reject if side that moved is in check.

- Why and design decisions:
  - Correct-by-construction and simple.
  - Tradeoff: copy-based legality is slower than pinned-piece-aware generation but reduces complexity.

### 2.7 Concept: Perft Verification

- What it is and where it lives:
  - perft recursion: [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L676).

- How it works and the math:
  - Classic node-count recursion over legal move tree with depth base cases.

- Why and design decisions:
  - perft is the standard correctness oracle for move generation.

---

## 3. Search Architecture

### 3.1 Concept: Search Constants and State

- What it is and where it lives:
  - Core constants: [src/search.cpp](src/search.cpp#L11).
  - Runtime counters and timing globals: [src/search.cpp](src/search.cpp#L137), declarations in [include/search.hpp](include/search.hpp#L10).

- How it works and the math:
  - Values are centipawn-scale integer bounds and pruning parameters.
  - TIME_CHECK_MASK triggers periodic time checks every 2048 nodes.

- Why and design decisions:
  - Centralized constants simplify tuning and behavior predictability.

### 3.2 Concept: Move Ordering (MVV-LVA, TT Move, Killer Moves)

- What it is and where it lives:
  - MVV-LVA table and scoring: [src/search.cpp](src/search.cpp#L51), [src/search.cpp](src/search.cpp#L81).
  - TT-prioritized stable sort: [src/search.cpp](src/search.cpp#L116).
  - Killer move prioritization: [src/search.cpp](src/search.cpp#L125), update in [src/search.cpp](src/search.cpp#L349).

- How it works and the math:
  - Capture score is victim-most-valuable / attacker-least-valuable style.
  - TT move receives fixed top score to search first.
  - Killer quiets are floated to front on beta cutoffs.

- Why and design decisions:
  - Better ordering dramatically increases alpha-beta cutoff rate, reducing effective branching factor.

### 3.3 Concept: Time Management and Cooperative Abort

- What it is and where it lives:
  - checkTime and abort flag: [src/search.cpp](src/search.cpp#L146).
  - periodic abort checks: [src/search.cpp](src/search.cpp#L68).

- How it works and the math:
  - Nodes are counted; every TIME_CHECK_MASK interval, elapsed time is compared with allocatedTimeMs.
  - Search exits cooperatively via timeAborted atomic flag.

- Why and design decisions:
  - Avoids expensive wall-clock query every node while preserving responsiveness.

### 3.4 Concept: Quiescence Search and Delta Pruning

- What it is and where it lives:
  - Quiescence routine: [src/search.cpp](src/search.cpp#L158).

- How it works and the math:
  - Computes stand-pat static score if not in check.
  - Alpha-beta on tactical continuation set (captures; full legal if in check).
  - Delta prune condition:
    $$\text{if } standPat + capturedValue + margin < \alpha,\ \text{skip move}$$
    implemented at [src/search.cpp](src/search.cpp#L198).

- Why and design decisions:
  - Solves horizon effect by extending only volatile tactical lines.
  - Delta pruning cuts obviously insufficient captures.

### 3.5 Concept: Negamax Core with Alpha-Beta

- What it is and where it lives:
  - Main recursive routine: [src/search.cpp](src/search.cpp#L230).

- How it works and the math:
  - Negamax identity:
    $$V(s,d,\alpha,\beta)=\max_{m\in Legal(s)}\left[-V(s_m,d-1,-\beta,-\alpha)\right]$$
  - Mate-distance normalization via ply offset at [src/search.cpp](src/search.cpp#L245).
  - Repetition draw shortcut at [src/search.cpp](src/search.cpp#L241).

- Why and design decisions:
  - Negamax collapses min/max logic into one symmetric routine and pairs naturally with side-to-move scoring.

### 3.6 Concept: Transposition Table (Direct-Mapped)

- What it is and where it lives:
  - TT structures: [src/search.cpp](src/search.cpp#L19), [src/search.cpp](src/search.cpp#L25), [src/search.cpp](src/search.cpp#L33).
  - Probe/use in negamax: [src/search.cpp](src/search.cpp#L252).
  - Store policy: [src/search.cpp](src/search.cpp#L369).

- How it works and the math:
  - Index by modulo hash.
  - Uses Exact/Alpha/Beta flags.
  - Replacement policy keeps entry if slot empty, same hash, or new depth is at least stored depth.

- Why and design decisions:
  - TT avoids re-search of transposed positions and improves move ordering via stored best move.
  - Direct-mapped table is cache-friendly and simple.

### 3.7 Concept: Null Move Pruning

- What it is and where it lives:
  - Null move condition and reduced search: [src/search.cpp](src/search.cpp#L280).

- How it works and the math:
  - If depth >= 3, not in check, and side has non-pawn material, run a reduced null-window search after null move.
  - If null score >= beta, assume fail-high and cut.

- Why and design decisions:
  - Uses the null-move observation: if passing still keeps position above beta, full search is usually unnecessary.

### 3.8 Concept: Late Move Reduction and Check Extension

- What it is and where it lives:
  - LMR logic: [src/search.cpp](src/search.cpp#L326).
  - Check extension: [src/search.cpp](src/search.cpp#L323).

- How it works and the math:
  - Quiet late moves (depth >= 3, moveCount >= 4, no check) are searched at reduced depth first.
  - If reduced search returns in (alpha, beta), re-search full depth.
  - Moves giving check receive +1 extension.

- Why and design decisions:
  - LMR reallocates effort from likely-irrelevant late quiet moves to critical lines.
  - Check extension protects tactical correctness in forcing lines.

### 3.9 Concept: Iterative Deepening and Aspiration Windows

- What it is and where it lives:
  - Root driver: [src/search.cpp](src/search.cpp#L378).

- How it works and the math:
  - Depth loop from 1 to maxDepth.
  - From depth >= 4, searches within aspiration band:
    $$[prevScore - w,\ prevScore + w],\quad w=75$$
    then falls back to full window on fail-low/fail-high.
  - Stops early using stability and elapsed-time heuristics.

- Why and design decisions:
  - Iterative deepening improves move ordering and provides best-so-far move under time pressure.
  - Aspiration windows reduce node count when score drift is small.

---

## 4. Evaluation Architecture

### 4.1 Concept: EvalWeights Namespace and Material Anchors

- What it is and where it lives:
  - EvalWeights namespace: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L28).
  - MG/EG base values and phase increments: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L30), [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L34), [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L38).

- How it works and the math:
  - Every piece contributes base material plus PST term in middle/endgame channels.
  - Game phase is the weighted sum of remaining non-king pieces.

- Why and design decisions:
  - Keeping base material separate from positional terms preserves evaluation sanity and interpretability.

### 4.2 Concept: Piece-Square Tables and Color Mirroring

- What it is and where it lives:
  - MG and EG PST arrays: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L201), [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L264).
  - Mirroring helpers: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L327), [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L331).

- How it works and the math:
  - PST index is mirrored for black so one table orientation is reused:
    $$idx = \begin{cases}s & \text{white}\\ s \oplus 56 & \text{black}\end{cases}$$

- Why and design decisions:
  - Single-orientation PST data avoids duplicate tables and reduces tuning surface.

### 4.3 Concept: Incremental Piece Delta Cache

- What it is and where it lives:
  - pieceScoreDelta: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1133).
  - Incremental add/remove hooks: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1145), [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1154).
  - Full rebuild fallback: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1163).

- How it works and the math:
  - For each piece-square event, update mgScore, egScore, and gamePhase by cached delta.

- Why and design decisions:
  - Avoids O(64) full rescans every makeMove in search.

### 4.4 Concept: Evaluation Entry Points

- What it is and where it lives:
  - evaluate and side-relative evaluate: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1180), [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1190).
  - Static full recompute checker: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1195).
  - Diagnostic breakdown print: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1229).

- How it works and the math:
  - evaluate starts from incremental mg/eg caches, then applies feature terms and taper.
  - evaluateSideToMove flips sign for black-to-move, matching negamax assumptions.

- Why and design decisions:
  - Side-to-move perspective is essential for sign-consistent alpha-beta recursion.

### 4.5 Concept: Mobility and Activity Features

- What it is and where it lives:
  - Mobility extraction: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L523).
  - Rook file/seventh-rank activity: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L577).

- How it works and the math:
  - Mobility uses popcount of legal attack reach masked by not-own-occupancy.
  - Weighted by per-piece MG/EG mobility coefficients.

- Why and design decisions:
  - Mobility proxies piece freedom and latent tactical potential with low computational overhead.

### 4.6 Concept: Pawn Structure and Pawn-Feature Stack

- What it is and where it lives:
  - Connected pawns: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L608).
  - Candidate passed pawns: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L676).
  - Backward pawns: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L723).
  - Pawn islands: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L792).
  - Doubled/isolated penalties: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L967).

- How it works and the math:
  - Connected/candidate/backward terms are rank-weighted tables by relative advancement.
  - Pawn islands count contiguous occupied-file segments.
  - Doubled penalty scales by extra pawns per file; isolated penalty scales by lack of adjacent file pawns.

- Why and design decisions:
  - Pawn structure is long-term and high signal for both strategy and endgame conversion.

### 4.7 Concept: Passed Pawn Scoring

- What it is and where it lives:
  - Passed count term: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L412) and application in [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1641).
  - Passed path bonus: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1022).

- How it works and the math:
  - Count bonus rewards number of passed pawns.
  - Advancement bonus uses quadratic rank growth:
    $$bonus \propto (relativeRank)^2 \cdot multiplier$$
  - Blocked forward square applies divisor attenuation.

- Why and design decisions:
  - Separating count and advancement captures both strategic potential and immediate promotion pressure.

### 4.8 Concept: King Safety Features

- What it is and where it lives:
  - King zone masks and pressure: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L343), [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L801).
  - Pawn shield bonus: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L860).
  - Uncastled penalty: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1115).

- How it works and the math:
  - Attack pressure counts enemy pieces attacking king-zone squares and maps count to a nonlinear penalty table.
  - Shield score counts friendly pawns in near-king mask up to a cap.

- Why and design decisions:
  - These terms model tactical vulnerability in a static evaluator without expensive deep king attack search.

### 4.9 Concept: Development and Piece-Placement Heuristics

- What it is and where it lives:
  - Trapped rook: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1068).
  - Bad bishop: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1084).
  - Early queen development penalty: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1100).

- How it works and the math:
  - Encodes targeted patterns as direct penalties.
  - These are handcrafted sparse heuristics, not generalized feature extractors.

- Why and design decisions:
  - Pattern penalties patch known strategic blind spots quickly and cheaply.

### 4.10 Concept: Endgame Draw Logic, Mop-Up, and Scale Factors

- What it is and where it lives:
  - Insufficient material detector: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L875).
  - Low-material scale factor: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L910).
  - Mop-up bonus: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L997).

- How it works and the math:
  - Forced draw returns zero immediately for selected dead-material sets.
  - Mop-up rewards central restriction and king proximity in winning endgames.
  - Low-material scale multiplies final score by fraction of TAPER_SCALE.

- Why and design decisions:
  - Prevents false conversion optimism in drawish material classes.
  - Improves practical endgame technique when materially ahead.

### 4.11 Concept: Unified Scoring Pipeline and Tapering

- What it is and where it lives:
  - Unified evaluator pipeline: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1493).
  - Taper formula: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1693).
  - No-pawn side attenuation: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1695).
  - Final low-material scaling: [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1701).

- How it works and the math:
  - Start from incremental MG/EG base and add all feature terms.
  - Blend phases:
    $$score_{tapered}=\frac{MG \cdot phase + EG \cdot (24-phase)}{24}$$
  - If advantaged side has no pawns, halve score magnitude in that direction.
  - Apply low-material scaling:
    $$score_{final}=\frac{score_{tapered}\cdot scale}{TAPER\_SCALE}$$

- Why and design decisions:
  - Dual-channel MG/EG structure captures opening-vs-endgame value shifts cleanly.
  - Tapering avoids abrupt evaluation discontinuities between phases.

---

## 5. End-to-End Dataflow (One Search Node)

- What it is and where it lives:
  - Node recursion and make/undo cycle: [src/search.cpp](src/search.cpp#L230), [src/piecesMovement.cpp](src/piecesMovement.cpp#L161), [src/piecesMovement.cpp](src/piecesMovement.cpp#L298).

- How it works and the math:
  1. Search probes TT and pruning conditions.
  2. Generates legal moves and orders them.
  3. Applies move incrementally, including hash and eval cache updates.
  4. Recurses with negated window and sign.
  5. Undoes move exactly from snapshot.
  6. Updates alpha/beta and TT/killer metadata.

- Why and design decisions:
  - This loop is the core performance path. Any state inconsistency here corrupts both tactical strength and reproducibility.

---

## 6. Practical Notes Before Automated Tuning

- Evaluation parameters are centralized in [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L28), making weight tuning straightforward.
- The most tuning-sensitive formulas are the taper blend, passed pawn terms, king safety terms, and low-material scaling in [src/evaluatePosition.cpp](src/evaluatePosition.cpp#L1493).
- Search sensitivity is highest around move ordering, LMR/null-move settings, and aspiration window width in [src/search.cpp](src/search.cpp#L11).
- Move generation correctness can be regression-tested with perft in [src/generateAllMoves.cpp](src/generateAllMoves.cpp#L676).

This architecture is already organized in a way that supports staged automated tuning: parameter-only tuning first, then selective structural tuning where node growth is controlled by TT and ordering quality.
