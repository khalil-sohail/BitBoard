export type MoveGrade =
  | 'Book'
  | 'Brilliant'
  | 'Best'
  | 'Good'
  | 'Inaccuracy'
  | 'Mistake'
  | 'Blunder';

export interface GradedMove {
  moveIndex: number; // 0-based index into the full move history
  grade: MoveGrade;
  delta: number; // cp change from the mover's perspective (+good, -bad)
}

/** Determine a MoveGrade from the centipawn delta (from the mover's perspective). */
export function gradeFromDelta(delta: number, isBook: boolean, isBrilliant: boolean): MoveGrade {
  if (isBook) return 'Book';
  if (isBrilliant) return 'Brilliant';
  if (delta <= -300) return 'Blunder';
  if (delta <= -150) return 'Mistake';
  if (delta <= -80) return 'Inaccuracy';
  if (delta >= 200) return 'Best';
  return 'Good';
}
