import { useState, useRef, useCallback, useEffect } from 'react';
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
  resetClock: () => void;
}

interface ClockConfig {
  initialWhiteMs: number;
  initialBlackMs: number;
  whiteIncMs: number;
  blackIncMs: number;
}

const TICK_MS = 100; // update interval

export function useChessClock({
  initialWhiteMs,
  initialBlackMs,
  whiteIncMs,
  blackIncMs,
}: ClockConfig): UseChessClockReturn {
  const [whiteMs, setWhiteMs] = useState(initialWhiteMs);
  const [blackMs, setBlackMs] = useState(initialBlackMs);
  const [activeSide, setActiveSide] = useState<PlayerColor | null>(null);
  const [isRunning, setIsRunning] = useState(false);

  // We hold mutable refs for values that are read inside the interval callback
  // to avoid stale-closure issues.
  const activeSideRef  = useRef<PlayerColor | null>(null);
  const whiteMsRef     = useRef(initialWhiteMs);
  const blackMsRef     = useRef(initialBlackMs);
  const intervalRef    = useRef<ReturnType<typeof setInterval> | null>(null);
  const lastTickRef    = useRef<number>(Date.now());

  // Keep refs in sync with state
  whiteMsRef.current = whiteMs;
  blackMsRef.current = blackMs;

  const clearTick = useCallback(() => {
    if (intervalRef.current !== null) {
      clearInterval(intervalRef.current);
      intervalRef.current = null;
    }
  }, []);

  const startTick = useCallback(() => {
    clearTick();
    lastTickRef.current = Date.now();
    intervalRef.current = setInterval(() => {
      const now = Date.now();
      const elapsed = now - lastTickRef.current;
      lastTickRef.current = now;

      if (activeSideRef.current === 'w') {
        setWhiteMs(prev => Math.max(0, prev - elapsed));
      } else if (activeSideRef.current === 'b') {
        setBlackMs(prev => Math.max(0, prev - elapsed));
      }
    }, TICK_MS);
  }, [clearTick]);

  const startClock = useCallback((side: PlayerColor) => {
    activeSideRef.current = side;
    setActiveSide(side);
    setIsRunning(true);
    startTick();
  }, [startTick]);

  const stopClock = useCallback(() => {
    clearTick();
    activeSideRef.current = null;
    setActiveSide(null);
    setIsRunning(false);
  }, [clearTick]);

  const applyIncrement = useCallback((side: PlayerColor) => {
    if (side === 'w') {
      const inc = whiteIncMs;
      setWhiteMs(prev => prev + inc);
    } else {
      const inc = blackIncMs;
      setBlackMs(prev => prev + inc);
    }
  }, [whiteIncMs, blackIncMs]);

  const resetClock = useCallback(() => {
    clearTick();
    activeSideRef.current = null;
    setActiveSide(null);
    setIsRunning(false);
    setWhiteMs(initialWhiteMs);
    setBlackMs(initialBlackMs);
    whiteMsRef.current = initialWhiteMs;
    blackMsRef.current = initialBlackMs;
  }, [clearTick, initialWhiteMs, initialBlackMs]);

  // Reinitialise when the time control config changes (new game with different TC)
  useEffect(() => {
    resetClock();
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [initialWhiteMs, initialBlackMs, whiteIncMs, blackIncMs]);

  // Cleanup on unmount
  useEffect(() => () => clearTick(), [clearTick]);

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
