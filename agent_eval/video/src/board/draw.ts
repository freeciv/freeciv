/**
 * Canvas painter for one turn of the synthetic board.
 *
 * The drawing order mirrors the viewer's WebGL board: terrain hexes, political
 * territory tint, ownership boundary edges, infrastructure, then cities and
 * unit stacks on top. Fills are batched by colour, so a 54x72 world costs
 * roughly twenty path fills instead of eight thousand.
 */

import type { TurnState } from '../dataset/film'
import type { DatasetMeta } from '../dataset/schema'
import {
  HEX_CIRCUMRADIUS, hexPath, isoHexNeighborCoordinates, tileCenter,
  type BoardLayout, type FitTransform,
} from '../dataset/geometry'
import { SHELL, mixColors, terrainColor, withAlpha } from '../theme'

export interface BoardPalette {
  readonly colorByPlayer: ReadonlyMap<number, string>
  readonly terrainByCode: ReadonlyMap<string, string>
  readonly riverBit: number
  readonly railBit: number
  readonly roadBit: number
  readonly wrapX: boolean
}

/**
 * `colorByPlayer` comes from the film's render-colour plan, not straight from
 * the dataset, so territory and markers use the same substituted colour the
 * standings panel does.
 */
export function buildPalette(
  meta: DatasetMeta, colorByPlayer: ReadonlyMap<number, string>,
): BoardPalette {
  return {
    wrapX: /WRAPX/i.test(meta.wrap),
    colorByPlayer,
    terrainByCode: new Map(
      meta.terrainCatalog.map((entry) => [entry.code, entry.name]),
    ),
    riverBit: meta.infrastructureBits['river'] ?? 0,
    railBit: meta.infrastructureBits['railroad'] ?? 0,
    roadBit: meta.infrastructureBits['road'] ?? 0,
  }
}

interface Point {
  x: number
  y: number
}

function tracePath(context: CanvasRenderingContext2D, points: readonly Point[]): void {
  const first = points[0]
  if (!first) return
  context.moveTo(first.x, first.y)
  for (let index = 1; index < points.length; index += 1) {
    const point = points[index]
    if (point) context.lineTo(point.x, point.y)
  }
  context.closePath()
}

function readInfrastructure(rows: readonly string[], x: number, y: number): number {
  const row = rows[y]
  if (row === undefined) return 0
  const character = row[x]
  if (character === undefined) return 0
  const value = Number.parseInt(character, 16)
  return Number.isFinite(value) ? value : 0
}

export interface DrawOptions {
  readonly layout: BoardLayout
  readonly fit: FitTransform
  readonly palette: BoardPalette
  readonly turn: TurnState
  /** 0..1 fade applied to territory tint and markers, for the intro sweep. */
  readonly reveal: number
  readonly showLabels: boolean
  /** Backing-store multiplier, so stroke and label weight stay constant. */
  readonly uiScale: number
}

