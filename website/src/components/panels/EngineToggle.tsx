"use client";

import { Badge } from '../ui/Badge';
import { useToast } from '../ui/Toast';
import { DifficultyLevel, GameMode } from '../../types/engine';
import { DIFFICULTY_OPTIONS } from '../../lib/engine-difficulty';

interface EngineToggleProps {
  currentVersion?: string;
  maxDepth: number;
  onDepthChange: (depth: number) => void;
  difficulty: DifficultyLevel;
  onDifficultyChange: (difficulty: DifficultyLevel) => void;
  multiPv: number;
  onMultiPvChange: (lines: number) => void;
  gameMode: GameMode;
  ownBook: boolean;
  optionsDisabled: boolean;
  onOwnBookChange: (enabled: boolean) => void;
  trainingPonderEnabled: boolean;
  onTrainingPonderChange: (enabled: boolean) => void;
}

export function EngineToggle({
  currentVersion = "Texel-Tuned HCE",
  maxDepth,
  onDepthChange,
  difficulty,
  onDifficultyChange,
  multiPv,
  onMultiPvChange,
  gameMode,
  ownBook,
  optionsDisabled,
  onOwnBookChange,
  trainingPonderEnabled,
  onTrainingPonderChange,
}: EngineToggleProps) {
  const { addToast } = useToast();

  const handleNNUEClick = () => {
    addToast('NNUE evaluation is currently under development.', 'info');
  };

  const handleWASMClick = () => {
    addToast('Server-side analysis only — WASM build coming soon', 'info');
  };

  const handleToggleBook = () => {
    onOwnBookChange(!ownBook);
  };

  const handleToggleTrainingPonder = () => {
    onTrainingPonderChange(!trainingPonderEnabled);
  };

  const handleDepthChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const val = parseInt(e.target.value, 10);
    onDepthChange(val);
  };

  const depthLabel = gameMode === 'training' ? 'Review Depth' : 'Analysis Depth';

  return (
    <div className="grid grid-cols-[5fr_3fr] gap-6 bg-surface-elevated border border-white/10 rounded-xl p-5 shadow-md relative">
      
      {/* Vertical Divider using border on the first column */}
      {/* ── ENGINE CONFIGURATION ─────────────────────────────────────────── */}
      <div className="flex flex-col gap-5 border-r border-white/5 pr-6">
        <h3 className="text-xs font-bold text-foreground uppercase tracking-wider">
          Engine Configuration
        </h3>
        
        {/* Row 1: Evaluation Method */}
        <div className="flex flex-col gap-2">
          <label className="text-sm font-medium text-muted-foreground whitespace-nowrap">
            Evaluation Method
          </label>
          <div className="flex items-center bg-background border border-white/5 rounded-lg p-1 w-full">
            <button className="flex-1 px-2 py-1.5 bg-surface-elevated text-foreground text-xs rounded-md shadow-sm font-semibold whitespace-nowrap">
              {currentVersion}
            </button>
            <button 
              onClick={handleNNUEClick}
              className="flex-1 px-2 py-1.5 text-muted-foreground/50 hover:text-muted-foreground transition-colors text-xs flex items-center justify-center gap-1.5 font-semibold whitespace-nowrap"
            >
              NNUE <Badge variant="accent" className="text-[9px] px-1 py-0 h-4">Planned</Badge>
            </button>
          </div>
        </div>

        {/* Row 2: Execution Environment */}
        <div className="flex flex-col gap-2">
          <label className="text-sm font-medium text-muted-foreground whitespace-nowrap">
            Execution Environment
          </label>
          <div className="flex items-center bg-background border border-white/5 rounded-lg p-1 w-full">
            <button className="flex-1 px-2 py-1.5 bg-surface-elevated text-foreground text-xs rounded-md shadow-sm font-semibold whitespace-nowrap">
              Server C++
            </button>
            <button 
              onClick={handleWASMClick}
              className="flex-1 px-2 py-1.5 text-muted-foreground/50 hover:text-muted-foreground transition-colors text-xs flex items-center justify-center gap-1.5 font-semibold whitespace-nowrap"
            >
              Browser WASM <Badge variant="accent" className="text-[9px] px-1 py-0 h-4">Planned</Badge>
            </button>
          </div>
        </div>
      </div>

      {/* ── SECOND COLUMN ────────────────────────────────────────────────── */}
      <div className="flex flex-col gap-5">
        
        {/* Book Settings */}
        <div className="flex flex-col gap-3">
          <h3 className="text-xs font-bold text-foreground uppercase tracking-wider">
            Engine Settings
          </h3>
          <div className="flex items-center justify-between">
            <label className="text-sm font-medium text-muted-foreground whitespace-nowrap">
              Use Openings
            </label>
            <button
              onClick={handleToggleBook}
              disabled={optionsDisabled}
              className={`relative inline-flex h-6 w-11 flex-shrink-0 items-center rounded-full transition-colors duration-200 ease-in-out focus:outline-none disabled:cursor-not-allowed disabled:opacity-50 ${ownBook ? 'bg-primary' : 'bg-white/10'}`}
            >
              <span className={`inline-block h-4 w-4 transform rounded-full bg-white transition duration-200 ease-in-out ${ownBook ? 'translate-x-6' : 'translate-x-1'}`} />
            </button>
          </div>
        </div>

        {/* Search Settings */}
        <div
          className={`flex flex-col gap-3 transition-opacity duration-200 ${
            gameMode === 'fair' ? 'opacity-40 pointer-events-none' : 'opacity-100'
          }`}
        >
          <div className="flex flex-col gap-1">
            {/* Max Depth Slider Controls */}
            <div className="flex w-full items-center justify-between gap-3 mb-2">
              <label className="text-sm font-medium text-muted-foreground whitespace-nowrap">
                {depthLabel}
              </label>
              
              <input
                type="range"
                min="2"
                max="30"
                step="1"
                value={maxDepth}
                onChange={handleDepthChange}
                className="w-full h-1.5 bg-white/10 rounded-lg appearance-none cursor-pointer accent-primary"
              />
              
              <div className="flex flex-col items-end min-w-[36px]">
                <span className="text-sm font-bold text-accent leading-none">
                  {maxDepth}
                </span>
                <span className="text-[10px] font-semibold text-accent/60 uppercase tracking-widest mt-1">
                  ply
                </span>
              </div>
            </div>

            {/* MultiPV Line Selector Buttons */}
            <div className="flex w-full items-center bg-background border border-white/5 rounded-lg p-1 mt-1">
              {[1, 2, 3].map((val) => (
                <button
                  key={val}
                  onClick={() => onMultiPvChange(val)}
                  className={`flex-1 px-2 py-1.5 text-xs rounded-md shadow-sm font-semibold whitespace-nowrap transition-colors ${
                    multiPv === val
                      ? 'bg-primary text-primary-foreground'
                      : 'bg-transparent text-muted-foreground hover:bg-white/5 hover:text-foreground'
                  }`}
                >
                  {val} {val === 1 ? 'Line' : 'Lines'}
                </button>
              ))}
            </div>

            {gameMode === 'training' && (
              <>
                <div className="flex w-full items-center bg-background border border-white/5 rounded-lg p-1 mt-3">
                  {DIFFICULTY_OPTIONS.map((opt) => (
                    <button
                      key={opt.id}
                      onClick={() => onDifficultyChange(opt.id)}
                      className={`flex-1 px-2 py-1.5 text-xs rounded-md shadow-sm font-semibold whitespace-nowrap transition-colors ${
                        difficulty === opt.id
                          ? 'bg-primary text-primary-foreground'
                          : 'bg-transparent text-muted-foreground hover:bg-white/5 hover:text-foreground'
                      }`}
                      title={`Opponent: ${opt.sublabel}`}
                    >
                      {opt.label}
                    </button>
                  ))}
                </div>
                <div className="flex items-start justify-between gap-3 mt-3">
                  <div>
                    <label className="text-sm font-medium text-muted-foreground whitespace-nowrap">
                      Pondering
                    </label>
                    <p className="text-[10px] text-muted/60 mt-0.5">
                      Allow the Training opponent to think during your turn.
                    </p>
                  </div>
                  <button
                    onClick={handleToggleTrainingPonder}
                    disabled={optionsDisabled}
                    className={`relative inline-flex h-6 w-11 flex-shrink-0 items-center rounded-full transition-colors duration-200 ease-in-out focus:outline-none disabled:cursor-not-allowed disabled:opacity-50 ${trainingPonderEnabled ? 'bg-primary' : 'bg-white/10'}`}
                  >
                    <span className={`inline-block h-4 w-4 transform rounded-full bg-white transition duration-200 ease-in-out ${trainingPonderEnabled ? 'translate-x-6' : 'translate-x-1'}`} />
                  </button>
                </div>
              </>
            )}
          </div>
        </div>

      </div>

    </div>
  );
}
