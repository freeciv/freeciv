import type { BoardResponse } from './types'

export interface BoardTile {
  index: number
  x: number
  y: number
  worldX: number
  worldZ: number
  terrainCode: string
  terrainName: string
  altitude: number
  ownerId: number | null
  extraIds: number[]
}

export interface BoardPosition {
  x: number
  z: number
}

export interface BoardBoundaryEdge {
  direction: number
  neighborIndex: number | null
  neighborOwnerId: number | null
  ownerId: number
  tileIndex: number
  x: number
  y: number
}

const ISO_HEX_NEIGHBORS = [
  { x: 0, y: -1 },
  { x: 1, y: 0 },
  { x: 1, y: 1 },
  { x: 0, y: 1 },
  { x: -1, y: 0 },
  { x: -1, y: -1 },
] as const

export function nativeTilePosition(x: number, y: number, width: number): BoardPosition {
  const mapX = x + Math.ceil(y / 2)
  const mapY = y - mapX + width
  return {
    x: mapX - mapY / 2,
    z: (Math.sqrt(3) / 2) * mapY,
  }
}

export function decodeOwnerRow(row: string, width: number): (number | null)[] {
  const values: (number | null)[] = []
  for (const run of row.split(',')) {
    const match = run.match(/^(-|\d+):([1-9]\d*)$/)
    if (!match) throw new Error('Invalid territory row')
    const owner = match[1] === '-' ? null : Number(match[1])
    const count = Number(match[2])
    if (!Number.isSafeInteger(owner ?? 0) || !Number.isSafeInteger(count)) {
      throw new Error('Invalid territory row')
    }
    for (let index = 0; index < count; index += 1) values.push(owner)
    if (values.length > width) throw new Error('Territory row exceeds board width')
  }
  if (values.length !== width) throw new Error('Territory row does not match board width')
  return values
}

export function decodeAltitudeRow(row: string, width: number): number[] {
  const values = row.split(',').map(Number)
  if (
    values.length !== width
    || values.some((value) => !Number.isFinite(value) || !Number.isInteger(value))
  ) throw new Error('Invalid altitude row')
  return values
}

export function decodeExtraIds(
  board: Pick<BoardResponse, 'extra_layers' | 'extras_catalog'>,
  x: number,
  y: number,
): number[] {
  const result: number[] = []
  for (let layer = 0; layer < board.extra_layers.length; layer += 1) {
    const nibble = Number.parseInt(board.extra_layers[layer]?.[y]?.[x] ?? '', 16)
    if (!Number.isInteger(nibble)) throw new Error('Invalid board extra layer')
    for (let bit = 0; bit < 4; bit += 1) {
      const id = layer * 4 + bit
      if ((nibble & (1 << bit)) !== 0 && id < board.extras_catalog.length) result.push(id)
    }
  }
  return result
}

export function buildBoardTiles(board: BoardResponse): BoardTile[] {
  if (
    board.topology !== 'ISO|HEX'
    ||
    !Number.isInteger(board.width) || !Number.isInteger(board.height)
    || board.width < 1 || board.height < 1
    || board.terrain_rows.length !== board.height
    || board.altitude_rows.length !== board.height
    || board.owner_rows.length !== board.height
  ) throw new Error(board.topology === 'ISO|HEX' ? 'Invalid semantic board dimensions' : 'Unsupported semantic board topology')
  const terrainNames = new Map(board.terrain_catalog.map((terrain) => [terrain.code, terrain.name]))
  const result: BoardTile[] = []
  for (let y = 0; y < board.height; y += 1) {
    const terrainRow = board.terrain_rows[y]
    if (terrainRow.length !== board.width) throw new Error('Invalid semantic terrain row')
    const altitudes = decodeAltitudeRow(board.altitude_rows[y], board.width)
    const owners = decodeOwnerRow(board.owner_rows[y], board.width)
    for (let x = 0; x < board.width; x += 1) {
      const terrainCode = terrainRow[x]
      const terrainName = terrainNames.get(terrainCode)
      if (!terrainName) throw new Error('Unknown semantic terrain code')
      const position = nativeTilePosition(x, y, board.width)
      result.push({
        index: y * board.width + x,
        x,
        y,
        worldX: position.x,
        worldZ: position.z,
        terrainCode,
        terrainName,
        altitude: altitudes[x],
        ownerId: owners[x],
        extraIds: decodeExtraIds(board, x, y),
      })
    }
  }
  return result
}

function wrappedCoordinate(value: number, size: number, enabled: boolean): number | null {
  if (value >= 0 && value < size) return value
  if (!enabled) return null
  return ((value % size) + size) % size
}

export function isoHexNeighborCoordinates(x: number, y: number): BoardPosition[] {
  const mapX = x + Math.ceil(y / 2)
  return ISO_HEX_NEIGHBORS.map((offset) => {
    const nativeY = y + offset.x + offset.y
    return {
      x: mapX + offset.x - Math.ceil(nativeY / 2),
      z: nativeY,
    }
  })
}

export function ownershipBoundaryEdges(
  board: Pick<BoardResponse, 'height' | 'owner_rows' | 'width' | 'wrap'>,
): BoardBoundaryEdge[] {
  const owners = board.owner_rows.flatMap((row) => decodeOwnerRow(row, board.width))
  if (owners.length !== board.width * board.height) throw new Error('Invalid territory board')
  const wrapX = /WRAPX/i.test(board.wrap)
  const wrapY = /WRAPY/i.test(board.wrap)
  const edges: BoardBoundaryEdge[] = []
  for (let y = 0; y < board.height; y += 1) {
    for (let x = 0; x < board.width; x += 1) {
      const tileIndex = y * board.width + x
      const ownerId = owners[tileIndex]
      if (ownerId === null) continue
      isoHexNeighborCoordinates(x, y).forEach((neighbor, direction) => {
        const neighborX = wrappedCoordinate(neighbor.x, board.width, wrapX)
        const neighborY = wrappedCoordinate(neighbor.z, board.height, wrapY)
        const neighborIndex = neighborX === null || neighborY === null
          ? null
          : neighborY * board.width + neighborX
        const neighborOwnerId = neighborIndex === null ? null : owners[neighborIndex]
        if (neighborOwnerId === ownerId) return
        // A shared edge between two empires is emitted once. Edges against
        // unowned space remain owned by the bordering faction.
        if (neighborOwnerId !== null && neighborIndex !== null && tileIndex > neighborIndex) return
        edges.push({
          direction,
          neighborIndex,
          neighborOwnerId,
          ownerId,
          tileIndex,
          x,
          y,
        })
      })
    }
  }
  return edges
}

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

export function terrainLift(name: string): number {
  if (/mountain/i.test(name)) return 0.7
  if (/hill/i.test(name)) return 0.38
  if (/forest|jungle/i.test(name)) return 0.2
  if (isWaterTerrain(name)) return 0.02
  return 0.1
}
