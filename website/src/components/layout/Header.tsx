export function Header() {
  return (
    <header className="border-b border-border bg-surface px-6 py-4 flex justify-between items-center sticky top-0 z-10">
      <div className="flex items-center gap-3">
        <h1 className="text-xl font-bold tracking-tight text-foreground uppercase">
          CHESS<span className="text-primary"> ENGINE</span>
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
