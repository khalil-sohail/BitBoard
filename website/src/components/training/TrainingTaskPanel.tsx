import type { TrainingPresentation } from './training-presentation';
import styles from './TrainingSidebar.module.css';

export function TrainingTaskPanel({ presentation }: { presentation: TrainingPresentation }) {
  return (
    <section className={styles.taskPanel} data-tone={presentation.tone} aria-live="polite" aria-atomic="true">
      <p className={styles.sectionEyebrow}>Current task</p>
      <h3>{presentation.headline}</h3>
      <p>{presentation.instruction}</p>
    </section>
  );
}
