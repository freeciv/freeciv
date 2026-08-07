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

function systemPrefersDark(): boolean {
  try {
    return window.matchMedia('(prefers-color-scheme: dark)').matches
  } catch {
    return false
  }
}

/**
 * An explicit choice wins; absent one the arena follows the operating system;
 * absent that it is light, which is the arena's default surface.
 */
export function resolveTheme(
  store: PreferenceStore | null = safeStore(),
  prefersDark: boolean = systemPrefersDark(),
): Theme {
  try {
    const stored = store?.getItem(THEME_PREFERENCE_KEY)
    if (stored === 'light' || stored === 'dark') return stored
  } catch {
    // Fall through to the system preference.
  }
  return prefersDark ? 'dark' : 'light'
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