export function drawBoard(
  context: CanvasRenderingContext2D,
  width: number,
  height: number,
  options: DrawOptions,
): void {
  const { layout, fit, palette, turn, reveal, uiScale } = options
  const radius = HEX_CIRCUMRADIUS
  // Pixel-denominated sizes, expressed in the world units the transform expects.
  const px = (value: number): number => (value * uiScale) / fit.scale
  const toScreenX = (worldX: number): number => worldX * fit.scale + fit.offsetX
  const toScreenY = (worldY: number): number => worldY * fit.scale + fit.offsetY

  context.setTransform(1, 0, 0, 1, 0, 0)
  context.clearRect(0, 0, width, height)
  context.fillStyle = SHELL.board
  context.fillRect(0, 0, width, height)
  context.setTransform(fit.scale, 0, 0, fit.scale, fit.offsetX, fit.offsetY)
  context.lineJoin = 'round'

  const terrainBatches = new Map<string, Point[][]>()
  const territoryBatches = new Map<string, Point[][]>()
  const railTiles: Point[] = []
  const roadTiles: Point[] = []

  for (let y = 0; y < layout.height; y += 1) {
    const terrainRow = turn.terrain[y]
    if (terrainRow === undefined) continue
    for (let x = 0; x < layout.width; x += 1) {
      const code = terrainRow[x]
      if (code === undefined) continue
      const name = palette.terrainByCode.get(code) ?? ''
      const center = tileCenter(layout, x, y)
      const infrastructure = readInfrastructure(turn.infrastructure, x, y)
      const hasRiver = palette.riverBit !== 0 && (infrastructure & palette.riverBit) !== 0
      const fill = hasRiver
        ? mixColors(terrainColor(name), '#59a5bb', 0.28)
        : terrainColor(name)
      const hex = hexPath(center.x, center.y, radius, layout.rotation)
      const batch = terrainBatches.get(fill)
      if (batch) batch.push(hex as Point[])
      else terrainBatches.set(fill, [hex as Point[]])

      const owner = turn.owners[y * layout.width + x] ?? -1
      const isWater = /ocean|lake/i.test(name)
      const tint = owner >= 0
        ? palette.colorByPlayer.get(owner) ?? '#ffffff'
        : isWater ? null : '#445156'
      if (tint !== null) {
        const inner = hexPath(center.x, center.y, radius * 0.97, layout.rotation)
        const key = owner >= 0 ? tint : 'unowned'
        const territory = territoryBatches.get(key)
        if (territory) territory.push(inner as Point[])
        else territoryBatches.set(key, [inner as Point[]])
      }
      if (palette.railBit !== 0 && (infrastructure & palette.railBit) !== 0) {
        railTiles.push(center)
      } else if (palette.roadBit !== 0 && (infrastructure & palette.roadBit) !== 0) {
        roadTiles.push(center)
      }
    }
  }

  for (const [fill, hexes] of terrainBatches) {
    context.fillStyle = fill
    context.beginPath()
    for (const hex of hexes) tracePath(context, hex)
    context.fill()
  }

  for (const [key, hexes] of territoryBatches) {
    context.fillStyle = key === 'unowned'
      ? withAlpha('#445156', 0.3 * reveal)
      : withAlpha(key, 0.6 * reveal)
    context.beginPath()
    for (const hex of hexes) tracePath(context, hex)
    context.fill()
  }

  // Ownership boundaries: one segment on the shared edge between two tiles of
  // different allegiance, the viewer's contested edges in warm ivory.
  const boundaries = new Map<string, Point[][]>()
  for (let y = 0; y < layout.height; y += 1) {
    for (let x = 0; x < layout.width; x += 1) {
      const owner = turn.owners[y * layout.width + x] ?? -1
      if (owner < 0) continue
      const center = tileCenter(layout, x, y)
      const neighbors = isoHexNeighborCoordinates(x, y)
      for (const neighbor of neighbors) {
        const wrappedX = palette.wrapX
          ? ((neighbor.x % layout.width) + layout.width) % layout.width
          : neighbor.x
        const inside = neighbor.y >= 0 && neighbor.y < layout.height
          && wrappedX >= 0 && wrappedX < layout.width
        const neighborOwner = inside
          ? turn.owners[neighbor.y * layout.width + wrappedX] ?? -1
          : -1
        if (neighborOwner === owner) continue
        if (neighborOwner >= 0 && neighborOwner < owner) continue
        const target = inside ? tileCenter(layout, wrappedX, neighbor.y) : null
        let deltaX = target ? target.x - center.x : 0
        let deltaY = target ? target.y - center.y : 0
        const distance = Math.hypot(deltaX, deltaY)
        if (distance === 0 || distance > 1.6) continue
        deltaX /= distance
        deltaY /= distance
        const midX = center.x + deltaX * 0.5
        const midY = center.y + deltaY * 0.5
        const half = HEX_CIRCUMRADIUS / 2
        const segment: Point[] = [
          { x: midX - deltaY * half, y: midY + deltaX * half },
          { x: midX + deltaY * half, y: midY - deltaX * half },
        ]
        const color = neighborOwner >= 0
          ? '#fff1c9'
          : palette.colorByPlayer.get(owner) ?? '#f4e7c5'
        const batch = boundaries.get(color)
        if (batch) batch.push(segment)
        else boundaries.set(color, [segment])
      }
    }
  }
  context.lineCap = 'round'
  for (const [color, segments] of boundaries) {
    context.strokeStyle = withAlpha(color, 0.92 * reveal)
    context.lineWidth = px(color === '#fff1c9' ? 1.2 : 0.8)
    context.beginPath()
    for (const segment of segments) {
      const [from, to] = segment
      if (!from || !to) continue
      context.moveTo(from.x, from.y)
      context.lineTo(to.x, to.y)
    }
    context.stroke()
  }

  if (reveal > 0.2) {
    context.fillStyle = withAlpha('#b49b70', 0.4 * reveal)
    context.beginPath()
    for (const point of roadTiles) {
      context.moveTo(point.x + 0.09, point.y)
      context.arc(point.x, point.y, 0.09, 0, Math.PI * 2)
    }
    context.fill()
    context.fillStyle = withAlpha('#d9cbb2', 0.68 * reveal)
    context.beginPath()
    for (const point of railTiles) {
      context.moveTo(point.x + 0.13, point.y)
      context.arc(point.x, point.y, 0.13, 0, Math.PI * 2)
    }
    context.fill()
  }

  // Unit stacks read as small chevrons so they never compete with cities.
  const unitsByColor = new Map<string, Point[]>()
  for (const [x, y, playerId] of turn.units) {
    const color = palette.colorByPlayer.get(playerId) ?? '#f1ede4'
    const center = tileCenter(layout, x, y)
    const batch = unitsByColor.get(color)
    if (batch) batch.push(center)
    else unitsByColor.set(color, [center])
  }
  for (const [color, centers] of unitsByColor) {
    context.fillStyle = withAlpha(color, 0.95 * reveal)
    context.strokeStyle = withAlpha('#05090b', 0.85 * reveal)
    context.lineWidth = px(0.5)
    context.beginPath()
    for (const center of centers) {
      context.moveTo(center.x, center.y - 0.22)
      context.lineTo(center.x + 0.19, center.y + 0.14)
      context.lineTo(center.x - 0.19, center.y + 0.14)
      context.closePath()
    }
    context.fill()
    context.stroke()
  }

  for (const [x, y, playerId, size, capital] of turn.cities) {
    const color = palette.colorByPlayer.get(playerId) ?? '#e7dfce'
    const center = tileCenter(layout, x, y)
    const cityRadius = 0.28 + Math.min(24, size) * 0.013
    context.beginPath()
    context.arc(center.x, center.y, cityRadius, 0, Math.PI * 2)
    context.fillStyle = withAlpha(color, 0.98 * reveal)
    context.fill()
    context.lineWidth = px(0.9)
    context.strokeStyle = withAlpha('#060d10', 0.9 * reveal)
    context.stroke()
    context.beginPath()
    context.arc(center.x, center.y, cityRadius * 0.34, 0, Math.PI * 2)
    context.fillStyle = withAlpha('#fff8df', 0.95 * reveal)
    context.fill()
    if (capital === 1) {
      context.beginPath()
      context.arc(center.x, center.y, cityRadius + 0.16, 0, Math.PI * 2)
      context.lineWidth = px(1.1)
      context.strokeStyle = withAlpha('#fff0a2', 0.95 * reveal)
      context.stroke()
    }
  }

  // Labels are screen-space: they must not inherit the world transform.
  context.setTransform(1, 0, 0, 1, 0, 0)
  if (!options.showLabels) return
  context.textAlign = 'center'
  context.textBaseline = 'bottom'
  for (const [x, y, playerId, , capital] of turn.cities) {
    if (capital !== 1) continue
    const name = turn.cityNames.get(`${x},${y}`)
    if (!name) continue
    const center = tileCenter(layout, x, y)
    const screenX = toScreenX(center.x)
    const screenY = toScreenY(center.y) - 12 * uiScale
    context.font = `600 ${13 * uiScale}px ui-monospace, SFMono-Regular, Menlo, monospace`
    context.lineWidth = 3.5 * uiScale
    context.strokeStyle = withAlpha('#05090b', 0.85 * reveal)
    context.strokeText(name, screenX, screenY)
    context.fillStyle = withAlpha(
      palette.colorByPlayer.get(playerId) ?? SHELL.ink, reveal,
    )
    context.fillText(name, screenX, screenY)
  }
}
