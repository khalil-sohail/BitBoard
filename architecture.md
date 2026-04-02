# Chess Engine Architecture Deep Dive

This document explains the engine architecture in detail, focused on board representation, move generation, search, and evaluation. Every concept block follows the same format:

- What it is and where it lives
- How it works and the math behind it
- Why it exists and why it is implemented this way

## 0. Module Map

- Core board API and state model: [include/board.hpp](include/board.hpp), with board state loading and board-level hash/repetition utilities in [src/Board.cpp](src/Board.cpp).
- Application layer (runtime mode selection, CLI loop, UCI loop, and shared text formatting): [include/app/app_options.hpp](include/app/app_options.hpp), [include/app/app_cli.hpp](include/app/app_cli.hpp), [include/app/app_uci.hpp](include/app/app_uci.hpp), [include/app/app_text.hpp](include/app/app_text.hpp), [src/app/app_options.cpp](src/app/app_options.cpp), [src/app/app_cli.cpp](src/app/app_cli.cpp), [src/app/app_uci.cpp](src/app/app_uci.cpp), [src/app/app_text.cpp](src/app/app_text.cpp), and thin entrypoint [src/main.cpp](src/main.cpp).
- Opening-book module and Polyglot decoding: [include/openingBook.hpp](include/openingBook.hpp) and [src/openingBook.cpp](src/openingBook.cpp).
- Move execution and board mutation pipeline: [src/move/board_move_make.cpp](src/move/board_move_make.cpp), [src/move/board_move_undo.cpp](src/move/board_move_undo.cpp), [src/move/board_move_parse.cpp](src/move/board_move_parse.cpp), [src/move/board_move_utils.cpp](src/move/board_move_utils.cpp), [include/move/move_hash.hpp](include/move/move_hash.hpp), [include/move/move_constants.hpp](include/move/move_constants.hpp), and [src/move/move_hash.cpp](src/move/move_hash.cpp).
- Move-generation subsystem: [include/move/movegen_constants.hpp](include/move/movegen_constants.hpp), [include/move/movegen_attacks.hpp](include/move/movegen_attacks.hpp), [src/move/board_movegen_state.cpp](src/move/board_movegen_state.cpp), [src/move/board_movegen_queries.cpp](src/move/board_movegen_queries.cpp), [src/move/board_movegen_attack_checks.cpp](src/move/board_movegen_attack_checks.cpp), [src/move/board_movegen_pseudo.cpp](src/move/board_movegen_pseudo.cpp), [src/move/board_movegen_captures.cpp](src/move/board_movegen_captures.cpp), and [src/move/board_movegen_legal_perft.cpp](src/move/board_movegen_legal_perft.cpp).
- Search public API and shared search data contracts: [include/search.hpp](include/search.hpp), [include/search/search_constants.hpp](include/search/search_constants.hpp), [include/search/search_types.hpp](include/search/search_types.hpp), and [include/search/search_internal.hpp](include/search/search_internal.hpp).
- Search implementation modules: [src/search/search_state.cpp](src/search/search_state.cpp), [src/search/search_move_ordering.cpp](src/search/search_move_ordering.cpp), [src/search/search_quiescence.cpp](src/search/search_quiescence.cpp), [src/search/search_negamax.cpp](src/search/search_negamax.cpp), and [src/search/search_root.cpp](src/search/search_root.cpp).
- Evaluation data contracts and split implementation: [include/eval/eval_types.hpp](include/eval/eval_types.hpp), [include/eval/eval_weights.hpp](include/eval/eval_weights.hpp), [include/eval/eval_tables.hpp](include/eval/eval_tables.hpp), [include/eval/eval_masks.hpp](include/eval/eval_masks.hpp), [include/eval/eval_terms.hpp](include/eval/eval_terms.hpp), [src/eval/eval_weights.cpp](src/eval/eval_weights.cpp), [src/eval/eval_tables.cpp](src/eval/eval_tables.cpp), [src/eval/eval_masks.cpp](src/eval/eval_masks.cpp), [src/eval/eval_terms_piece_activity.cpp](src/eval/eval_terms_piece_activity.cpp), [src/eval/eval_terms_pawns.cpp](src/eval/eval_terms_pawns.cpp), [src/eval/eval_terms_king.cpp](src/eval/eval_terms_king.cpp), [src/eval/eval_terms_endgame.cpp](src/eval/eval_terms_endgame.cpp), [src/eval/board_eval_incremental.cpp](src/eval/board_eval_incremental.cpp), [src/eval/board_eval_pipeline.cpp](src/eval/board_eval_pipeline.cpp), [src/eval/board_eval_entrypoints.cpp](src/eval/board_eval_entrypoints.cpp), and [src/eval/board_eval_diagnostics.cpp](src/eval/board_eval_diagnostics.cpp).
- Board/position presentation helpers: [src/printers.cpp](src/printers.cpp).

