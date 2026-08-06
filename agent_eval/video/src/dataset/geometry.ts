/**
 * Freeciv ISO|HEX board geometry, ported from the arena viewer's
 * `board-geometry.ts` so the film draws exactly the lattice the viewer draws.
 *
 * Tile centres come from `nativeTilePosition`. The six neighbours of a tile sit
 * at world offsets (+/-1, 0) and (+/-0.5, +/-sqrt(3)/2), i.e. a regular hex
 * lattice with unit centre spacing, so each tile is a hexagon of circumradius
 * 1/sqrt(3) whose vertices start at 30 degrees.
 */

export const HEX_CIRCUMRADIUS = 1 / Math.sqrt(3)
export const HEX_VERTEX_PHASE = Math.PI / 6

/**
 * The board's long axis runs at -30 degrees in native coordinates. Rotating the
 * whole lattice back by +30 degrees turns the diamond into an axis-aligned
 * rectangle, which fits a landscape frame far better while keeping every tile
 * adjacency and shape identical.
 */
export const FIT_ROTATION = Math.PI / 6

export interface Point {
  readonly x: number
  readonly y: number
}

export interface Rect {
  readonly minX: number
  readonly minY: number
  readonly maxX: number
  readonly maxY: number
}

/** World-space centre of a native `(x, y)` tile, matching the viewer exactly. */
export function nativeTilePosition(x: number, y: number, width: number): Point {
  const mapX = x + Math.ceil(y / 2)
  const mapY = y - mapX + width
  return { x: mapX - mapY / 2, y: (Math.sqrt(3) / 2) * mapY }
}

export function rotatePoint(point: Point, radians: number): Point {
  const cos = Math.cos(radians)
  const sin = Math.sin(radians)
  return {
    x: point.x * cos - point.y * sin,
    y: point.x * sin + point.y * cos,
  }
}

/** Native neighbour coordinates in the viewer's canonical direction order. */
const ISO_HEX_NEIGHBORS: readonly Point[] = [
  { x: 0, y: -1 },
  { x: 1, y: 0 },
  { x: 1, y: 1 },
  { x: 0, y: 1 },
  { x: -1, y: 0 },
  { x: -1, y: -1 },
]

export function isoHexNeighborCoordinates(x: number, y: number): readonly Point[] {
  const mapX = x + Math.ceil(y / 2)
  return ISO_HEX_NEIGHBORS.map((offset) => {
    const nativeY = y + offset.x + offset.y
    return { x: mapX + offset.x - Math.ceil(nativeY / 2), y: nativeY }
  })
}

export interface BoardLayout {
  readonly width: number
  readonly height: number
  readonly rotation: number
  /** Rotated world-space centre of every tile, indexed `y * width + x`. */
  readonly centers: Float64Array
  readonly bounds: Rect
}

export function buildBoardLayout(
  width: number, height: number, rotation: number = FIT_ROTATION,
): BoardLayout {
  const centers = new Float64Array(width * height * 2)
  let minX = Number.POSITIVE_INFINITY
  let minY = Number.POSITIVE_INFINITY
  let maxX = Number.NEGATIVE_INFINITY
  let maxY = Number.NEGATIVE_INFINITY
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const point = rotatePoint(nativeTilePosition(x, y, width), rotation)
      const index = (y * width + x) * 2
      centers[index] = point.x
      centers[index + 1] = point.y
      if (point.x < minX) minX = point.x
      if (point.x > maxX) maxX = point.x
      if (point.y < minY) minY = point.y
      if (point.y > maxY) maxY = point.y
    }
  }
  const margin = HEX_CIRCUMRADIUS
  return {
    width,
    height,
    rotation,
    centers,
    bounds: {
      minX: minX - margin,
      minY: minY - margin,
      maxX: maxX + margin,
      maxY: maxY + margin,
    },
  }
}

export interface FitTransform {
  readonly scale: number
  readonly offsetX: number
  readonly offsetY: number
  readonly renderWidth: number
  readonly renderHeight: number
}

/** Contain-fit the rotated board inside a box, centred. */
export function fitBoard(
  layout: BoardLayout, boxWidth: number, boxHeight: number,
): FitTransform {
  const worldWidth = layout.bounds.maxX - layout.bounds.minX
  const worldHeight = layout.bounds.maxY - layout.bounds.minY
  const scale = Math.min(boxWidth / worldWidth, boxHeight / worldHeight)
  const renderWidth = worldWidth * scale
  const renderHeight = worldHeight * scale
  return {
    scale,
    offsetX: (boxWidth - renderWidth) / 2 - layout.bounds.minX * scale,
    offsetY: (boxHeight - renderHeight) / 2 - layout.bounds.minY * scale,
    renderWidth,
    renderHeight,
  }
}

export function tileCenter(layout: BoardLayout, x: number, y: number): Point {
  const index = (y * layout.width + x) * 2
  return { x: layout.centers[index] ?? 0, y: layout.centers[index + 1] ?? 0 }
}

/** Screen-space hexagon vertices for one tile, ready for `ctx.moveTo`/`lineTo`. */
export function hexPath(
  centerX: number, centerY: number, radius: number, rotation: number,
): readonly Point[] {
  const points: Point[] = []
  for (let corner = 0; corner < 6; corner += 1) {
    const angle = rotation + HEX_VERTEX_PHASE + (corner * Math.PI) / 3
    points.push({
      x: centerX + radius * Math.cos(angle),
      y: centerY + radius * Math.sin(angle),
    })
  }
  return points
}
