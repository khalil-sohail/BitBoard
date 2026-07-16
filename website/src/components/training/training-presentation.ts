import type { ConnectionStatus } from '../../hooks/useEngine';
import type { TrainingState } from '../../lib/training-machine';
import type { PlayerColor } from '../../types/engine';

export type TrainingSidebarState =
  | 'idle'
  | 'starting'
  | 'player-turn'
  | 'reviewing'
  | 'feedback-ready'
  | 'opponent-thinking'
  | 'applying-opponent-move'
  | 'completed'
  | 'error';

export interface TrainingPresentation {
  state: TrainingSidebarState;
  headline: string;
  instruction: string;
  operationalLabel: string;
  tone: 'normal' | 'working' | 'success' | 'error';
}

export function deriveTrainingPresentation(input: {
  lifecycle: 'idle' | 'active' | 'completed';
  trainingState: TrainingState;
  connectionStatus: ConnectionStatus;
  currentTurn: PlayerColor;
  playerColor: PlayerColor;
  hasLatestFeedback: boolean;
}): TrainingPresentation {
  if (input.lifecycle === 'idle' || input.trainingState.status === 'inactive') {
    return {
      state: 'idle', headline: 'Build better decisions',
      instruction: 'Configure a guided game when you are ready.',
      operationalLabel: 'Training is ready to set up', tone: 'normal',
    };
  }

  if (input.lifecycle === 'completed' || input.trainingState.status === 'game-over') {
    return {
      state: 'completed', headline: 'Training complete',
      instruction: 'Review the graded moves and start another game when ready.',
      operationalLabel: 'Session complete', tone: 'success',
    };
  }

  if (input.trainingState.status === 'engine-error') {
    return {
      state: 'error', headline: 'Training paused', instruction: input.trainingState.message,
      operationalLabel: 'Engine error', tone: 'error',
    };
  }

  if (input.trainingState.status === 'connection-lost' || input.connectionStatus === 'disconnected' || input.connectionStatus === 'session_expired' || input.connectionStatus === 'error') {
    return {
      state: 'error', headline: 'Connection interrupted',
      instruction: 'Your position is preserved while the engine session recovers.',
      operationalLabel: input.connectionStatus === 'connecting' ? 'Reconnecting…' : 'Engine unavailable', tone: 'error',
    };
  }

  if (input.trainingState.status === 'initializing' || input.trainingState.status === 'resetting' || input.connectionStatus === 'connecting' || input.connectionStatus === 'queued') {
    return {
      state: 'starting', headline: 'Preparing training',
      instruction: 'The engine session is getting the position ready.',
      operationalLabel: input.connectionStatus === 'queued' ? 'Waiting for an engine session…' : 'Connecting…', tone: 'working',
    };
  }

  if (input.trainingState.status === 'promotion-pending') {
    return {
      state: 'player-turn', headline: 'Choose a promotion piece',
      instruction: 'Complete your move on the board to continue the review.',
      operationalLabel: 'Waiting for your promotion choice', tone: 'normal',
    };
  }

  if (input.trainingState.status === 'reviewing-player-move') {
    return {
      state: 'reviewing', headline: 'Reviewing your move',
      instruction: 'Comparing your decision with the engine’s preferred continuation.',
      operationalLabel: 'Move review in progress…', tone: 'working',
    };
  }

  if (input.trainingState.status === 'showing-feedback') {
    return {
      state: 'feedback-ready', headline: 'Feedback ready',
      instruction: 'Use the latest review below before the engine replies.',
      operationalLabel: 'Move review complete', tone: 'success',
    };
  }

  if (input.trainingState.status === 'waiting-engine-move') {
    if (input.connectionStatus === 'result_ready') {
      return {
        state: 'applying-opponent-move', headline: 'Applying the engine move',
        instruction: 'The validated reply is being placed on the board.',
        operationalLabel: 'Applying engine move…', tone: 'working',
      };
    }
    return {
      state: 'opponent-thinking', headline: input.hasLatestFeedback ? 'Review the feedback' : 'Engine preparing a reply',
      instruction: input.hasLatestFeedback ? 'Your latest review remains available while the engine chooses its move.' : 'Wait for the engine reply.',
      operationalLabel: 'Engine choosing a reply…', tone: 'working',
    };
  }

  const isPlayerTurn = input.currentTurn === input.playerColor;
  return {
    state: 'player-turn', headline: isPlayerTurn ? 'Find your best move' : 'Waiting for the board',
    instruction: isPlayerTurn ? 'Study the position, make a move, or request a hint if you are stuck.' : 'The engine is preparing the next position.',
    operationalLabel: isPlayerTurn ? 'Your turn' : 'Engine turn', tone: 'normal',
  };
}