---

## 1. Board Representation

### 1.1 Concept: Bitboard State Model

- What it is and where it lives:
- State storage is a 2 x 6 bitboard tensor in [include/board.hpp](include/board.hpp).
- Color and piece enums are declared in [include/board.hpp](include/board.hpp).
- Initial piece placement and default game-state reset are implemented in [src/move/board_movegen_state.cpp](src/move/board_movegen_state.cpp).

- How it works and the math:
- Each 64-bit integer represents one piece set on 64 squares.
- Square indexing uses:
$$
\text{square} = 8 \cdot \text{rank} + \text{file}
$$
with file in [0, 7], rank in [0, 7].
- Occupancy is the bitwise union of per-piece bitboards, exposed by [src/move/board_movegen_queries.cpp](src/move/board_movegen_queries.cpp).

- Why it exists and why it is implemented this way:
- Bitboards enable branch-light bit math (shifts, masks, popcount) in move generation and evaluation.
- The 2 x 6 layout aligns with color and piece loops across search, eval, and legality code paths.

### 1.2 Concept: Board Utility Accessors

- What it is and where it lives:
- Occupancy and square query functions are in [src/move/board_movegen_queries.cpp](src/move/board_movegen_queries.cpp).
- Coordinate conversion and side-to-move accessors are in [src/move/board_move_utils.cpp](src/move/board_move_utils.cpp).

- How it works and the math:
- Square presence check uses mask test:
$$
\text{occupied}(s) \Leftrightarrow (\text{occ} \& (1 \ll s)) \neq 0
$$
- squareToString and squareFromString map between algebraic coordinates and internal square indices.

- Why it exists and why it is implemented this way:
- Utility accessors centralize fundamental conversions used by parser, UI, and tests.
- pieceAt remains linear over small fixed-size bitboard sets, which keeps the API simple and deterministic.

### 1.3 Concept: Undo State and Incremental State Cache

- What it is and where it lives:
- Undo snapshot structure is declared in [include/board.hpp](include/board.hpp).
- Move snapshot push is in [src/move/board_move_make.cpp](src/move/board_move_make.cpp).
- Undo restoration is in [src/move/board_move_undo.cpp](src/move/board_move_undo.cpp).
- Incremental MG/EG/phase cache maintenance helpers are in [src/eval/board_eval_incremental.cpp](src/eval/board_eval_incremental.cpp).

- How it works and the math:
- Before makeMove, full reversible state is pushed onto m_undoStack and hash onto m_hashHistory.
- undoMove restores bitboards, side, castling, en-passant, MG/EG scores, and phase in O(1).

- Why it exists and why it is implemented this way:
- Search requires very frequent make/undo cycles; O(1) restoration is mandatory for throughput.
- Snapshot-based restore is robust and avoids drift between board state and incremental caches.

### 1.4 Concept: Polyglot Zobrist Hashing and Repetition

- What it is and where it lives:
- Full position hash recomputation is in [src/Board.cpp](src/Board.cpp).
- Incremental hash primitives are in [include/move/move_hash.hpp](include/move/move_hash.hpp) and [src/move/move_hash.cpp](src/move/move_hash.cpp).
- Incremental hash updates are applied in [src/move/board_move_make.cpp](src/move/board_move_make.cpp) and [src/move/board_move_undo.cpp](src/move/board_move_undo.cpp).
- Repetition detection is implemented in [src/Board.cpp](src/Board.cpp).

