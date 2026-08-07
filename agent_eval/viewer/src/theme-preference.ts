/** Persistence for the arena's light/dark surface. */

import type { PreferenceStore } from './map-preference'

export const THEME_PREFERENCE_KEY = 'freeciv-arena.theme'

export type Theme = 'dark' | 'light'

function safeStore(): PreferenceStore | null {
  try {
    return window.localStorage
  } catch {
    // Storage access throws in locked-down browser modes. Fall back to the OS.
    return null
  }
}

/**
 * Light is the arena's surface. Only an explicit choice moves off it.
 *
 * Deliberately NOT `prefers-color-scheme`: the arena is a presentation surface
 * with one intended look, the way a film has one grade, and most people run
 * their OS dark without wanting every page they open to be dark. Dark stays a
 * click away for anyone who wants it, and that choice is what persists.
 */
export function resolveTheme(store: PreferenceStore | null = safeStore()): Theme {
  try {
    const stored = store?.getItem(THEME_PREFERENCE_KEY)
    if (stored === 'light' || stored === 'dark') return stored
  } catch {
    // Storage is blocked; the default surface still stands.
  }
  return 'light'
}

export function rememberTheme(
  theme: Theme,
  store: PreferenceStore | null = safeStore(),
): void {
  try {
    store?.setItem(THEME_PREFERENCE_KEY, theme)
  } catch {
    // A refresh simply forgets the choice when storage is unavailable.
  }
}

/** All `applyTheme` needs of the document, so a test can hand it a stub. */
export interface ThemeTarget {
  setAttribute(name: string, value: string): void
  removeAttribute(name: string): void
}

/**
 * The tokens key off `<html data-theme>`, so the attribute is the single place
 * the choice becomes visible. It is always written explicitly -- `index.html`
 * ships `data-theme="light"` so the default surface is painted before any
 * script runs, and there is no frame of the wrong theme on load.
 */
export function applyTheme(theme: Theme, root: ThemeTarget = document.documentElement): void {
  root.setAttribute('data-theme', theme)
}
