/**
 * Render-time faction color substitution.
 *
 * Freeciv hands out nation colors with no knowledge of the map underneath them.
 * The agent seat's default blue (#0067A5) sits on top of the ocean palette and
 * the pirate goldenrod (#B8860B) sits on top of plains, so their territory
 * tints and their city and unit markers disappear into the board. This module
 * moves any faction color that lands too close to the terrain palette, or too
 * close to another faction already placed, onto one that cannot occur on a
 * Freeciv map.
 *
 * Presentation only: recorded replay data, the wire payloads, and every export
 * keep the color Freeciv assigned. The plan is deterministic — the same faction
 * set always yields the same plan — and it is the same algorithm, the same
 * constants, and the same pinned table as the offline film renderer in
 * `agent_eval/video/src/faction-color.ts`, so a match reads identically in a
 * preview tab and in the rendered video. Change one, change the other.
 *
 * Clearance is a property of the whole faction set, not of one color, so
 * callers build a palette once per game with `buildDisplayPalette` and paint
 * through it. `displayPlayerColor` without a palette still honors the pinned
 * table, which is all an isolated component can know on its own.
 */

import { LruCache } from './board-loader'
import { terrainColor } from './board-geometry'

/** Terrain fills a faction color must stay clear of. */
const BOARD_COLORS: readonly string[] = [
  'Deep Ocean', 'Ocean', 'Lake', 'Glacier', 'Desert', 'Forest', 'Grassland',
  'Hills', 'Jungle', 'Mountains', 'Plains', 'Swamp', 'Tundra',
].map(terrainColor)

/**
 * Fixed substitutions that hold whatever the clearance search below would
 * otherwise pick, applied before any algorithmic step so no future change to
 * the substitute pool or the constants can displace them.
 *
 * This table mirrors `PINNED` in `agent_eval/video/src/faction-color.ts`. An
 * entry here that is missing there means a match reads differently in the two
 * surfaces. Periwinkle clears every terrain by a wide margin (0.181 to its
 * nearest, tundra) and sits at least 0.23 from every other faction color in use.
 *
 * The barbarian/pirate salmon is pinned for a different reason: it sits only
 * 0.087 from the default orange, fails the film's faction clearance, and so
 * fell through to the first substitute -- aqua mint, which made a raiding
 * third party read as a cool, calm fourth empire. Crimson says what they are
 * and holds up: 0.213 from its nearest terrain, 0.185 from that orange.
 */