- How it works and the math:
- Hash is XOR over piece-square occupancy keys, castling rights, en-passant file (conditionally), and side-to-move.
- Core identity:
$$
H = \bigoplus K_{piece,color,square} \oplus K_{castle} \oplus K_{ep} \oplus K_{stm}
$$
- makeMove toggles out prior state keys and toggles in successor state keys.

- Why it exists and why it is implemented this way:
- Zobrist hashing provides near-O(1) identity for transposition lookup and repetition checks.
- Polyglot-compatible hashing keeps opening-book interoperability intact.

### 1.5 Concept: Move Application Pipeline

- What it is and where it lives:
- Main mutation pipeline is in [src/move/board_move_make.cpp](src/move/board_move_make.cpp).
- Legal application wrapper and undo are in [src/move/board_move_undo.cpp](src/move/board_move_undo.cpp).
- SAN/coordinate parsing that feeds application is in [src/move/board_move_parse.cpp](src/move/board_move_parse.cpp).

- How it works and the math:
- The pipeline handles snapshot push, hash cleanup, piece movement, captures, en-passant capture, promotion replacement, rook motion for castling, rights update, en-passant target update, and side toggle.
- Incremental eval and hash deltas mirror board mutations exactly.

- Why it exists and why it is implemented this way:
- A single canonical make/undo path guarantees consistency between CLI/UCI, tests, and search recursion.
- Incremental state updates remove repeated full-board recomputation costs.

### 1.6 Concept: Null Move State Transition

- What it is and where it lives:
- Null move and undo-null-move are in [src/move/board_move_undo.cpp](src/move/board_move_undo.cpp).
- Null-move pruning call-site is in [src/search/search_negamax.cpp](src/search/search_negamax.cpp).

- How it works and the math:
- Null move clears en-passant, toggles side-to-move, and updates hash.
- Piece bitboards are unchanged, and undoNullMove restores side and en-passant hash component.

- Why it exists and why it is implemented this way:
- Null move pruning needs a cheap reversible pass operator.
- Keeping hash transitions exact is critical for TT correctness after null-move search branches.

---

## 2. Move Generation

### 2.1 Concept: Attack Primitives (Knight, King, Bishop, Rook) and Attack Queries

- What it is and where it lives:
- Primitive attack builders are in [include/move/movegen_attacks.hpp](include/move/movegen_attacks.hpp).
- Shared movegen constants are in [include/move/movegen_constants.hpp](include/move/movegen_constants.hpp).
- isSquareAttacked and inCheck are in [src/move/board_movegen_attack_checks.cpp](src/move/board_movegen_attack_checks.cpp).

- How it works and the math:
- Knight and king attacks use fixed shift-and-mask patterns.
- Sliding attacks ray-step until first blocker.
- Pop-LSB iteration uses:
$$
s = \operatorname{ctz}(bb),\quad bb \leftarrow bb \& (bb - 1)
$$

- Why it exists and why it is implemented this way:
- Shared primitive math avoids duplicated attack logic across pseudo, capture-only, and evaluation code.
- Explicit attack-query module keeps legality checks independent from move-list construction.

### 2.2 Concept: Pseudo-Legal Move Generation

- What it is and where it lives:
- Full pseudo-legal generator is in [src/move/board_movegen_pseudo.cpp](src/move/board_movegen_pseudo.cpp).

- How it works and the math:
- Builds moves by piece class using own occupancy, opponent occupancy, and combined occupancy.
- Pawns use directional shifts for push, capture, promotion, and en-passant candidates.
- Non-pawn pieces use attack masks intersected with not-own-occupancy.

- Why it exists and why it is implemented this way:
- Pseudo-legal-first keeps generation loops compact and isolates legality filtering to a separate pass.
- This structure supports both search and UI move-list consumers.

### 2.3 Concept: Pawn Rules (Push, Double Push, Capture, Promotion, En Passant)

- What it is and where it lives:
- Pawn logic for full pseudo lists is in [src/move/board_movegen_pseudo.cpp](src/move/board_movegen_pseudo.cpp).
- Pawn logic for capture-only lists is in [src/move/board_movegen_captures.cpp](src/move/board_movegen_captures.cpp).

