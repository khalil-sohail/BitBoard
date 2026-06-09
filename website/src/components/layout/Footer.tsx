export function Footer() {
  return (
    <footer className="border-t border-border bg-surface px-6 py-6 mt-8 flex flex-col md:flex-row items-center justify-between gap-4 text-sm text-muted">
      <div className="flex items-center gap-2">
        <span>© 2026 Khalil Sohail</span>
        <span className="w-1 h-1 rounded-full bg-border inline-block"></span>
        <span>Bitboard-based C++ Chess Engine</span>
      </div>
      <div className="flex items-center gap-4">
        <span>Built with Next.js, Tailwind, and React Chessboard</span>
      </div>
    </footer>
  );
}
