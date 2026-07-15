"use client";

import type { ReactNode } from 'react';
import { SessionControllerContext } from './SessionControllerContext';
import { useSessionControllerValue } from './useSessionControllerValue';

interface SessionControllerProviderProps {
  children: ReactNode;
}

export function SessionControllerProvider({ children }: SessionControllerProviderProps) {
  const controller = useSessionControllerValue();
  return (
    <SessionControllerContext.Provider value={controller}>
      {children}
    </SessionControllerContext.Provider>
  );
}