- How it works and the math:
- White pawn motion uses left shifts, black uses right shifts.
- Promotion paths are rank-mask gated (RANK_8 for white, RANK_1 for black).
- Double-push requires two-square emptiness from starting ranks.
- En-passant candidates are generated by intersecting shifted pawn attack lanes with en-passant target mask.

- Why it exists and why it is implemented this way:
- Pawn irregularities are centralized to avoid leaking special cases into search logic.
- Separate full and capture-only generators keep quiescence branching under control.

### 2.4 Concept: Castling Rule Encoding

- What it is and where it lives:
- Castling generation checks are in [src/move/board_movegen_pseudo.cpp](src/move/board_movegen_pseudo.cpp).
- Castling right bit definitions are in [include/move/movegen_constants.hpp](include/move/movegen_constants.hpp) and [include/move/move_constants.hpp](include/move/move_constants.hpp).
- Castling-right updates and rook moves during makeMove are in [src/move/board_move_make.cpp](src/move/board_move_make.cpp).

- How it works and the math:
- Requires castling right bit, empty transit squares, and unattacked king path squares.
- Rights are stored as a 4-bit mask (WK, WQ, BK, BQ).

- Why it exists and why it is implemented this way:
- Bitmask rights are compact and easy to mutate on king/rook movement and rook capture squares.
- Separating generation checks from mutation updates keeps rule implementation auditable.

### 2.5 Concept: Capture-Only Move Generation for Quiescence

- What it is and where it lives:
- Capture-only generator is in [src/move/board_movegen_captures.cpp](src/move/board_movegen_captures.cpp).

- How it works and the math:
- Generates capture and promotion-capture moves, plus en-passant captures.
- Piece target sets are intersected directly with opponent occupancy.

- Why it exists and why it is implemented this way:
- Quiescence requires tactical continuations while pruning quiet-move explosion.
- Keeping this path separate from full pseudo generation avoids conditional overhead in hot loops.

### 2.6 Concept: Legal Move Filtering

- What it is and where it lives:
- Legal generator is in [src/move/board_movegen_legal_perft.cpp](src/move/board_movegen_legal_perft.cpp).

- How it works and the math:
- For each pseudo move, clone board, apply move, reject if the moving side remains in check.

- Why it exists and why it is implemented this way:
- This is simple and correctness-first.
- It provides a reliable legal move source for search, parser validation, and perft.

### 2.7 Concept: Perft Verification

- What it is and where it lives:
- Perft recursion is in [src/move/board_movegen_legal_perft.cpp](src/move/board_movegen_legal_perft.cpp).

- How it works and the math:
- Standard node-count recursion over legal moves with depth base cases.

- Why it exists and why it is implemented this way:
- Perft is the canonical regression oracle for move-generation correctness.

---

## 3. Search Architecture

### 3.1 Concept: Search Constants, Types, and Runtime State

- What it is and where it lives:
- Core numeric constants are in [include/search/search_constants.hpp](include/search/search_constants.hpp).
- TT entry and flags are in [include/search/search_types.hpp](include/search/search_types.hpp).
- Internal shared declarations are in [include/search/search_internal.hpp](include/search/search_internal.hpp).
- Runtime state, timers, node counters, TT storage, and killer arrays are in [src/search/search_state.cpp](src/search/search_state.cpp).

- How it works and the math:
- Values are centipawn-scale bounds plus pruning and aspiration parameters.
- TIME_CHECK_MASK triggers periodic time checks every 2048 nodes.

- Why it exists and why it is implemented this way:
- Constants and mutable runtime state are split so tuning data and search state lifecycle remain explicit.
- Shared internal contracts keep root, negamax, quiescence, and ordering modules synchronized.

### 3.2 Concept: Move Ordering (MVV-LVA, TT Move, Killer Moves)

- What it is and where it lives:
- Capture scoring, move scoring, and stable sorting are in [src/search/search_move_ordering.cpp](src/search/search_move_ordering.cpp).
- Killer table storage is in [src/search/search_state.cpp](src/search/search_state.cpp).
- Killer updates are applied in [src/search/search_negamax.cpp](src/search/search_negamax.cpp).

