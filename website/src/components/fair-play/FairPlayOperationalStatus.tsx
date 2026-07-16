import type { FairPlayStatusPresentation } from './fair-play-presentation';
import { OperationalStatus } from '../live-data/OperationalStatus';

export function FairPlayOperationalStatus({ status }: { status: FairPlayStatusPresentation }) {
  if (status.state === 'idle' || status.state === 'completed') return null;
  if (status.state === 'active' && (status.headline === 'Your turn' || status.headline === 'Engine thinking' || status.headline === 'Waiting for engine')) return null;
  const isError = status.tone === 'error';
  return <OperationalStatus title={status.headline} description={status.detail} tone={isError ? 'error' : status.tone === 'waiting' ? 'loading' : 'working'} />;
}
