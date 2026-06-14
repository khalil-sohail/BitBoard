export function Badge({ children, variant = 'default', className = '' }: { children: React.ReactNode, variant?: 'default' | 'accent', className?: string }) {
  return (
    <span className={`inline-flex items-center px-2 py-0.5 rounded text-xs font-medium ${
      variant === 'accent' 
        ? 'bg-accent/20 text-accent border border-accent/30' 
        : 'bg-surface-elevated text-muted border border-border'
    } ${className}`}>
      {children}
    </span>
  );
}