- How it works and the math:
- Capture score uses MVV-LVA table from [include/search/search_constants.hpp](include/search/search_constants.hpp).
- TT best move is given fixed top score.
- Killer quiets are floated to front at same ply after fail-high cutoffs.

- Why it exists and why it is implemented this way:
- Ordering quality directly improves alpha-beta cutoff rate.
- Separating ordering logic into its own module avoids cluttering recursive search code.

### 3.3 Concept: Time Management and Cooperative Abort

- What it is and where it lives:
- checkTime and global time-abort flag are in [src/search/search_state.cpp](src/search/search_state.cpp).
- Node-gated periodic abort checks are in SearchInternal::shouldAbortSearch in [src/search/search_state.cpp](src/search/search_state.cpp).
- Root time-budget setup is in [src/search/search_root.cpp](src/search/search_root.cpp), and UCI time parsing is in [src/app/app_uci.cpp](src/app/app_uci.cpp).

- How it works and the math:
- Search checks wall-clock at periodic node intervals.
- Root search receives allocatedTimeMs and exits cooperatively when timeAborted flips.

- Why it exists and why it is implemented this way:
- Cooperative abort keeps recursion logic simple while preserving responsiveness under hard time limits.
- Root and UCI layers stay decoupled: UCI computes policy, root consumes budget.

### 3.4 Concept: Quiescence Search and Delta Pruning

- What it is and where it lives:
- Quiescence routine is in [src/search/search_quiescence.cpp](src/search/search_quiescence.cpp).
- Capture move source is [src/move/board_movegen_captures.cpp](src/move/board_movegen_captures.cpp).
- Stand-pat evaluation entrypoint is [src/eval/board_eval_entrypoints.cpp](src/eval/board_eval_entrypoints.cpp).

- How it works and the math:
- Uses stand-pat static score when not in check.
- Searches tactical continuation set (captures, or full legal if in check).
- Delta pruning applies:
$$
\text{if } standPat + capturedValue + margin < \alpha,\ \text{skip move}
$$

- Why it exists and why it is implemented this way:
- Reduces horizon effect by extending only tactically volatile leaves.
- Direct coupling to capture-only movegen keeps the leaf frontier compact.

### 3.5 Concept: Negamax Core with Alpha-Beta

- What it is and where it lives:
- Main recursion is in [src/search/search_negamax.cpp](src/search/search_negamax.cpp).
- Public function declarations are in [include/search.hpp](include/search.hpp).

- How it works and the math:
- Uses negamax identity:
$$
V(s,d,\alpha,\beta)=\max_{m \in Legal(s)}\left[-V(s_m,d-1,-\beta,-\alpha)\right]
$$
- Applies repetition draw detection, mate-distance normalization, TT probing, null move pruning, LMR, and check extensions.
- At depth 0, transitions to quiescence; static eval path is Board::evaluateSideToMove in [src/eval/board_eval_entrypoints.cpp](src/eval/board_eval_entrypoints.cpp), which calls the split bonus pipeline in [src/eval/board_eval_pipeline.cpp](src/eval/board_eval_pipeline.cpp).

- Why it exists and why it is implemented this way:
- Negamax keeps minimax symmetric and compact.
- Split recursion and leaf-eval modules improve readability while preserving hot-path behavior.

### 3.6 Concept: Transposition Table (Direct-Mapped)

- What it is and where it lives:
- TT storage and lifecycle helpers are in [src/search/search_state.cpp](src/search/search_state.cpp).
- TT entry schema is in [include/search/search_types.hpp](include/search/search_types.hpp).
- Probe and store policy are in [src/search/search_negamax.cpp](src/search/search_negamax.cpp).

- How it works and the math:
- Indexing is modulo hash over fixed-size TT vector.
- Uses Exact, Alpha, and Beta node flags.
- Replacement keeps newer/deeper or matching-hash entries.

- Why it exists and why it is implemented this way:
- Avoids re-searching transposed states and feeds best-move ordering.
- Direct-mapped table is simple and cache friendly.

### 3.7 Concept: Null Move Pruning

- What it is and where it lives:
- Null move condition and reduced null-window search are in [src/search/search_negamax.cpp](src/search/search_negamax.cpp).
- Null move state transitions are in [src/move/board_move_undo.cpp](src/move/board_move_undo.cpp).

