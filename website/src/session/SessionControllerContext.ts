"use client";

import { createContext } from 'react';
import type { SessionController } from './session-controller.types';

export const SessionControllerContext = createContext<SessionController | null>(null);
