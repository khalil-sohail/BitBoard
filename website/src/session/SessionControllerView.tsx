"use client";

import { ProductAppShell } from '@/components/layout/ProductAppShell';
import { ChessBoardComponent } from '@/components/board/ChessBoard';
import { EvalBar } from '@/components/board/EvalBar';
import { FairPlaySidebar } from '@/components/fair-play/FairPlaySidebar';
import { TrainingSidebar } from '@/components/training/TrainingSidebar';
import { AnalysisSidebar } from '@/components/analysis/AnalysisSidebar';
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
            evaluation={(mode.isAnalysis ? session.analysis.snapshot?.lines[0]?.evaluation : engine.displayInfo?.pvs?.[0]?.evaluation) ?? null}
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
        ) : mode.isTraining ? (
          <TrainingSidebar
            lifecycle={lifecycle.status}
            gameOverMessage={lifecycle.gameOverMessage}
            state={training.state}
            connectionStatus={engine.status}
            currentTurn={training.currentTurn}
            playerColor={board.orientation}
            difficulty={setup.difficulty}
            analysisInfo={training.analysisInfo}
            hintView={training.hint.hintView}
            canRequestHint={training.canRequestHint}
            moves={history.moves}
            grades={history.grades}
            evaluationGraph={history.evaluationGraph}
            canUndo={actions.canUndo}
            canResign={actions.canResign}
            onRequestHint={training.hint.requestHint}
            onSetup={setup.open}
            onNewGame={actions.newGame}
            onUndo={actions.undo}
            onFlip={actions.flipBoard}
            onResign={actions.resign}
          />
        ) : (
          <AnalysisSidebar
            lifecycle={lifecycle.status}
            connectionStatus={engine.status}
            queuePosition={engine.queuePosition}
            paused={session.analysis.paused}
            source={session.analysis.source}
            positionTurn={session.analysis.positionTurn}
            positionFen={session.analysis.positionFen}
            snapshot={session.analysis.snapshot}
            engineInfo={session.analysis.displayInfo}
            moves={history.moves}
            cursorPly={session.analysis.cursorPly}
            historyLength={session.analysis.historyLength}
            depth={setup.maxDepth}
            multiPv={setup.multiPv}
            controlsDisabled={session.analysis.controlsUnavailable}
            onNavigate={session.analysis.navigate}
            onDepthChange={setup.changeMaxDepth}
            onMultiPvChange={setup.changeMultiPv}
            onSetup={setup.open}
            onStop={session.analysis.stop}
            onResume={session.analysis.resume}
            onReset={session.analysis.resetFen}
            onFlip={actions.flipBoard}
          />
        )}
      />

      <SessionSetupHost />
    </>
  );
}
