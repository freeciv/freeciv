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

function systemPrefersLight(): boolean {
  try {
    return window.matchMedia('(prefers-color-scheme: light)').matches
  } catch {
    return false
  }
}

/**
 * An explicit choice wins; absent one the arena follows the operating system;
 * absent that it is dark, which is the surface the match films are cut on.
 */
export function resolveTheme(
  store: PreferenceStore | null = safeStore(),
  prefersLight: boolean = systemPrefersLight(),
): Theme {
  try {
    const stored = store?.getItem(THEME_PREFERENCE_KEY)
    if (stored === 'light' || stored === 'dark') return stored
  } catch {
    // Fall through to the system preference.
  }
  return prefersLight ? 'light' : 'dark'
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
 * the choice becomes visible. Dark is the default skin and carries no
 * attribute, which keeps the default path free of a flash-of-wrong-theme.
 */
export function applyTheme(theme: Theme, root: ThemeTarget = document.documentElement): void {
  if (theme === 'light') root.setAttribute('data-theme', 'light')
  else root.removeAttribute('data-theme')
}
