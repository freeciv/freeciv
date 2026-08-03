import { describe, expect, it } from 'vitest'
import { LatestRequestGate, LruCache, priorAvailableTurns } from './board-loader'

describe('board playback coordination', () => {
  it('does not allow a stale slow turn to overwrite the latest request', () => {
    const gate = new LatestRequestGate()
    const slowTurn = gate.begin()
    const latestTurn = gate.begin()
    expect(gate.isCurrent(slowTurn)).toBe(false)
    expect(gate.isCurrent(latestTurn)).toBe(true)
  })

  it('retains only the most recent bounded turn payloads', () => {
    const cache = new LruCache<number>(2)
    cache.set('turn-1', 1)
    cache.set('turn-2', 2)
    expect(cache.get('turn-1')).toBe(1)
    cache.set('turn-3', 3)
    expect(cache.has('turn-1')).toBe(true)
    expect(cache.has('turn-2')).toBe(false)
    expect(cache.get('turn-3')).toBe(3)
  })

  it('walks sparse saved turns backward newest-first within a bound', () => {
    expect(priorAvailableTurns([1, 80, 321, 100, 321, 400], 322, 3)).toEqual([321, 100, 80])
    expect(priorAvailableTurns([1, 2], 1)).toEqual([])
  })
})
