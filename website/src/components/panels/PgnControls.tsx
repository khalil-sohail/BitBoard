import React, { useState } from 'react';
import { useToast } from '../ui/Toast';

interface PgnControlsProps {
  exportPgn: () => string;
  loadPgn: (pgn: string) => string | false;
  onImportSuccess?: (fen: string) => void;
}

export function PgnControls({ exportPgn, loadPgn, onImportSuccess }: PgnControlsProps) {
  const [pgnInput, setPgnInput] = useState('');
  const { addToast } = useToast();

  const handleCopy = () => {
    const pgn = exportPgn();
    if (!pgn || pgn.trim() === '') {
      addToast('No moves to copy.', 'warning');
      return;
    }
    navigator.clipboard.writeText(pgn).then(() => {
      addToast('PGN copied to clipboard!', 'info');
    }).catch(() => {
      addToast('Failed to copy PGN.', 'error');
    });
  };

  const handleImport = () => {
    if (!pgnInput.trim()) return;
    const finalFen = loadPgn(pgnInput);
    if (finalFen === false) {
      addToast('Invalid PGN format.', 'error');
    } else {
      setPgnInput('');
      addToast('PGN loaded successfully.', 'info');
      if (onImportSuccess) {
        onImportSuccess(finalFen);
      }
    }
  };

  return (
    <div className="bg-surface rounded-lg border border-white/10 p-3 shadow-md mt-auto">
      <div className="flex flex-row items-center gap-3">
        <input 
          type="text" 
          value={pgnInput}
          onChange={(e) => setPgnInput(e.target.value)}
          placeholder="Paste PGN here..."
          className="flex-1 bg-background border border-border rounded-md px-3 py-1.5 text-sm text-foreground placeholder:text-muted focus:outline-none focus:border-primary transition-colors min-w-0"
        />
        <button 
          onClick={handleImport}
          disabled={!pgnInput.trim()}
          className="shrink-0 px-3 py-1.5 bg-surface-elevated hover:bg-surface-elevated/80 border border-border rounded-md text-sm font-medium transition-colors disabled:opacity-50 disabled:cursor-not-allowed"
        >
          Import
        </button>
        <button 
          onClick={handleCopy}
          className="shrink-0 px-3 py-1.5 bg-primary/10 hover:bg-primary/20 text-primary border border-primary/20 rounded-md text-sm font-medium transition-colors"
        >
          Copy PGN
        </button>
      </div>
    </div>
  );
}
