import type { GradedMove, MoveGrade } from '@/types/grades';
import styles from './TrainingSidebar.module.css';

export function TrainingCompletedSummary({ result, grades, plyCount }: { result: string; grades: GradedMove[]; plyCount: number }) {
  const counts = grades.reduce<Partial<Record<MoveGrade, number>>>((summary, grade) => {
    summary[grade.grade] = (summary[grade.grade] ?? 0) + 1;
    return summary;
  }, {});
  return (
    <section className={styles.completedSummary} role="status" aria-live="assertive">
      <p className={styles.sectionEyebrow}>Training complete</p>
      <h2>{result}</h2>
      <p>{grades.length} reviewed move{grades.length === 1 ? '' : 's'} across {Math.ceil(plyCount / 2)} full move{Math.ceil(plyCount / 2) === 1 ? '' : 's'}.</p>
      <dl>
        {(['Best', 'Good', 'Inaccuracy', 'Mistake', 'Blunder'] as const).map(grade => (
          <div key={grade}><dt>{grade}</dt><dd>{counts[grade] ?? 0}</dd></div>
        ))}
      </dl>
    </section>
  );
}
