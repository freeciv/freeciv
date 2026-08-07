import { describe, expect, it } from 'vitest'
import { formatYear } from './game-year'

describe('formatYear', () => {
  it('reads a negative year as BC, without the sign', () => {
    expect(formatYear(-4000)).toBe('4000 BC')
    expect(formatYear(-300)).toBe('300 BC')
    expect(formatYear(-1)).toBe('1 BC')
  })

  it('reads a positive year as AD', () => {
    expect(formatYear(1)).toBe('AD 1')
    expect(formatYear(1200)).toBe('AD 1200')
    expect(formatYear(2050)).toBe('AD 2050')
  })

  it('holds the BC/AD boundary the way the film does', () => {
    // Freeciv skips year 0, so this branch never fires on real data; it is
    // pinned here only so the viewer and the film keep answering alike.
    expect(formatYear(0)).toBe('1 AD')
  })

  it('never emits a bare signed integer', () => {
    for (const year of [-4000, -50, -1, 0, 1, 1200]) {
      expect(formatYear(year)).not.toMatch(/^-?\d+$/)
    }
  })
})
