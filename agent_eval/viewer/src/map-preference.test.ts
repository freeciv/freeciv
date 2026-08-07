import { describe, expect, it } from 'vitest'
import {
  MAP_HASH,
  MAP_PREFERENCE_KEY,
  mapSectionOpen,
  rememberMapSection,
  type PreferenceStore,
} from './map-preference'

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

describe('strategic map preference', () => {
  it('leads with the map when nothing has been chosen', () => {
    expect(mapSectionOpen('', memoryStore())).toBe(true)
    expect(mapSectionOpen('#overview', memoryStore())).toBe(true)
  })

  it('opens from the URL hash and closes only on an explicit choice', () => {
    expect(mapSectionOpen(MAP_HASH, memoryStore())).toBe(true)
    expect(mapSectionOpen('', memoryStore({ [MAP_PREFERENCE_KEY]: 'open' }))).toBe(true)
    expect(mapSectionOpen('', memoryStore({ [MAP_PREFERENCE_KEY]: 'closed' }))).toBe(false)
  })

  it('round-trips the remembered choice across a reload', () => {
    const store = memoryStore()
    rememberMapSection(false, store)
    expect(mapSectionOpen('', store)).toBe(false)
    rememberMapSection(true, store)
    expect(mapSectionOpen('', store)).toBe(true)
  })

  it('still leads with the map when storage is unavailable', () => {
    expect(mapSectionOpen('', failingStore)).toBe(true)
    expect(mapSectionOpen('', null)).toBe(true)
    expect(() => rememberMapSection(true, failingStore)).not.toThrow()
    expect(mapSectionOpen(MAP_HASH, failingStore)).toBe(true)
  })
})
