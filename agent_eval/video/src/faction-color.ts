/**
 * Render-time faction colour substitution.
 *
 * Freeciv hands out nation colours with no knowledge of the map underneath
 * them. The agent seat's default blue (#0067A5) sits right on top of the ocean
 * palette, so its territory tint and its city and unit markers disappear into
 * the water. This module swaps any faction colour that lands too close to the
 * board's own terrain colours for one that cannot occur on a Freeciv map.
 *
 * This is presentation only and lives entirely in the film: the exported
 * dataset and the replay viewer keep the original colours. The mapping is
 * deterministic — the same seat in the same match always gets the same
 * substitute — and the original is surfaced in the match dossier.
 */

import type { PlayerEntry } from './dataset/schema'
import { parseHexColor, terrainColor } from './theme'

/** Terrain fills a faction colour must stay clear of. */
const BOARD_COLORS: readonly string[] = [
  'Deep Ocean', 'Ocean', 'Lake', 'Glacier', 'Desert', 'Forest', 'Grassland',
  'Hills', 'Jungle', 'Mountains', 'Plains', 'Swamp', 'Tundra',
].map(terrainColor)

/**
 * Fixed substitutions that must hold everywhere, whatever the algorithm below
 * would otherwise pick. The replay viewer applies the same remap, and the two
 * surfaces have to agree tile for tile, so the agent seat's water-coloured blue
 * is pinned rather than derived.
 *
 * This table mirrors `DISPLAY_COLORS` in
 * `agent_eval/viewer/src/display-color.ts`. Keep the two in step: an entry here
 * that is missing there means a match reads differently in the two surfaces.
 * The clearance search below may still move *other* factions that the viewer
 * leaves alone, since only the film has to keep a whole board legible at once.
 *
 * Periwinkle clears every terrain by a wide margin (0.181 to its nearest,
 * tundra) and sits at least 0.23 from every other faction colour in use.
 */
const PINNED: ReadonlyMap<string, string> = new Map([
  ['#0067a5', '#A78BFA'], // agent blue -> periwinkle violet
])

/**
 * Substitutes for anything not pinned, in preference order. None of these can
 * be confused with terrain: Freeciv maps have no magenta, mint, or violet at
 * these chromas.
 */
const SUBSTITUTES: readonly string[] = [
  '#5cf0d0', // aqua mint
  '#ff5ecb', // hot magenta
  '#b57cff', // violet
  '#ffe9b0', // warm cream
  '#ff9d3d', // amber
]

/**
 * OKLab clearances, calibrated against the actual Freeciv seat colours.
 *
 * The agent's blue sits 0.065 from ocean, which is what makes it vanish; the
 * default orange sits 0.093 from desert, which reads fine. 0.085 separates
 * exactly those two cases. Faction-to-faction needs a wider margin: the default
 * orange and the barbarian salmon are only 0.087 apart from each other, and on
 * a busy board that is not enough to tell two empires apart.
 */
const TERRAIN_CLEARANCE = 0.085
const FACTION_CLEARANCE = 0.15

interface Oklab {
  readonly l: number
  readonly a: number
  readonly b: number
}

function linearize(channel: number): number {
  const value = channel / 255
  return value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4
}

/** sRGB to OKLab, so "perceptually close" means what it looks like it means. */
export function toOklab(color: string): Oklab {
  const { r, g, b } = parseHexColor(color)
  const red = linearize(r)
  const green = linearize(g)
  const blue = linearize(b)
  const long = Math.cbrt(0.4122214708 * red + 0.5363325363 * green + 0.0514459929 * blue)
  const medium = Math.cbrt(0.2119034982 * red + 0.6806995451 * green + 0.1073969566 * blue)
  const short = Math.cbrt(0.0883024619 * red + 0.2817188376 * green + 0.6299787005 * blue)
  return {
    l: 0.2104542553 * long + 0.793617785 * medium - 0.0040720468 * short,
    a: 1.9779984951 * long - 2.428592205 * medium + 0.4505937099 * short,
    b: 0.0259040371 * long + 0.7827717662 * medium - 0.808675766 * short,
  }
}

export function colorDistance(left: string, right: string): number {
  const one = toOklab(left)
  const two = toOklab(right)
  return Math.hypot(one.l - two.l, one.a - two.a, one.b - two.b)
}

function nearestTerrainDistance(color: string): number {
  return BOARD_COLORS.reduce(
    (best, terrain) => Math.min(best, colorDistance(color, terrain)),
    Number.POSITIVE_INFINITY,
  )
}

export interface FactionColorPlan {
  /** The colour every part of the film should draw this faction with. */
  readonly colorByPlayer: ReadonlyMap<number, string>
  /** Only the factions that were actually moved, for the dossier note. */
  readonly substitutions: readonly {
    readonly playerId: number
    readonly nation: string
    readonly from: string
    readonly to: string
  }[]
}

/**
 * Decide a render colour per faction, in player order so the result is stable.
 *
 * A faction keeps its own colour unless it is too close to the terrain palette
 * or to a faction already placed. Substitutes are taken in order, skipping any
 * that would collide with a colour already in play.
 */
export function planFactionColors(players: readonly PlayerEntry[]): FactionColorPlan {
  const colorByPlayer = new Map<number, string>()
  const substitutions: {
    playerId: number; nation: string; from: string; to: string
  }[] = []
  const taken: string[] = []

  const clearsTaken = (candidate: string): boolean =>
    taken.every((used) => colorDistance(candidate, used) >= FACTION_CLEARANCE)

  for (const player of [...players].sort((a, b) => a.playerId - b.playerId)) {
    const original = player.color
    // A pinned colour wins outright: it is a contract with the viewer, not a
    // preference, so it is never displaced by a clearance check.
    const pinned = PINNED.get(original.toLowerCase())
    if (pinned !== undefined) {
      colorByPlayer.set(player.playerId, pinned)
      taken.push(pinned)
      if (pinned !== original) {
        substitutions.push({
          playerId: player.playerId, nation: player.nation, from: original, to: pinned,
        })
      }
      continue
    }
    if (nearestTerrainDistance(original) >= TERRAIN_CLEARANCE && clearsTaken(original)) {
      colorByPlayer.set(player.playerId, original)
      taken.push(original)
      continue
    }
    const replacement = SUBSTITUTES.find(
      (candidate) =>
        nearestTerrainDistance(candidate) >= TERRAIN_CLEARANCE && clearsTaken(candidate),
    ) ?? original
    colorByPlayer.set(player.playerId, replacement)
    taken.push(replacement)
    if (replacement !== original) {
      substitutions.push({
        playerId: player.playerId,
        nation: player.nation,
        from: original,
        to: replacement,
      })
    }
  }
  return { colorByPlayer, substitutions }
}
