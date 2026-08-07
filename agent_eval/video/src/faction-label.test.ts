import { describe, expect, it } from 'vitest'
import {
  controllerDisplayName, factionDisplayLabel, nationDisplayName,
} from './faction-label'
import type { PlayerEntry } from './dataset/schema'

/**
 * These cases mirror `agent_eval/viewer/src/faction-label.test.ts`, so a
 * change to either surface's naming fails on both instead of letting a
 * rendered film and a live viewer tab credit the same match differently.
 */

function player(fields: Partial<PlayerEntry> = {}): PlayerEntry {
  return {
    playerId: 0,
    seatId: 'place-1',
    seat: true,
    name: 'AgentPlace1',
    nation: 'English',
    color: '#0067A5',
    controllerLabel: null,
    controllerType: null,
    model: null,
    aiDifficulty: null,
    ...fields,
  }
}

const NATIVE = {
  playerId: 1,
  seatId: 'place-2',
  name: 'NativePlace2',
  nation: 'Italian',
  controllerLabel: 'Freeciv Classic AI',
  controllerType: 'native',
} as const

describe('film faction labels', () => {
  it('labels a harness seat as model: civilization', () => {
    expect(factionDisplayLabel(player({
      controllerLabel: 'pi-gpt-5.6-sol',
      controllerType: 'external',
      nation: 'English',
    }))).toBe('pi-gpt-5.6-sol: English')
  })

  it('labels the native side nation-first with its difficulty', () => {
    expect(factionDisplayLabel(player({
      ...NATIVE, aiDifficulty: 'hard',
    }))).toBe('Italian (CPU: Hard)')
  })

  it("names freeciv's cheating level Deity, as Civilization does", () => {
    expect(factionDisplayLabel(player({
      ...NATIVE, aiDifficulty: 'cheating',
    }))).toBe('Italian (CPU: Deity)')
  })

  it('drops the difficulty when the dataset does not record one', () => {
    // Every dataset exported before `ai_difficulty` existed lands here.
    expect(factionDisplayLabel(player(NATIVE))).toBe('Italian (CPU)')
    expect(factionDisplayLabel(player({
      ...NATIVE, aiDifficulty: 'novice',
    }))).toBe('Italian (CPU)')
  })

  it('splits the native label into a controller and a nation half', () => {
    const native = player({ ...NATIVE, aiDifficulty: 'cheating' })
    expect(controllerDisplayName(native)).toBe('CPU: Deity')
    expect(nationDisplayName(native)).toBe('Italian')
  })

  it('leaves the dynamic faction naming alone', () => {
    expect(factionDisplayLabel(player({
      playerId: 2,
      seatId: null,
      seat: false,
      name: 'Blackbeard',
      nation: 'Pirate',
      controllerLabel: 'Freeciv dynamic faction',
      controllerType: 'dynamic',
    }))).toBe('Freeciv dynamic: Pirate')
  })
})
