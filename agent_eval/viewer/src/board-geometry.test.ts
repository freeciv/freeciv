import { describe, expect, it } from 'vitest'
import {
  buildBoardTiles,
  decodeExtraIds,
  decodeOwnerRow,
  isoHexNeighborCoordinates,
  nativeTilePosition,
  ownershipBoundaryEdges,
} from './board-geometry'
import type { BoardResponse } from './types'

const board: BoardResponse = {
  schema_version: 1,
  game_id: 'game_abcdefghijklmnop',
  turn: 7,
  width: 3,
  height: 2,
  topology: 'ISO|HEX',
  wrap: 'WRAPX',
  terrain_catalog: [
    { code: ' ', name: 'Ocean' },
    { code: 'd', name: 'Desert' },
    { code: 'g', name: 'Grassland' },
  ],
  terrain_rows: ['g d', 'ddg'],
  altitude_rows: ['10,0,20', '30,40,50'],
  owner_rows: ['0:2,-:1', '1:2,0:1'],
  extras_catalog: [
    { id: 0, name: 'Irrigation' },
    { id: 1, name: 'Road' },
    { id: 2, name: 'River' },
    { id: 3, name: 'Gold' },
    { id: 4, name: 'Fish' },
  ],
  extra_layers: [['124', '800'], ['100', '010']],
  cities: [],
  unit_stacks: [],
  players: [],
}

describe('semantic board geometry', () => {
  it('decodes compact territory rows exactly', () => {
    expect(decodeOwnerRow('0:2,-:1,12:2', 5)).toEqual([0, 0, null, 12, 12])
    expect(() => decodeOwnerRow('0:6', 5)).toThrow('exceeds board width')
  })

  it('decodes extras with Freeciv nibble bit order', () => {
    expect(decodeExtraIds(board, 0, 0)).toEqual([0, 4])
    expect(decodeExtraIds(board, 1, 0)).toEqual([1])
    expect(decodeExtraIds(board, 2, 0)).toEqual([2])
  })

  it('builds one unique native ISO|HEX transform per tile', () => {
    const tiles = buildBoardTiles(board)
    expect(tiles).toHaveLength(6)
    expect(new Set(tiles.map((tile) => `${tile.worldX}:${tile.worldZ}`))).toHaveLength(6)
    expect(tiles[1]).toMatchObject({ terrainName: 'Ocean', ownerId: 0, altitude: 0 })
    expect(nativeTilePosition(2, 1, 3)).toEqual({ x: 2.5, z: Math.sqrt(3) / 2 })
  })

  it('rejects unsupported topology instead of drawing misleading geometry', () => {
    expect(() => buildBoardTiles({ ...board, topology: 'WRAPX' })).toThrow('Unsupported semantic board topology')
  })

  it('emits only ownership edges that meet another owner or unowned space', () => {
    const edges = ownershipBoundaryEdges(board)
    const sharedOwnedEdge = edges.filter((edge) => edge.neighborOwnerId !== null)
    expect(sharedOwnedEdge.length).toBeGreaterThan(0)
    expect(new Set(sharedOwnedEdge.map((edge) => [edge.tileIndex, edge.neighborIndex].sort().join(':'))).size).toBe(sharedOwnedEdge.length)
    expect(edges.some((edge) => edge.tileIndex === 0 && edge.neighborIndex === 1)).toBe(false)
    expect(isoHexNeighborCoordinates(4, 5)).toEqual([
      { x: 5, z: 4 }, { x: 5, z: 6 }, { x: 4, z: 7 },
      { x: 4, z: 6 }, { x: 4, z: 4 }, { x: 4, z: 3 },
    ])
    const center = nativeTilePosition(4, 5, 20)
    isoHexNeighborCoordinates(4, 5).forEach((neighbor) => {
      const position = nativeTilePosition(neighbor.x, neighbor.z, 20)
      expect(Math.hypot(position.x - center.x, position.z - center.z)).toBeCloseTo(1)
    })
  })
})
