"use client";

import { SessionControllerProvider } from '@/session/SessionControllerProvider';
import { SessionControllerView } from '@/session/SessionControllerView';

export default function Home() {
  return (
    <SessionControllerProvider>
      <SessionControllerView />
    </SessionControllerProvider>
  );
}
