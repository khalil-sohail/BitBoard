import type { PrincipalVariationView } from './live-data.types';

export function sortPrincipalVariations(lines: readonly PrincipalVariationView[]): PrincipalVariationView[] {
  return [...lines].sort((left, right) => left.rank - right.rank);
}
