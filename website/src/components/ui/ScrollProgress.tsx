"use client";

import { useEffect, useState } from 'react';

export function ScrollProgress() {
  const [progress, setProgress] = useState(0);

  useEffect(() => {
    let frame: number | null = null;
    const updateProgress = () => {
      frame = null;
      const scrollHeight = document.documentElement.scrollHeight - window.innerHeight;
      if (scrollHeight > 0) {
        const scrolled = (window.scrollY / scrollHeight) * 100;
        setProgress(Math.min(Math.max(scrolled, 0), 100));
      } else {
        setProgress(0);
      }
    };
    const handleScroll = () => {
      if (frame === null) frame = requestAnimationFrame(updateProgress);
    };

    window.addEventListener('scroll', handleScroll, { passive: true });
    updateProgress();

    return () => {
      window.removeEventListener('scroll', handleScroll);
      if (frame !== null) cancelAnimationFrame(frame);
    };
  }, []);

  return (
    <div aria-hidden="true" className="fixed top-0 right-0 w-[6px] h-full bg-white/5 z-50 pointer-events-none">
      <div
        className="fixed top-0 right-0 w-[6px] bg-gradient-to-b from-primary to-accent z-50 pointer-events-none"
        style={{
          height: `${progress}%`,
          opacity: Math.max(0.3, progress / 100)
        }}
      />
    </div>
  );
}
