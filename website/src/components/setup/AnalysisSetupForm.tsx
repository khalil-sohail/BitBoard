"use client";

import { useRef, useState } from 'react';
import type { SessionController } from '@/session/session-controller.types';
import { AdvancedSettings } from './AdvancedSettings';
import { RangeControl, ToggleControl } from './SetupControls';
import { SetupSection } from './SetupSection';
import { SessionSetupDialog } from './SessionSetupDialog';
import { validateFen, validatePgn } from './analysis-setup-validation';

export type AnalysisSource = 'default' | 'fen' | 'pgn';

export function AnalysisSetupForm({ session }: { session: SessionController }) {
  const { setup, analysis } = session;
  const [source, setSource] = useState<AnalysisSource>('default');
  const [fen, setFen] = useState('');
  const [pgn, setPgn] = useState('');
  const [sourceError, setSourceError] = useState<string | null>(null);
  const [depth, setDepth] = useState(setup.maxDepth);
  const [multiPv, setMultiPv] = useState(setup.multiPv);
  const [ownBook, setOwnBook] = useState(setup.ownBook);
  const fenRef = useRef<HTMLTextAreaElement>(null);
  const pgnRef = useRef<HTMLTextAreaElement>(null);

  const submit = () => {
    const error = source === 'fen' ? validateFen(fen) : source === 'pgn' ? validatePgn(pgn) : null;
    if (error) {
      setSourceError(error);
      (source === 'fen' ? fenRef.current : pgnRef.current)?.focus();
      return;
    }

    setup.changeMaxDepth(depth);
    setup.changeMultiPv(multiPv);
    setup.changeOwnBook(ownBook);
    const accepted = source === 'fen'
      ? analysis.startFromFen(fen.trim())
      : source === 'pgn'
        ? analysis.startFromPgn(pgn)
        : analysis.startFromDefault();
    if (!accepted) {
      setSourceError(source === 'pgn' ? 'The PGN could not be loaded.' : 'The position could not be loaded.');
      (source === 'fen' ? fenRef.current : pgnRef.current)?.focus();
    }
  };

  return (
    <SessionSetupDialog
      modeLabel="Analysis"
      description="Choose a source position and initial search settings, then load it into the persistent analysis session."
      submitLabel="Load & Analyze"
      onCancel={setup.close}
      onSubmit={submit}
    >
      <div className="space-y-4">
        <SetupSection title="Source position" description="Start from the normal initial position, a FEN, or a PGN mainline.">
          <fieldset>
            <legend className="sr-only">Analysis source</legend>
            <div className="grid grid-cols-3 gap-2">
              {(['default', 'fen', 'pgn'] as AnalysisSource[]).map(option => (
                <button
                  key={option}
                  type="button"
                  data-setup-autofocus={option === 'default' ? '' : undefined}
                  aria-pressed={source === option}
                  onClick={() => { setSource(option); setSourceError(null); }}
                  className={`min-h-12 rounded-lg border text-xs font-bold uppercase tracking-wider ${source === option ? 'border-primary/50 bg-primary/15 text-primary' : 'border-white/10 bg-background text-muted hover:text-foreground'}`}
                >
                  {option === 'default' ? 'Starting position' : option.toUpperCase()}
                </button>
              ))}
            </div>
          </fieldset>

          {source === 'fen' && (
            <div className="mt-4">
              <label htmlFor="analysis-fen" className="mb-2 block text-xs font-semibold text-muted">FEN</label>
              <textarea ref={fenRef} id="analysis-fen" value={fen} onChange={event => { setFen(event.target.value.replace(/\n/g, '')); setSourceError(null); }} aria-invalid={Boolean(sourceError)} aria-describedby={sourceError ? 'analysis-source-error' : undefined} className="min-h-28 w-full resize-y rounded-lg border border-white/10 bg-background p-3 font-mono text-xs text-foreground outline-none focus:border-primary/60" placeholder="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" />
            </div>
          )}

          {source === 'pgn' && (
            <div className="mt-4">
              <label htmlFor="analysis-pgn" className="mb-2 block text-xs font-semibold text-muted">PGN</label>
              <textarea ref={pgnRef} id="analysis-pgn" value={pgn} onChange={event => { setPgn(event.target.value); setSourceError(null); }} aria-invalid={Boolean(sourceError)} aria-describedby={sourceError ? 'analysis-source-error' : undefined} className="min-h-40 w-full resize-y rounded-lg border border-white/10 bg-background p-3 font-mono text-xs text-foreground outline-none focus:border-primary/60" placeholder="Paste a PGN mainline…" />
            </div>
          )}

          {sourceError && <p id="analysis-source-error" role="alert" className="mt-2 text-xs text-red-400">{sourceError}</p>}
        </SetupSection>

        <SetupSection title="Initial search" description="Depth and line count remain adjustable during active Analysis.">
          <div className="space-y-5">
            <RangeControl id="analysis-setup-depth" label="Analysis depth" value={depth} min={2} max={30} onChange={setDepth} />
            <RangeControl id="analysis-setup-multipv" label="Principal variations" value={multiPv} min={1} max={3} onChange={setMultiPv} />
          </div>
        </SetupSection>

        <AdvancedSettings defaultExpanded={!ownBook}>
          <ToggleControl label="Opening book" description="Allow book resolution before normal analysis search when applicable." checked={ownBook} onChange={setOwnBook} />
        </AdvancedSettings>
      </div>
    </SessionSetupDialog>
  );
}
