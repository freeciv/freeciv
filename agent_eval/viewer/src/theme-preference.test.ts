import { describe, expect, it } from 'vitest'
import type { PreferenceStore } from './map-preference'
import {
  THEME_PREFERENCE_KEY,
  applyTheme,
  rememberTheme,
  resolveTheme,
  type ThemeTarget,
} from './theme-preference'

function memoryStore(seed: Record<string, string> = {}): PreferenceStore {
  const values = new Map(Object.entries(seed))
  return {
    getItem: (key) => values.get(key) ?? null,
    setItem: (key, value) => { values.set(key, value) },
  }
}

const failingStore: PreferenceStore = {
  getItem() { throw new Error('storage is blocked') },
  setItem() { throw new Error('storage is blocked') },
}

describe('arena theme preference', () => {
  it('follows the operating system when nothing has been chosen', () => {
    expect(resolveTheme(memoryStore(), true)).toBe('light')
    expect(resolveTheme(memoryStore(), false)).toBe('dark')
  })

  it('lets an explicit choice outrank the operating system', () => {
    expect(resolveTheme(memoryStore({ [THEME_PREFERENCE_KEY]: 'dark' }), true)).toBe('dark')
    expect(resolveTheme(memoryStore({ [THEME_PREFERENCE_KEY]: 'light' }), false)).toBe('light')
  })

  it('ignores a stored value that is not a theme', () => {
    expect(resolveTheme(memoryStore({ [THEME_PREFERENCE_KEY]: 'sepia' }), false)).toBe('dark')
    expect(resolveTheme(memoryStore({ [THEME_PREFERENCE_KEY]: '' }), true)).toBe('light')
  })

  it('round-trips the remembered choice across a reload', () => {
    const store = memoryStore()
    rememberTheme('light', store)
    expect(resolveTheme(store, false)).toBe('light')
    rememberTheme('dark', store)
    expect(resolveTheme(store, true)).toBe('dark')
  })

  it('falls back to the system preference when storage is unavailable', () => {
    expect(resolveTheme(failingStore, true)).toBe('light')
    expect(resolveTheme(null, false)).toBe('dark')
    expect(() => rememberTheme('light', failingStore)).not.toThrow()
  })

  it('marks only the light surface on the document, leaving dark unattributed', () => {
    const attributes = new Map<string, string>()
    const root: ThemeTarget = {
      setAttribute: (name, value) => { attributes.set(name, value) },
      removeAttribute: (name) => { attributes.delete(name) },
    }
    applyTheme('light', root)
    expect(attributes.get('data-theme')).toBe('light')
    applyTheme('dark', root)
    expect(attributes.has('data-theme')).toBe(false)
  })
})
