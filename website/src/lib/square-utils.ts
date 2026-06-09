import { Square } from 'chess.js';

// Convert a single UCI move string like "e2e4" or "e7e8q" to a custom arrow format
// Expected return format for react-chessboard: [Square, Square, string]
// where string is a valid CSS color
export function convertUciToArrow(uciMove: string, color: string = 'rgba(142, 72%, 45%, 0.8)'): [Square, Square, string] | null {
  if (!uciMove || uciMove.length < 4) return null;
  
  const from = uciMove.substring(0, 2) as Square;
  const to = uciMove.substring(2, 4) as Square;
  
  // Basic validation that they look like coordinates
  if (!/^[a-h][1-8]$/.test(from) || !/^[a-h][1-8]$/.test(to)) {
    return null;
  }
  
  return [from, to, color];
}

export function generatePVArrows(pvLines: string[] | undefined, baseColor: string = 'rgba(71, 225, 137, 0.6)'): [Square, Square, string][] {
    if (!pvLines || pvLines.length === 0) return [];

    const arrows: [Square, Square, string][] = [];
    
    // We only want to draw arrows for the first few moves to avoid clutter
    // Fade out the opacity for deeper moves
    const maxArrows = Math.min(pvLines.length, 3);
    
    for (let i = 0; i < maxArrows; i++) {
        const uciMove = pvLines[i];
        
        // Calculate fading opacity (e.g., 0.8, 0.5, 0.3)
        let opacity = 0.8 - (i * 0.25);
        if (opacity < 0.2) opacity = 0.2;
        
        // Replace opacity in the base color
        const color = baseColor.replace(/[\d.]+\)$/, `${opacity})`);
        
        const arrow = convertUciToArrow(uciMove, color);
        if (arrow) {
            arrows.push(arrow);
        }
    }
    
    return arrows;
}
