"use client";

import { useMemo } from 'react';
import { formatEvaluation } from './evaluation-format';
import { EmptyState, LoadingState } from './EmptyState';
import { formatPrincipalVariation } from './move-notation';
import type { PrincipalVariationView } from './live-data.types';
import { sortPrincipalVariations } from './pv-model';
import styles from './LiveData.module.css';

export function PrincipalVariationList({ lines, rootFen, requestId, requestedLines, loading = false, emptyText = 'No principal variation is available.' }: {
  lines: readonly PrincipalVariationView[]; rootFen?: string; requestId?: number; requestedLines?: number; loading?: boolean; emptyText?: string;
}) {
  const formatted = useMemo(() => sortPrincipalVariations(lines).map(line => ({
    ...line, formatted: formatPrincipalVariation(rootFen, line.moves), evaluationText: formatEvaluation(line.evaluation),
  })), [lines, rootFen]);
  if (!formatted.length) return <div data-live-pv>{loading ? <LoadingState>{emptyText}</LoadingState> : <EmptyState>{emptyText}</EmptyState>}</div>;
  return <div data-live-pv>
    {requestedLines !== undefined ? <p className={styles.lineCount}>{formatted.length}/{requestedLines} lines</p> : null}
    <ol className={styles.pvList}>{formatted.map(line => <li key={`${requestId ?? 0}:${line.rank}`} data-primary={line.rank === 1} data-notation={line.formatted.notation}>
      <div className={styles.pvHeader}><strong>Line {line.rank}</strong><span>{line.evaluationText.score}</span>{line.depth ? <small>Depth {line.depth}</small> : null}<em>{line.formatted.notation}</em></div>
      <code>{line.formatted.moves.join(' ') || 'No continuation reported'}</code>
    </li>)}</ol>
  </div>;
}