- How it works and the math:
- If depth and position conditions hold, engine executes null move and searches reduced depth.
- If null score fails high against beta, branch is cut.

- Why it exists and why it is implemented this way:
- Captures the null-move observation to reduce search in clearly superior branches.
- Board module provides exact reversible null transitions for safe integration.

### 3.8 Concept: Late Move Reduction and Check Extension

- What it is and where it lives:
- LMR and check-extension logic are in [src/search/search_negamax.cpp](src/search/search_negamax.cpp).

- How it works and the math:
- Late quiet moves at sufficient depth are searched at reduced depth first.
- If reduced result is in-window, a full-depth re-search is triggered.
- Moves that give check receive extension.

- Why it exists and why it is implemented this way:
- Reallocates compute budget from low-value quiet tails to tactical/critical lines.
- Maintains tactical reliability in forcing branches.

### 3.9 Concept: Iterative Deepening and Aspiration Windows

- What it is and where it lives:
- Root driver is in [src/search/search_root.cpp](src/search/search_root.cpp).
- Aspiration constants are in [include/search/search_constants.hpp](include/search/search_constants.hpp).

- How it works and the math:
- Root searches depth 1 to maxDepth while preserving best completed move.
- Uses aspiration window around previous score at deeper iterations and falls back to full window on fail-low or fail-high.
- Emits UCI info lines consumed by the UCI runtime in [src/app/app_uci.cpp](src/app/app_uci.cpp).

- Why it exists and why it is implemented this way:
- Iterative deepening provides stable best-so-far output under time pressure.
- Aspiration windows reduce node count when score drift is small.

---

## 4. Evaluation Architecture

### 4.1 Concept: EvalWeights Namespace and Material Anchors

- What it is and where it lives:
- Weight declarations are in [include/eval/eval_weights.hpp](include/eval/eval_weights.hpp).
- Weight definitions are in [src/eval/eval_weights.cpp](src/eval/eval_weights.cpp).

- How it works and the math:
- Piece base values, phase increments, and term coefficients are centralized.
- Phase accumulation supports MG/EG blend weighting.

- Why it exists and why it is implemented this way:
- Centralized constants support targeted tuning without touching feature logic.
- Clear separation of parameters from algorithms improves maintainability.

### 4.2 Concept: Piece-Square Tables and Color Mirroring

- What it is and where it lives:
- PST declarations and pieceScoreDelta interface are in [include/eval/eval_tables.hpp](include/eval/eval_tables.hpp).
- PST definitions and pieceScoreDelta implementation are in [src/eval/eval_tables.cpp](src/eval/eval_tables.cpp).
- Mirror/file/lsb helpers are in [include/eval/eval_types.hpp](include/eval/eval_types.hpp).

- How it works and the math:
- Black uses mirrored square indices so one orientation serves both colors.
- Mapping:
$$
idx = \begin{cases}
 s & \text{white} \\
 s \oplus 56 & \text{black}
\end{cases}
$$

- Why it exists and why it is implemented this way:
- Prevents duplicate PST tables and reduces tuning surface area.
- Encapsulates piece-square and phase deltas behind one reusable function.

### 4.3 Concept: Incremental Piece Delta Cache

- What it is and where it lives:
- Incremental add/remove and reset-from-board cache rebuild are in [src/eval/board_eval_incremental.cpp](src/eval/board_eval_incremental.cpp).
- Incremental hooks are called during move application in [src/move/board_move_make.cpp](src/move/board_move_make.cpp).

- How it works and the math:
- Every piece add/remove applies MG, EG, and phase delta from pieceScoreDelta.
- Full cache rebuild path exists for reset/FEN load safety.

- Why it exists and why it is implemented this way:
- Keeps per-node evaluation updates O(1) per changed piece, critical for search speed.
- Preserves exact consistency between move application and evaluator state.

### 4.4 Concept: Evaluation Entry Points

- What it is and where it lives:
- evaluate, evaluateSideToMove, and static recompute are in [src/eval/board_eval_entrypoints.cpp](src/eval/board_eval_entrypoints.cpp).
- Aggregate bonus/taper pipeline is in [src/eval/board_eval_pipeline.cpp](src/eval/board_eval_pipeline.cpp).
- Diagnostic breakdown tooling is in [src/eval/board_eval_diagnostics.cpp](src/eval/board_eval_diagnostics.cpp).

