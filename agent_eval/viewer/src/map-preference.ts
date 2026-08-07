/** Persistence for the deferred strategic-map section on the match page. */

export const MAP_PREFERENCE_KEY = 'freeciv-arena.map-section'
export const MAP_HASH = '#map'

export interface PreferenceStore {
  getItem(key: string): string | null
  setItem(key: string, value: string): void
}

function safeStore(): PreferenceStore | null {
  try {
    return window.localStorage
  } catch {
    // Storage access throws in locked-down browser modes. Stay collapsed.
    return null
  }
}

/**
 * The map leads the page now, so it opens unless the viewer has closed it.
 * That costs the board chunk on first paint, which is the deliberate trade:
 * the world state is the thing people come to watch, and a match that opens
 * on a "Show map" button buries its own subject.
 */
export function mapSectionOpen(
  hash: string,
  store: PreferenceStore | null = safeStore(),
): boolean {
  if (hash === MAP_HASH) return true
  try {
    return store?.getItem(MAP_PREFERENCE_KEY) !== 'closed'
  } catch {
    return true
  }
}

export function rememberMapSection(
  open: boolean,
  store: PreferenceStore | null = safeStore(),
): void {
  try {
    store?.setItem(MAP_PREFERENCE_KEY, open ? 'open' : 'closed')
  } catch {
    // A refresh simply forgets the choice when storage is unavailable.
  }
}
