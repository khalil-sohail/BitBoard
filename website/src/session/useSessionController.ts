"use client";

import { useContext } from 'react';
import { SessionControllerContext } from './SessionControllerContext';

export function useSessionController() {
  const controller = useContext(SessionControllerContext);
  if (controller === null) {
    throw new Error('useSessionController must be used within a SessionControllerProvider');
  }
  return controller;
}
