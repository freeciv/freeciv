/**
 * Arena palette as plain strings, for the canvas painter and for the runtime
 * alpha blends that Tailwind cannot express. DOM styling uses the same values
 * as theme tokens in `tailwind.css` -- change one and change the other.
 *
 * Terrain and shell colours are lifted verbatim from the replay viewer
 * (`agent_eval/viewer/src/board-geometry.ts` and `index.css`) so a rendered
 * film and a live viewer tab read as one product.
 */

/**
 * A four-step near-achromatic ladder plus two text steps. The chrome is
 * deliberately colourless: the only saturation in a frame belongs to the
 * factions and to the board, so a faction colour never has to compete with the
 * furniture for attention. `amber` and `red` survive as semantics -- a held
 * board, a dead empire -- and are never spent on decoration.
 *
 * The film is a light surface, matching the viewer: the page is a warm grey
 * and even the raised panel stops short of white, because the one thing on
 * screen that is allowed to be dark is the board. `amber` and `red` are the
 * viewer's light-tuned pair, not the dark ones -- the dark values are mixed
 * for near-black and go chalky on paper.
 */
export const SHELL = {
  page: '#eceae8',
  panel: '#f6f5f3',
  panelRaised: '#e4e1dd',
  line: '#dcd8d4',
  ink: '#1a1917',
  muted: '#6d6862',
  /** The quietest legible step: axis ticks, secondary chrome. */
  dim: '#97918a',
  amber: '#855e05',
  red: '#bd382c',
  /** The ink of the dithered ground -- one step under the page, no more. */
  ditherInk: '#d7d3ce',
  board: '#0a1c1f',
  boardEdge: '#1e3a41',

  /*
   * The board's own surface palette. Anything drawn ON the map -- the event
   * caption, city labels -- belongs to the board, not to the page, and has to
   * stay dark however light the page gets. When the film flipped to a light
   * ladder the caption followed the page and became a bright slab punched
   * through the middle of a dark map; these exist so it cannot happen again.
   */
  boardPanel: '#0e262b',
  boardInk: '#eef2f3',
  boardMuted: '#8ea3a7',
} as const

/** Terrain fill, matching the viewer's `terrainColor`. */
export function terrainColor(name: string): string {
  const normalized = name.toLowerCase()
  if (normalized.includes('deep ocean')) return '#194c62'
  if (normalized.includes('ocean')) return '#277086'
  if (normalized.includes('lake')) return '#3d8192'
  if (normalized.includes('glacier') || normalized.includes('inaccessible')) return '#e1e5dc'
  if (normalized.includes('desert')) return '#c7a161'
  if (normalized.includes('forest')) return '#42774b'
  if (normalized.includes('grass')) return '#78965e'
  if (normalized.includes('hill')) return '#8c8255'
  if (normalized.includes('jungle')) return '#346640'
  if (normalized.includes('mountain')) return '#8d8c82'
  if (normalized.includes('plain')) return '#9e965b'
  if (normalized.includes('swamp')) return '#526f5c'
  if (normalized.includes('tundra')) return '#98a092'
  return '#788072'
}

export function isWaterTerrain(name: string): boolean {
  return /ocean|lake/i.test(name)
}

interface Rgb {
  readonly r: number
  readonly g: number
  readonly b: number
}

export function parseHexColor(color: string): Rgb {
  const hex = color.replace('#', '')
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

export function withAlpha(color: string, alpha: number): string {
  const { r, g, b } = parseHexColor(color)
  return `rgba(${r},${g},${b},${alpha})`
}

export function mixColors(from: string, to: string, amount: number): string {
  const a = parseHexColor(from)
  const b = parseHexColor(to)
  const blend = (left: number, right: number): number =>
    Math.round(left + (right - left) * amount)
  return `rgb(${blend(a.r, b.r)},${blend(a.g, b.g)},${blend(a.b, b.b)})`
}
