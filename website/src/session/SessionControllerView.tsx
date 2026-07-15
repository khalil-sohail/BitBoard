"use client";

import { ProductAppShell } from '@/components/layout/ProductAppShell';
import { ChessBoardComponent } from '@/components/board/ChessBoard';
import { EvalBar } from '@/components/board/EvalBar';
import { EnginePanel } from '@/components/panels/EnginePanel';
import { MoveHistory } from '@/components/panels/MoveHistory';
import { EngineToggle } from '@/components/panels/EngineToggle';
import { PositionSetup } from '@/components/panels/PositionSetup';
import { EvalGraph } from '@/components/panels/EvalGraph';
import { ClockDisplay } from '@/components/panels/ClockDisplay';
import { TrainingHintPanel } from '@/components/panels/TrainingHintPanel';
import { GameControls } from '@/components/controls/GameControls';
import { NewGameModal } from '@/components/ui/NewGameModal';
import { useSessionController } from './useSessionController';

export function SessionControllerView() {
  const session = useSessionController();
  const { mode, lifecycle, board, engine, clocks, setup, training, analysis, history, actions } = session;

  return (
    <>
      <ProductAppShell
        mode={mode.value}
        onModeChange={mode.change}
        sessionActive={lifecycle.isActive}
        evaluationBar={engine.showEvaluation ? (
          <EvalBar
            evaluation={engine.displayInfo?.pvs?.[0]?.evaluation ?? null}
            orientation={board.orientation}
          />
        ) : undefined}
        board={(
          <>
            <ChessBoardComponent
              key={board.promotionResetKey}
              fen={board.fen}
              arrows={engine.showPanel ? board.arrows : []}
              onMove={board.onMove}
              onPromotionPending={board.onPromotionPending}
              onPromotionSelected={board.onPromotionSelected}
              onPromotionCancelled={board.onPromotionCancelled}
              disabled={board.disabled}
              orientation={board.orientation === 'w' ? 'white' : 'black'}
              checkSquare={board.checkSquare}
              lastMove={board.lastMove}
              hintView={board.trainingHintView}
            />

            {clocks.show && (
              <div className="absolute top-2 right-2 z-10 pointer-events-none">
                <span className="bg-black/60 text-muted text-[9px] font-bold font-mono tracking-wider px-2 py-0.5 rounded-full backdrop-blur-sm">
                  ⏱ {clocks.timeControl.label}
                </span>
              </div>
            )}

            {lifecycle.status === 'idle' && (
              <div className="absolute inset-0 bg-background/50 backdrop-blur-sm z-50 flex flex-col items-center justify-center rounded-md gap-4">
                <div className="text-center">
                  <h2 className="text-2xl font-bold text-foreground mb-1">Ready to Play?</h2>
                  <p className="text-sm text-muted">Start the engine when you are ready.</p>
                </div>
                {mode.isAnalysis ? (
                  <button
                    onClick={analysis.start}
                    className="px-8 py-3 bg-primary hover:bg-primary/90 text-primary-foreground rounded-xl font-bold text-base shadow-lg shadow-primary/30 transition-all duration-150 active:scale-[0.97]"
                  >
                    Start Analysis →
                  </button>
                ) : (
                  <button
                    onClick={setup.open}
                    className="px-8 py-3 bg-primary hover:bg-primary/90 text-primary-foreground rounded-xl font-bold text-base shadow-lg shadow-primary/30 transition-all duration-150 active:scale-[0.97]"
                  >
                    Setup Match →
                  </button>
                )}
              </div>
            )}

            {lifecycle.isComplete && !mode.isAnalysis && (
              <div className="absolute inset-0 bg-background/70 backdrop-blur-sm z-50 flex flex-col items-center justify-center rounded-md">
                <h2 className="text-3xl font-bold text-foreground mb-2">Game Over</h2>
                <p className="text-lg text-muted-foreground mb-6 font-medium">{lifecycle.gameOverMessage}</p>
                <button
                  onClick={actions.newGame}
                  className="px-6 py-2 bg-primary hover:bg-primary/90 text-primary-foreground rounded-md font-semibold transition-colors"
                >
                  Play Again
                </button>
              </div>
            )}
          </>
        )}
        sidebar={(
          <>
            {clocks.show && (
              <ClockDisplay
                whiteMs={clocks.value.whiteMs}
                blackMs={clocks.value.blackMs}
                activeSide={clocks.value.activeSide}
                playerColor={board.orientation}
                isRunning={clocks.value.isRunning}
                isGameActive={lifecycle.isActive}
                fen={board.fen}
                disabled={false}
              />
            )}

            {mode.isAnalysis && (
              <PositionSetup
                currentFen={board.fen}
                onLoadFen={analysis.loadFen}
                onReset={analysis.resetFen}
                exportPgn={analysis.exportPgn}
                loadPgn={analysis.loadPgn}
                onImportSuccess={analysis.importSucceeded}
              />
            )}

            {engine.showConfiguration && (
              <EngineToggle
                currentVersion="Texel-Tuned HCE"
                maxDepth={setup.maxDepth}
                onDepthChange={setup.changeMaxDepth}
                difficulty={setup.difficulty}
                onDifficultyChange={setup.changeDifficulty}
                multiPv={setup.multiPv}
                onMultiPvChange={setup.changeMultiPv}
                gameMode={mode.value}
                ownBook={setup.ownBook}
                optionsDisabled={engine.optionsUnavailable}
                onOwnBookChange={setup.changeOwnBook}
                trainingPonderEnabled={setup.trainingPonderEnabled}
                onTrainingPonderChange={setup.changeTrainingPonder}
              />
            )}

            {mode.isTraining && (
              <TrainingHintPanel
                hint={training.state.status === 'waiting-player' ? training.state.hint : undefined}
                hintView={training.hint.hintView}
                canRequest={training.canRequestHint}
                onRequest={training.hint.requestHint}
              />
            )}

            {engine.showPanel && (
              <EnginePanel info={engine.displayInfo} status={engine.status} queuePosition={engine.queuePosition} />
            )}

            <MoveHistory moves={history.moves} grades={history.grades} showGrades={engine.showPanel} />

            {engine.showPanel && <EvalGraph data={history.evaluationGraph} />}

            <div className="shrink-0">
              <GameControls
                onNewGameClick={actions.newGame}
                onUndo={actions.undo}
                onFlipBoard={actions.flipBoard}
                onResign={actions.resign}
                orientation={board.orientation}
                canUndo={actions.canUndo}
                canResign={actions.canResign}
              />
            </div>
          </>
        )}
      />

      <NewGameModal
        isOpen={setup.isOpen}
        gameMode={mode.value}
        defaultDifficulty={setup.difficulty}
        defaultPlayerColor={board.orientation}
        defaultTimeControl={setup.timeControl}
        defaultMaxDepth={setup.maxDepth}
        onStart={setup.start}
        onCancel={setup.close}
      />
    </>
  );
}
