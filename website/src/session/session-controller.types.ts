import type { useSessionControllerValue } from './useSessionControllerValue';

export type SessionController = ReturnType<typeof useSessionControllerValue>;

export type SessionLifecycle = SessionController['lifecycle'];
export type SessionModeDomain = SessionController['mode'];
export type SessionBoardDomain = SessionController['board'];
export type SessionEngineDomain = SessionController['engine'];
export type SessionClockDomain = SessionController['clocks'];
export type SessionSetupDomain = SessionController['setup'];
export type SessionTrainingDomain = SessionController['training'];
export type SessionAnalysisDomain = SessionController['analysis'];
export type SessionActionDomain = SessionController['actions'];
