import styles from './ProductLayout.module.css';

export function ProductFooter() {
  return (
    <footer className={`${styles.footer} border-t border-border/50 bg-surface px-6 py-3 flex items-center justify-center text-xs text-muted/60`}>
      <span>Bitboard — C++23 chess engine with real-time WebSocket analysis</span>
    </footer>
  );
}
