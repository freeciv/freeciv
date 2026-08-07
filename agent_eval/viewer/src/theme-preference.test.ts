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
  it('is light until someone chooses otherwise', () => {
    expect(resolveTheme(memoryStore())).toBe('light')
  })

  it('lets an explicit choice move off the default surface', () => {
    expect(resolveTheme(memoryStore({ [THEME_PREFERENCE_KEY]: 'dark' }))).toBe('dark')
    expect(resolveTheme(memoryStore({ [THEME_PREFERENCE_KEY]: 'light' }))).toBe('light')
  })

  it('ignores a stored value that is not a theme', () => {
    expect(resolveTheme(memoryStore({ [THEME_PREFERENCE_KEY]: 'sepia' }))).toBe('light')
    expect(resolveTheme(memoryStore({ [THEME_PREFERENCE_KEY]: '' }))).toBe('light')
  })

  it('round-trips the remembered choice across a reload', () => {
    const store = memoryStore()
    rememberTheme('dark', store)
    expect(resolveTheme(store)).toBe('dark')
    rememberTheme('light', store)
    expect(resolveTheme(store)).toBe('light')
  })

  it('stays on the default surface when storage is unavailable', () => {
    expect(resolveTheme(failingStore)).toBe('light')
    expect(resolveTheme(null)).toBe('light')
    expect(() => rememberTheme('light', failingStore)).not.toThrow()
  })

  it('writes the chosen surface onto the document explicitly', () => {
    const attributes = new Map<string, string>()
    const root: ThemeTarget = {
      setAttribute: (name, value) => { attributes.set(name, value) },
      removeAttribute: (name) => { attributes.delete(name) },
    }
    applyTheme('light', root)
    expect(attributes.get('data-theme')).toBe('light')
    applyTheme('dark', root)
    expect(attributes.get('data-theme')).toBe('dark')
  })
})
