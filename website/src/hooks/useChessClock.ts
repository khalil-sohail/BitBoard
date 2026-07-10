import { useState, useRef, useCallback, useEffect } from 'react';
import type { Chess } from 'chess.js';
import { PlayerColor } from '../types/engine';

export interface ClockState {
  whiteMs: number;
  blackMs: number;
  /** Which side's clock is currently ticking, or null if paused. */
  activeSide: PlayerColor | null;
  isRunning: boolean;
}

export interface UseChessClockReturn extends ClockState {
  /** Start (or resume) the given side's clock. */
  startClock: (side: PlayerColor) => void;
  /** Pause both clocks without resetting. */
  stopClock: () => void;
  /** Add the increment to the given side's remaining time. */
  applyIncrement: (side: PlayerColor) => void;
  /** Reset both clocks to initial values and stop. */
  resetClock: (config?: ResetClockConfig) => void;
}

interface ClockConfig {
  initialWhiteMs: number;
  initialBlackMs: number;
  whiteIncMs: number;
  blackIncMs: number;
  onTimeout?: (color: PlayerColor) => void;
}

interface ResetClockConfig {
  initialWhiteMs: number;
  initialBlackMs: number;
}

const TICK_MS = 100; // update interval

export function useChessClock({
  initialWhiteMs,
  initialBlackMs,
  whiteIncMs,
  blackIncMs,
  onTimeout,
}: ClockConfig, game?: Pick<Chess, 'turn'>): UseChessClockReturn {
  const [whiteMs, setWhiteMs] = useState(initialWhiteMs);
  const gameRef = useRef(game);
  const [blackMs, setBlackMs] = useState(initialBlackMs);
  const [activeSide, setActiveSide] = useState<PlayerColor | null>(null);
  const [isRunning, setIsRunning] = useState(false);

  // We hold mutable refs for values that are read inside the interval callback
  // to avoid stale-closure issues.
  const activeSideRef  = useRef<PlayerColor | null>(null);
  const whiteMsRef     = useRef(initialWhiteMs);
  const blackMsRef     = useRef(initialBlackMs);
  const intervalRef    = useRef<ReturnType<typeof setInterval> | null>(null);
  const warningTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const lastTickRef    = useRef<number>(0);

  useEffect(() => {
    gameRef.current = game;
  }, [game]);

  useEffect(() => {
    whiteMsRef.current = whiteMs;
  }, [whiteMs]);

  useEffect(() => {
    blackMsRef.current = blackMs;
  }, [blackMs]);

  const clearTick = useCallback(() => {
    if (intervalRef.current !== null) {
      clearInterval(intervalRef.current);
      intervalRef.current = null;
    }
  }, []);

  const stopClock = useCallback(() => {
    clearTick();
    activeSideRef.current = null;
    setActiveSide(null);
    setIsRunning(false);
  }, [clearTick]);

  const startTick = useCallback(() => {
    clearTick();
    lastTickRef.current = Date.now();
    intervalRef.current = setInterval(() => {
      const now = Date.now();
      const elapsed = now - lastTickRef.current;
      lastTickRef.current = now;

      if (activeSideRef.current === 'w') {
        const previousMs = whiteMsRef.current;
        const newMs = Math.max(0, previousMs - elapsed);
        whiteMsRef.current = newMs;
        setWhiteMs(newMs);
        if (newMs <= 0 && previousMs > 0) {
          clearTick();
          activeSideRef.current = null;
          setActiveSide(null);
          setIsRunning(false);
          onTimeout?.('w');
        }
      } else if (activeSideRef.current === 'b') {
        const previousMs = blackMsRef.current;
        const newMs = Math.max(0, previousMs - elapsed);
        blackMsRef.current = newMs;
        setBlackMs(newMs);
        if (newMs <= 0 && previousMs > 0) {
          clearTick();
          activeSideRef.current = null;
          setActiveSide(null);
          setIsRunning(false);
          onTimeout?.('b');
        }
      }
    }, TICK_MS);
  }, [clearTick, onTimeout]);

  const startClock = useCallback((side: PlayerColor) => {
    if (gameRef.current && side !== gameRef.current.turn()) {
      if (process.env.NODE_ENV !== 'production') {
        if (warningTimeoutRef.current !== null) {
          clearTimeout(warningTimeoutRef.current);
        }
        warningTimeoutRef.current = setTimeout(() => {
          warningTimeoutRef.current = null;
          if (gameRef.current && side !== gameRef.current.turn()) {
            console.warn(`[Clock Sync Warning] activeSide is ${side} but turn is ${gameRef.current.turn()}`);
          }
        }, 0);
      }
    }
    activeSideRef.current = side;
    setActiveSide(side);
    setIsRunning(true);
    startTick();
  }, [startTick]);

  const applyIncrement = useCallback((side: PlayerColor) => {
    if (side === 'w') {
      const inc = whiteIncMs;
      setWhiteMs(prev => {
        const next = prev + inc;
        whiteMsRef.current = next;
        return next;
      });
    } else {
      const inc = blackIncMs;
      setBlackMs(prev => {
        const next = prev + inc;
        blackMsRef.current = next;
        return next;
      });
    }
  }, [whiteIncMs, blackIncMs]);

  const resetClock = useCallback((config?: ResetClockConfig) => {
    const nextWhiteMs = config?.initialWhiteMs ?? initialWhiteMs;
    const nextBlackMs = config?.initialBlackMs ?? initialBlackMs;

    clearTick();
    activeSideRef.current = null;
    setActiveSide(null);
    setIsRunning(false);
    setWhiteMs(nextWhiteMs);
    setBlackMs(nextBlackMs);
    whiteMsRef.current = nextWhiteMs;
    blackMsRef.current = nextBlackMs;
  }, [clearTick, initialWhiteMs, initialBlackMs]);

  // Cleanup on unmount
  useEffect(() => () => {
    clearTick();
    if (warningTimeoutRef.current !== null) {
      clearTimeout(warningTimeoutRef.current);
      warningTimeoutRef.current = null;
    }
  }, [clearTick]);

  return {
    whiteMs,
    blackMs,
    activeSide,
    isRunning,
    startClock,
    stopClock,
    applyIncrement,
    resetClock,
  };
}
