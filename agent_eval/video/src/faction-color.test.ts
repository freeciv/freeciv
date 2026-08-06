import { describe, expect, it } from 'vitest'
import { colorDistance, planFactionColors } from './faction-color'
import type { PlayerEntry } from './dataset/schema'

/**
 * The film and the replay viewer must produce the same plan for the same
 * factions. These cases mirror `agent_eval/viewer/src/display-color.test.ts`
 * verbatim, so a change to either calibration fails on both sides instead of
 * silently letting one surface drift.
 */

const OCEAN = '#277086'
const DESERT = '#c7a161'
const PLAINS = '#9e965b'
const FOREST = '#42774b'

const AGENT_BLUE = '#0067A5'
const PERIWINKLE = '#A78BFA'
const NATIVE_ORANGE = '#F38400'
const PIRATE_GOLDENROD = '#B8860B'
const PIRATE_SALMON = '#FA8072'
const BARBARIAN_PURPLE = '#991199'
const NATIVE_GREEN = '#008856'
const MINT = '#5cf0d0'

function faction(playerId: number, color: string, nation = `Nation ${playerId}`): PlayerEntry {
  return {
    playerId,
    seatId: `place-${playerId + 1}`,
    seat: true,
    name: `Player ${playerId}`,
    nation,
    color,
    controllerLabel: null,
    controllerType: null,
    model: null,
  }
}

function planOf(players: readonly PlayerEntry[]): string[] {
  const plan = planFactionColors(players)
  return players.map((player) => plan.colorByPlayer.get(player.playerId) ?? 'none')
}

function nearestTerrain(color: string): number {
  return [
    '#194c62', OCEAN, '#3d8192', '#e1e5dc', DESERT, FOREST, '#78965e',
    '#8c8255', '#346640', '#8d8c82', PLAINS, '#526f5c', '#98a092',
  ].reduce((best, terrain) => Math.min(best, colorDistance(color, terrain)), Infinity)
}

const LNJ = [
  faction(0, AGENT_BLUE, 'English'),
  faction(1, NATIVE_ORANGE, 'Italian'),
  faction(2, PIRATE_GOLDENROD, 'Pirate'),
  faction(3, BARBARIAN_PURPLE, 'Barbarian'),
]

const A8 = [
  faction(0, AGENT_BLUE, 'English'),
  faction(1, NATIVE_ORANGE, 'Spanish'),
  faction(2, PIRATE_SALMON, 'Pirate'),
]

describe('the pinned pair', () => {
  it('always remaps the agent seat blue to periwinkle', () => {
    expect(planOf([faction(0, AGENT_BLUE)])).toEqual([PERIWINKLE])
  })

  it('honours the pin regardless of the recorded hex casing', () => {
    expect(planOf([faction(0, '#0067a5')])).toEqual([PERIWINKLE])
  })

  it('pins ahead of the clearance search, so no plan can displace it', () => {
    // Even crowded by periwinkle-adjacent factions the pin holds; only the
    // other faction is free to move.
    const plan = planOf([faction(0, AGENT_BLUE), faction(1, '#A78BFA')])
    expect(plan[0]).toBe(PERIWINKLE)
    expect(plan[1]).not.toBe(PERIWINKLE)
  })
})

describe('OKLab clearance calibration', () => {
  it('separates the colours that vanish from the ones that read', () => {
    // Below 0.085: the two that disappear into the board. The goldenrod's
    // nearest terrain is plains, not the desert it gets blamed for.
    expect(colorDistance(AGENT_BLUE, OCEAN)).toBeCloseTo(0.065, 3)
    expect(colorDistance(PIRATE_GOLDENROD, PLAINS)).toBeCloseTo(0.065, 3)
    expect(colorDistance(PIRATE_GOLDENROD, DESERT)).toBeCloseTo(0.087, 3)
    expect(nearestTerrain(PIRATE_GOLDENROD)).toBeCloseTo(0.065, 3)
    // Above 0.085: the one that has always read fine.
    expect(colorDistance(NATIVE_ORANGE, DESERT)).toBeCloseTo(0.093, 3)
    expect(nearestTerrain(NATIVE_ORANGE)).toBeGreaterThan(0.085)
  })

  it('catches the native green that hides in forest', () => {
    // A third seat colour nobody had flagged: #008856 is closer to forest than
    // the agent blue ever was to ocean.
    expect(colorDistance(NATIVE_GREEN, FOREST)).toBeCloseTo(0.054, 3)
    expect(nearestTerrain(NATIVE_GREEN)).toBeLessThan(0.085)
    expect(planOf([
      faction(0, AGENT_BLUE),
      faction(1, NATIVE_ORANGE),
      faction(2, NATIVE_GREEN),
    ])).toEqual([PERIWINKLE, NATIVE_ORANGE, MINT])
  })

  it('catches a faction that clears terrain but not another faction', () => {
    expect(nearestTerrain(PIRATE_SALMON)).toBeGreaterThan(0.085)
    expect(colorDistance(PIRATE_SALMON, NATIVE_ORANGE)).toBeCloseTo(0.087, 3)
    expect(colorDistance(PIRATE_SALMON, NATIVE_ORANGE)).toBeLessThan(0.15)
  })

  it('keeps every colour it hands out clear of terrain', () => {
    expect(nearestTerrain(PERIWINKLE)).toBeGreaterThan(0.085)
    expect(nearestTerrain(MINT)).toBeGreaterThan(0.085)
  })
})

describe('whole-match plans', () => {
  it('reproduces the lnj plan the viewer asserts', () => {
    expect(planOf(LNJ)).toEqual([
      PERIWINKLE,        // pinned, never derived
      NATIVE_ORANGE,     // clears desert at 0.093, kept
      MINT,              // goldenrod sits 0.065 from plains, moved
      BARBARIAN_PURPLE,  // clears terrain and every faction, kept
    ])
  })

  it('reproduces the a8 plan the viewer asserts', () => {
    expect(planOf(A8)).toEqual([PERIWINKLE, NATIVE_ORANGE, MINT])
  })

  it('moves a faction for terrain and another for crowding in one plan', () => {
    // Salmon clears terrain; it moves only because orange is already placed.
    expect(planOf([faction(0, NATIVE_ORANGE), faction(1, PIRATE_SALMON)]))
      .toEqual([NATIVE_ORANGE, MINT])
    // With orange absent, salmon keeps its recorded colour.
    expect(planOf([faction(0, PIRATE_SALMON)])).toEqual([PIRATE_SALMON])
  })

  it('is stable under the order factions arrive in', () => {
    const reversed = [...LNJ].reverse()
    const plan = planFactionColors(reversed)
    expect(LNJ.map((player) => plan.colorByPlayer.get(player.playerId)))
      .toEqual([PERIWINKLE, NATIVE_ORANGE, MINT, BARBARIAN_PURPLE])
  })

  it('reports only what moved, so the dossier note stays honest', () => {
    expect(planFactionColors(LNJ).substitutions).toEqual([
      { playerId: 0, nation: 'English', from: AGENT_BLUE, to: PERIWINKLE },
      { playerId: 2, nation: 'Pirate', from: PIRATE_GOLDENROD, to: MINT },
    ])
  })
})