const PINNED: ReadonlyMap<string, string> = new Map([
  ['#0067a5', '#A78BFA'], // agent blue -> periwinkle violet
  ['#fa8072', '#E01B24'], // barbarian salmon -> crimson
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
 * OKLab clearances, calibrated against the actual Freeciv seat colors.
 *
 * The agent's blue sits 0.065 from ocean and the pirate goldenrod 0.065 from
 * plains (0.087 from desert, its second nearest), which is what makes both
 * vanish; the default orange sits 0.093 from desert, which reads fine. 0.085
 * separates exactly those cases.
 * Faction-to-faction needs a wider margin: the default orange and the barbarian
 * salmon are only 0.087 apart from each other, and on a busy board that is not
 * enough to tell two empires apart.
 */
const TERRAIN_CLEARANCE = 0.085
const FACTION_CLEARANCE = 0.15

interface Oklab {
  readonly l: number
  readonly a: number
  readonly b: number
}

interface Rgb {
  readonly r: number
  readonly g: number
  readonly b: number
}

/** Mirrors the film's parser, including its neutral fallback, so identical
 *  inputs give identical distances in both surfaces. */
function parseHexColor(color: string): Rgb {
  const hex = color.trim().replace('#', '')
  const expanded = hex.length === 3
    ? hex.split('').map((character) => character + character).join('')
    : hex
  const value = Number.parseInt(expanded.slice(0, 6), 16)
  if (!Number.isFinite(value)) return { r: 140, g: 148, b: 156 }
  return {
    r: (value >> 16) & 0xff,
    g: (value >> 8) & 0xff,
    b: value & 0xff,
  }
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

export function nearestTerrainDistance(color: string): number {
  return BOARD_COLORS.reduce(
    (best, terrain) => Math.min(best, colorDistance(color, terrain)),
    Number.POSITIVE_INFINITY,
  )
}

/** One faction's recorded identity: the id that orders the plan, and the color
 *  Freeciv recorded for it. */
export interface DisplayFaction {
  readonly playerId: number
  readonly color: string | null | undefined
}

export interface DisplaySubstitution {
  readonly playerId: number
  readonly from: string
  readonly to: string
}

export interface DisplayPalette {
  /** Recorded color, lowercased, to the color the viewer paints. */
  readonly byRecordedColor: ReadonlyMap<string, string>
  /** Only the factions that were actually moved, in player order. */
  readonly substitutions: readonly DisplaySubstitution[]
}

/** What a component paints with before its game's faction set is known. The
 *  pinned table still applies; only clearance needs the whole set. */
export const EMPTY_DISPLAY_PALETTE: DisplayPalette = {
  byRecordedColor: new Map(),
  substitutions: [],
}

const paletteCache = new LruCache<DisplayPalette>(16)

function paletteKey(factions: readonly DisplayFaction[]): string {
  return factions
    .map((faction) => `${faction.playerId}:${(faction.color ?? '').toLowerCase()}`)
    .join('|')
}

/**
 * Decide a paint color per faction, in player order so the result is stable.
 *
 * A faction keeps its recorded color unless it is too close to the terrain
 * palette or to a faction already placed. Substitutes are taken in order,
 * skipping any that would collide with a color already in play. Identical
 * faction sets are memoized, so callers may rebuild the input array freely.
 *
 * The returned map is keyed by recorded color rather than by player id, because
 * the viewer paints from several shapes that do not all carry a player id (a
 * configured place knows its seat, not its id). Should two factions somehow
 * record the same color, the lower player id decides what that color paints as.
 */
export function buildDisplayPalette(factions: readonly DisplayFaction[]): DisplayPalette {
  const ordered = [...factions].sort((left, right) => left.playerId - right.playerId)
  const key = paletteKey(ordered)
  const cached = paletteCache.get(key)
  if (cached) return cached

  const byRecordedColor = new Map<string, string>()
  const substitutions: DisplaySubstitution[] = []
  const taken: string[] = []

  const clearsTaken = (candidate: string): boolean =>
    taken.every((used) => colorDistance(candidate, used) >= FACTION_CLEARANCE)

  for (const faction of ordered) {
    const original = faction.color
    if (!original) continue
    const recorded = original.toLowerCase()
    // A faction already seen under this exact color keeps that decision, so the
    // plan stays a function of the set and not of iteration order.
    if (byRecordedColor.has(recorded)) continue

    // A pinned color wins outright: it is a contract with the film, not a
    // preference, so it is never displaced by a clearance check.
    const pinned = PINNED.get(recorded)
    const chosen = pinned
      ?? (nearestTerrainDistance(original) >= TERRAIN_CLEARANCE && clearsTaken(original)
        ? original
        : SUBSTITUTES.find(
          (candidate) =>
            nearestTerrainDistance(candidate) >= TERRAIN_CLEARANCE && clearsTaken(candidate),
        ) ?? original)

    byRecordedColor.set(recorded, chosen)
    taken.push(chosen)
    if (chosen !== original) {
      substitutions.push({ playerId: faction.playerId, from: original, to: chosen })
    }
  }

  const palette: DisplayPalette = { byRecordedColor, substitutions }
  paletteCache.set(key, palette)
  return palette
}

/**
 * Returns the color to paint for a recorded player color.
 *
 * With a palette, the game's whole plan applies. Without one, only the pinned
 * table does — every other color passes through byte for byte. An absent color
 * stays absent so callers keep deciding their own neutral fallback.
 */
export function displayPlayerColor(
  color: string | null | undefined,
  palette?: DisplayPalette | null,
): string | null {
  if (!color) return null
  const recorded = color.trim().toLowerCase()
  return palette?.byRecordedColor.get(recorded) ?? PINNED.get(recorded) ?? color
}