- How it works and the math:
- evaluate starts from incremental MG/EG caches, then applies feature terms and taper.
- evaluateSideToMove flips sign by side to match negamax convention.

- Why it exists and why it is implemented this way:
- Entry points remain small and stable while feature growth stays in specialized modules.
- Diagnostics can evolve independently from search-critical fast paths.

### 4.5 Concept: Mobility and Activity Features

- What it is and where it lives:
- Leaper/slider attack helpers, mobility terms, rook activity, and development-pattern penalties are in [src/eval/eval_terms_piece_activity.cpp](src/eval/eval_terms_piece_activity.cpp).

- How it works and the math:
- Mobility uses popcount of attack maps masked by not-own-occupancy.
- Rook file/seventh-rank activity is encoded with MG/EG term weights.

- Why it exists and why it is implemented this way:
- Mobility and activity are high-signal positional features with low computational overhead.
- Grouping related piece-activity terms in one module simplifies targeted tuning.

### 4.6 Concept: Pawn Structure and Pawn-Feature Stack

- What it is and where it lives:
- Connected pawns, candidate passers, backward pawns, islands, doubled, and isolated penalties are in [src/eval/eval_terms_pawns.cpp](src/eval/eval_terms_pawns.cpp).
- Shared file/passed/shield masks are declared in [include/eval/eval_masks.hpp](include/eval/eval_masks.hpp) and instantiated in [src/eval/eval_masks.cpp](src/eval/eval_masks.cpp).

- How it works and the math:
- Rank-weighted tables map advancement to MG/EG bonuses or penalties.
- File occupancy segments count pawn islands.
- Doubled and isolated penalties are computed from per-file occupancy and adjacency tests.

- Why it exists and why it is implemented this way:
- Pawn structure is long-horizon and strongly predictive in both middlegame plans and endgame conversion.
- Dedicated pawn module isolates dense rule logic from the core evaluator pipeline.

### 4.7 Concept: Passed Pawn Scoring

- What it is and where it lives:
- Passed-pawn counting and advancement bonus are in [src/eval/eval_terms_pawns.cpp](src/eval/eval_terms_pawns.cpp).
- Passed-pawn coefficients are in [src/eval/eval_weights.cpp](src/eval/eval_weights.cpp).

- How it works and the math:
- Count bonus rewards number of passed pawns.
- Advancement uses quadratic growth by relative rank:
$$
bonus \propto (relativeRank)^2
$$
- Blocked forward square attenuates bonus.

- Why it exists and why it is implemented this way:
- Separates strategic quantity effect from concrete advancement pressure.
- Improves endgame conversion behavior without expensive forward search extensions.

### 4.8 Concept: King Safety Features

- What it is and where it lives:
- King attack pressure and pawn shield terms are in [src/eval/eval_terms_king.cpp](src/eval/eval_terms_king.cpp).
- King-zone and shield masks are generated in [include/eval/eval_masks.hpp](include/eval/eval_masks.hpp).

- How it works and the math:
- Enemy attacks into king-zone squares are counted and mapped to nonlinear penalties.
- Shield bonus counts nearby friendly pawns up to capped contribution.

- Why it exists and why it is implemented this way:
- Captures king vulnerability patterns in static eval without full tactical search.
- Mask precomputation keeps per-node overhead low.

### 4.9 Concept: Development and Piece-Placement Heuristics

- What it is and where it lives:
- Trapped rook, bad bishop, early queen development, and uncastled-king penalties are in [src/eval/eval_terms_piece_activity.cpp](src/eval/eval_terms_piece_activity.cpp).

- How it works and the math:
- Sparse pattern detectors apply handcrafted penalties.
- These are additive terms blended into MG/EG totals.

- Why it exists and why it is implemented this way:
- Patches known strategic blind spots quickly and transparently.
- Keeps heuristic patterns modular and testable.

### 4.10 Concept: Endgame Draw Logic, Mop-Up, and Scale Factors

