export function Header() {
  return (
    <header className="border-b border-border bg-surface px-6 py-4 flex justify-between items-center sticky top-0 z-10">
      <div className="flex items-center gap-3">
        <div className="w-8 h-8 bg-primary rounded shadow-[0_0_15px_rgba(34,197,94,0.3)] flex items-center justify-center">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" className="w-5 h-5 text-primary-foreground stroke-[2.5] stroke-linecap-round stroke-linejoin-round">
            <path d="M12 2l3 6 7 1-5 5 1.5 7-6.5-3.5L5.5 21 7 14l-5-5 7-1 3-6z" />
          </svg>
        </div>
        <h1 className="text-xl font-bold tracking-tight text-foreground uppercase">
          1337<span className="text-primary">AI</span>
        </h1>
      </div>
      <nav className="flex gap-4">
        <a href="https://github.com/khalil-sohail" target="_blank" rel="noreferrer" className="text-sm font-medium text-muted hover:text-foreground transition-colors">
          GitHub
        </a>
        <a href="#" className="text-sm font-medium text-muted hover:text-foreground transition-colors">
          Architecture
        </a>
      </nav>
    </header>
  );
}
