export type MoveGrade =
  | 'Book'
  | 'Forced'
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
