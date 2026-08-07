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
 * A four-step achromatic ladder plus two text steps. The chrome is deliberately
 * colourless: the only saturation in a frame belongs to the factions and to the
 * board, so a faction colour never has to compete with the furniture for
 * attention. `amber` and `red` survive as semantics -- a held board, a dead
 * empire -- and are never spent on decoration.
 */
export const SHELL = {
  page: '#0a0c0d',
  panel: '#101416',
  panelRaised: '#171b1e',
  line: '#272d31',
  ink: '#eef2f3',
  muted: '#7e888f',
  /** The quietest legible step: axis ticks, secondary chrome. */
  dim: '#565f66',
  amber: '#dfa53f',
  red: '#e0665d',
  board: '#0a1c1f',
  boardEdge: '#1e3a41',
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
