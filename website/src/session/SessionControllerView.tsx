"use client";

import { ProductAppShell } from '@/components/layout/ProductAppShell';
import { ChessBoardComponent } from '@/components/board/ChessBoard';
import { EvalBar } from '@/components/board/EvalBar';
import { EnginePanel } from '@/components/panels/EnginePanel';
import { MoveHistory } from '@/components/panels/MoveHistory';
import { AnalysisSearchControls } from '@/components/panels/AnalysisSearchControls';
import { EvalGraph } from '@/components/panels/EvalGraph';
import { TrainingHintPanel } from '@/components/panels/TrainingHintPanel';
import { GameControls } from '@/components/controls/GameControls';
import { FairPlaySidebar } from '@/components/fair-play/FairPlaySidebar';
import { SessionSetupHost } from '@/components/setup/SessionSetupHost';
import { useSessionController } from './useSessionController';

export function SessionControllerView() {
  const session = useSessionController();
  const { mode, lifecycle, board, engine, clocks, setup, training, history, actions } = session;

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
              <div className="absolute inset-x-3 bottom-3 z-40 rounded-xl border border-white/10 bg-surface/95 p-3 shadow-xl backdrop-blur-md sm:inset-x-auto sm:bottom-5 sm:left-1/2 sm:w-80 sm:-translate-x-1/2">
                <div className="flex items-center justify-between gap-3">
                  <div className="min-w-0">
                    <h2 className="text-sm font-bold text-foreground">{mode.isAnalysis ? 'Analysis' : mode.isTraining ? 'Training' : 'Fair Play'}</h2>
                    <p className="truncate text-xs text-muted">Configure the session when you are ready.</p>
                  </div>
                  <button
                    onClick={setup.open}
                    className="shrink-0 rounded-lg bg-primary px-4 py-2.5 text-xs font-bold text-primary-foreground shadow-lg shadow-primary/20"
                  >
                    Set up
                  </button>
                </div>
              </div>
            )}

            {lifecycle.isComplete && mode.isTraining && (
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
        sidebar={mode.value === 'fair' ? (
          <FairPlaySidebar
            lifecycle={lifecycle.status}
            gameOverMessage={lifecycle.gameOverMessage}
            connectionStatus={session.fairPlay.connectionStatus}
            queuePosition={session.fairPlay.queuePosition}
            searchRetryCount={session.fairPlay.searchRetryCount}
            waitingForSessionReady={session.fairPlay.waitingForSessionReady}
            currentTurn={session.fairPlay.currentTurn}
            playerColor={board.orientation}
            whiteMs={clocks.value.whiteMs}
            blackMs={clocks.value.blackMs}
            activeClock={clocks.value.activeSide}
            clockRunning={clocks.value.isRunning}
            timeControlDisabled={clocks.timeControl.initialMs === 0}
            moves={history.moves}
            canResign={actions.canResign}
            onSetup={setup.open}
            onNewGame={actions.newGame}
            onFlipBoard={actions.flipBoard}
            onResign={actions.resign}
          />
        ) : lifecycle.status === 'idle' ? null : (
          <>
            {mode.isAnalysis && (
              <AnalysisSearchControls
                depth={setup.maxDepth}
                multiPv={setup.multiPv}
                disabled={engine.optionsUnavailable}
                onDepthChange={setup.changeMaxDepth}
                onMultiPvChange={setup.changeMultiPv}
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

      <SessionSetupHost />
    </>
  );
}
