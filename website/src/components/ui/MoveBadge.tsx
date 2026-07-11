import type { MoveGrade } from '../../types/grades';

interface MoveBadgeProps {
  grade: MoveGrade;
  /** Display as an icon-only pill (no text). Defaults to false. */
  compact?: boolean;
}

const GRADE_CONFIG: Record<MoveGrade, { label: string; icon: string; className: string }> = {
  Book: {
    label: 'Book',
    icon: '📖',
    className: 'bg-sky-500/10 text-sky-400 border border-sky-500/20',
  },
  Forced: {
    label: 'Forced',
    icon: '!',
    className: 'bg-zinc-500/10 text-zinc-300 border border-zinc-500/20',
  },
  Best: {
    label: 'Best',
    icon: '★',
    className: 'bg-emerald-500/10 text-emerald-400 border border-emerald-500/20',
  },
  Good: {
    label: 'Good',
    icon: '●',
    className: 'bg-green-500/10 text-green-400 border border-green-500/20',
  },
  Inaccuracy: {
    label: '?!',
    icon: '?!',
    className: 'bg-yellow-500/10 text-yellow-400 border border-yellow-500/20',
  },
  Mistake: {
    label: '?',
    icon: '?',
    className: 'bg-orange-500/10 text-orange-400 border border-orange-500/20',
  },
  Blunder: {
    label: '??',
    icon: '??',
    className: 'bg-red-500/10 text-red-400 border border-red-500/20',
  },
};

export function MoveBadge({ grade, compact = false }: MoveBadgeProps) {
  const config = GRADE_CONFIG[grade];

  // For "Good" in compact mode, skip rendering — clean board
  if (compact && grade === 'Good') return null;

  return (
    <span
      className={`
        inline-flex items-center justify-center shrink-0
        rounded-full font-bold leading-none
        ${compact ? 'w-5 h-5 text-[10px]' : 'px-1.5 py-0.5 text-[10px] gap-0.5'}
        ${config.className}
        transition-all duration-150
      `}
      title={compact ? config.label : undefined}
    >
      {compact ? (
        config.icon
      ) : (
        <>
          <span>{config.icon}</span>
          <span>{config.label}</span>
        </>
      )}
    </span>
  );
}