- What it is and where it lives:
- Insufficient-material draw detection, opposite-bishop scaling, low-material scaling, and mop-up bonus are in [src/eval/eval_terms_endgame.cpp](src/eval/eval_terms_endgame.cpp).
- Endgame scale constants are in [src/eval/eval_weights.cpp](src/eval/eval_weights.cpp).

- How it works and the math:
- Dead-material detection can return forced draw outcome.
- Mop-up bonus models edge/corner king restriction and king approach.
- Low-material scale attenuates final score in draw-prone reduced material classes.

- Why it exists and why it is implemented this way:
- Prevents over-optimistic conversion in theoretically drawish endings.
- Improves practical king-driving technique in clearly winning reduced-material endings.

### 4.11 Concept: Unified Scoring Pipeline and Tapering

- What it is and where it lives:
- Unified bonus aggregation and taper blend are in [src/eval/board_eval_pipeline.cpp](src/eval/board_eval_pipeline.cpp).
- Entrypoint handoff is in [src/eval/board_eval_entrypoints.cpp](src/eval/board_eval_entrypoints.cpp).

- How it works and the math:
- Starts from incremental MG/EG base and adds all feature deltas.
- Phase blend:
$$
score_{tapered}=\frac{MG \cdot phase + EG \cdot (24-phase)}{24}
$$
- Applies no-pawn attenuation and low-material scale factor.

- Why it exists and why it is implemented this way:
- Dual-channel MG/EG representation captures phase-dependent value shifts smoothly.
- Central pipeline keeps all feature interactions visible and consistent.

---

## 5. End-to-End Dataflow (One Search Node)

- What it is and where it lives:
- Root iterative deepening is in [src/search/search_root.cpp](src/search/search_root.cpp).
- Recursive node expansion is in [src/search/search_negamax.cpp](src/search/search_negamax.cpp) and [src/search/search_quiescence.cpp](src/search/search_quiescence.cpp).
- Move apply/undo cycle is in [src/move/board_move_make.cpp](src/move/board_move_make.cpp) and [src/move/board_move_undo.cpp](src/move/board_move_undo.cpp).
- Static eval path is in [src/eval/board_eval_entrypoints.cpp](src/eval/board_eval_entrypoints.cpp) and [src/eval/board_eval_pipeline.cpp](src/eval/board_eval_pipeline.cpp).

- How it works and the math:
- Root chooses depth and bounds, then calls negamax.
- negamax probes TT, applies pruning, generates and orders moves, then recurses with sign-flipped window.
- makeMove applies board, hash, and incremental-eval deltas; undoMove restores snapshot.
- At leaf depth, quiescence stabilizes tactical frontier and uses evaluateSideToMove.

- Why it exists and why it is implemented this way:
- This loop is the throughput-critical engine core.
- Strict make/undo and eval/hash coherence are required for tactical correctness and reproducibility.

---

## 6. Practical Notes Before Automated Tuning

- Weight tuning surface is centralized in [include/eval/eval_weights.hpp](include/eval/eval_weights.hpp) and [src/eval/eval_weights.cpp](src/eval/eval_weights.cpp).
- Feature-level tuning is isolated across [src/eval/eval_terms_piece_activity.cpp](src/eval/eval_terms_piece_activity.cpp), [src/eval/eval_terms_pawns.cpp](src/eval/eval_terms_pawns.cpp), [src/eval/eval_terms_king.cpp](src/eval/eval_terms_king.cpp), and [src/eval/eval_terms_endgame.cpp](src/eval/eval_terms_endgame.cpp).
- Search sensitivity is highest around [include/search/search_constants.hpp](include/search/search_constants.hpp), [src/search/search_move_ordering.cpp](src/search/search_move_ordering.cpp), [src/search/search_negamax.cpp](src/search/search_negamax.cpp), and [src/search/search_root.cpp](src/search/search_root.cpp).
- Move-generation correctness regression should use perft in [src/move/board_movegen_legal_perft.cpp](src/move/board_movegen_legal_perft.cpp).

This architecture is now fully modular across board state, movegen, move execution, search, eval, and app runtime layers, which enables parameter tuning, feature ablation, and subsystem-level profiling without monolithic-file friction.
