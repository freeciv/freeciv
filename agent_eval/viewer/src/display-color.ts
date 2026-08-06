/**
 * Presentation-only palette remap for recorded player colors.
 *
 * Recorded data is never rewritten: replay payloads, wire responses, and every
 * export keep the exact color Freeciv assigned to a player. A few of those hues
 * are indistinguishable from the board's ocean, so the viewer substitutes a
 * legible hue at the moment it paints. The table is pinned to the offline video
 * renderer so a match reads the same in both surfaces.
 *
 * The substitutes are deliberately never themselves keys, so applying the remap
 * twice is the same as applying it once.
 */
const DISPLAY_COLORS: ReadonlyMap<string, string> = new Map([
  // The standard agent seat blue sits inside the board's water range.
  ['#0067a5', '#A78BFA'],
])

/**
 * Returns the color to paint for a recorded player color. Unmapped colors pass
 * through byte for byte; an absent color stays absent so callers keep deciding
 * their own neutral fallback.
 */
export function displayPlayerColor(color: string | null | undefined): string | null {
  if (!color) return null
  return DISPLAY_COLORS.get(color.trim().toLowerCase()) ?? color
}
