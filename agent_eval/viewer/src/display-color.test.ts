import { describe, expect, it } from 'vitest'
import {
  buildDisplayPalette,
  colorDistance,
  displayPlayerColor,
  nearestTerrainDistance,
  type DisplayFaction,
} from './display-color'

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

/** The two matches the film renderer reports plans for. Both surfaces must
 *  agree faction for faction, so these are the film's plans verbatim. */
const LNJ: DisplayFaction[] = [
  { playerId: 0, color: AGENT_BLUE },        // English, the agent seat
  { playerId: 1, color: NATIVE_ORANGE },     // Italian, the native AI
  { playerId: 2, color: PIRATE_GOLDENROD },  // Pirate
  { playerId: 3, color: BARBARIAN_PURPLE },  // Barbarian
]

const A8: DisplayFaction[] = [
  { playerId: 0, color: AGENT_BLUE },     // English, the agent seat
  { playerId: 1, color: NATIVE_ORANGE },  // Spanish, the native AI
  { playerId: 2, color: PIRATE_SALMON },  // Pirate
]

function planOf(factions: DisplayFaction[]): string[] {
  const palette = buildDisplayPalette(factions)
  return factions.map((faction) => displayPlayerColor(faction.color, palette) ?? 'none')
}

describe('displayPlayerColor without a palette', () => {
  it('remaps the pinned agent seat blue to periwinkle', () => {
    expect(displayPlayerColor(AGENT_BLUE)).toBe(PERIWINKLE)
  })

  it('matches the pinned table without regard to case or surrounding space', () => {
    expect(displayPlayerColor('#0067a5')).toBe(PERIWINKLE)
    expect(displayPlayerColor(' #0067A5 ')).toBe(PERIWINKLE)
  })

  it('passes an unpinned color through byte for byte', () => {
    expect(displayPlayerColor(NATIVE_ORANGE)).toBe(NATIVE_ORANGE)
    expect(displayPlayerColor(PIRATE_GOLDENROD)).toBe(PIRATE_GOLDENROD)
  })

  it('reports an absent color as absent instead of inventing one', () => {
    expect(displayPlayerColor(null)).toBeNull()
    expect(displayPlayerColor(undefined)).toBeNull()
    expect(displayPlayerColor('')).toBeNull()
  })

  it('is idempotent, so a display color survives a second pass unchanged', () => {
    expect(displayPlayerColor(displayPlayerColor(AGENT_BLUE))).toBe(PERIWINKLE)
  })
})

describe('OKLab clearance calibration', () => {
  it('separates the colors that vanish from the ones that read', () => {
    // Below 0.085: the two that disappear into the board. The goldenrod's
    // nearest terrain is plains, not the desert it gets blamed for (0.087).
    expect(colorDistance(AGENT_BLUE, OCEAN)).toBeCloseTo(0.065, 3)
    expect(colorDistance(PIRATE_GOLDENROD, PLAINS)).toBeCloseTo(0.065, 3)
    expect(colorDistance(PIRATE_GOLDENROD, DESERT)).toBeCloseTo(0.087, 3)
    expect(nearestTerrainDistance(PIRATE_GOLDENROD)).toBeCloseTo(0.065, 3)
    // Above 0.085: the one that has always read fine.
    expect(colorDistance(NATIVE_ORANGE, DESERT)).toBeCloseTo(0.093, 3)
    expect(nearestTerrainDistance(NATIVE_ORANGE)).toBeGreaterThan(0.085)
  })

  it('catches a faction that clears terrain but not another faction', () => {
    expect(nearestTerrainDistance(PIRATE_SALMON)).toBeGreaterThan(0.085)
    expect(colorDistance(PIRATE_SALMON, NATIVE_ORANGE)).toBeCloseTo(0.087, 3)
    expect(colorDistance(PIRATE_SALMON, NATIVE_ORANGE)).toBeLessThan(0.15)
  })

  it('catches the native green that hides in forest', () => {
    // A third seat color nobody had flagged: #008856 is closer to forest than
    // the agent blue ever was to ocean.
    expect(colorDistance(NATIVE_GREEN, FOREST)).toBeCloseTo(0.054, 3)
    expect(nearestTerrainDistance(NATIVE_GREEN)).toBeLessThan(0.085)
    expect(planOf([
      { playerId: 0, color: AGENT_BLUE },
      { playerId: 1, color: NATIVE_ORANGE },
      { playerId: 2, color: NATIVE_GREEN },
    ])).toEqual([PERIWINKLE, NATIVE_ORANGE, MINT])
  })

  it('keeps every substitute clear of terrain', () => {
    expect(nearestTerrainDistance(PERIWINKLE)).toBeGreaterThan(0.085)
    expect(nearestTerrainDistance(MINT)).toBeGreaterThan(0.085)
  })
})

describe('buildDisplayPalette', () => {
  it('reproduces the film plan for the lnj match', () => {
    expect(planOf(LNJ)).toEqual([
      PERIWINKLE,        // pinned, never derived
      NATIVE_ORANGE,     // clears desert at 0.093, kept
      MINT,              // goldenrod sits 0.065 from plains, moved
      BARBARIAN_PURPLE,  // clears terrain and every faction, kept
    ])
  })

  it('reproduces the film plan for the a8 match', () => {
    expect(planOf(A8)).toEqual([PERIWINKLE, NATIVE_ORANGE, MINT])
  })

  it('reports what moved and why it is worth telling the reader', () => {
    expect(buildDisplayPalette(LNJ).substitutions).toEqual([
      { playerId: 0, from: AGENT_BLUE, to: PERIWINKLE },
      { playerId: 2, from: PIRATE_GOLDENROD, to: MINT },
    ])
    // The two factions that were left alone are absent from the report.
    expect(buildDisplayPalette(LNJ).substitutions.map((entry) => entry.from))
      .not.toContain(NATIVE_ORANGE)
  })

  it('moves a faction for terrain and another for faction crowding in one plan', () => {
    // Salmon clears terrain; it moves only because orange is already placed.
    expect(planOf([
      { playerId: 0, color: NATIVE_ORANGE },
      { playerId: 1, color: PIRATE_SALMON },
    ])).toEqual([NATIVE_ORANGE, MINT])
    // With orange absent, salmon keeps its recorded color.
    expect(planOf([{ playerId: 0, color: PIRATE_SALMON }])).toEqual([PIRATE_SALMON])
  })

  it('pins the agent blue before any clearance step can claim its substitute', () => {
    // Periwinkle is offered to nobody else first: even with a faction ahead of
    // it in player order, the pinned seat still lands on periwinkle.
    expect(planOf([
      { playerId: 0, color: PIRATE_GOLDENROD },
      { playerId: 1, color: AGENT_BLUE },
    ])).toEqual([MINT, PERIWINKLE])
  })

  it('is deterministic: input order does not change the plan', () => {
    const shuffled = [LNJ[3], LNJ[1], LNJ[0], LNJ[2]]
    const byPlayer = new Map(
      shuffled.map((faction) => [
        faction.playerId,
        displayPlayerColor(faction.color, buildDisplayPalette(shuffled)),
      ]),
    )
    expect([0, 1, 2, 3].map((id) => byPlayer.get(id))).toEqual([
      PERIWINKLE, NATIVE_ORANGE, MINT, BARBARIAN_PURPLE,
    ])
  })

  it('memoizes a faction set so consumers can depend on palette identity', () => {
    expect(buildDisplayPalette(LNJ)).toBe(buildDisplayPalette([...LNJ]))
    expect(buildDisplayPalette(LNJ)).not.toBe(buildDisplayPalette(A8))
  })

  it('skips factions with no recorded color instead of planning around a blank', () => {
    const palette = buildDisplayPalette([
      { playerId: 0, color: null },
      { playerId: 1, color: undefined },
      { playerId: 2, color: PIRATE_GOLDENROD },
    ])
    expect(displayPlayerColor(null, palette)).toBeNull()
    expect(displayPlayerColor(PIRATE_GOLDENROD, palette)).toBe(MINT)
  })

  it('falls back to the pinned table for a faction the plan never saw', () => {
    const palette = buildDisplayPalette([{ playerId: 0, color: NATIVE_ORANGE }])
    expect(displayPlayerColor(AGENT_BLUE, palette)).toBe(PERIWINKLE)
    expect(displayPlayerColor('#123456', palette)).toBe('#123456')
  })

  it('plans an empty faction set without inventing entries', () => {
    const palette = buildDisplayPalette([])
    expect(palette.substitutions).toEqual([])
    expect(displayPlayerColor(NATIVE_ORANGE, palette)).toBe(NATIVE_ORANGE)
  })
})
