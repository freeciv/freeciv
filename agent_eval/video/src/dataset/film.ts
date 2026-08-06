/**
 * Turns the raw export into the shape the film renders from: one fully
 * resolved board state per turn plus score timelines for the side panel.
 *
 * The exporter ships terrain and infrastructure only when they change, and
 * omits the board entirely for turns with no readable autosave. Resolution
 * here is a single forward pass that carries the last known layer forward, so
 * a component never has to reason about gaps.
 */

import { planFactionColors, type FactionColorPlan } from '../faction-color'
import type {
  CityTuple, DatasetMeta, EventLog, PlayerEntry, PlayerStat, RawFrame, UnitTuple,
} from './schema'

/** Owner id per tile, `-1` for unclaimed. Indexed `y * width + x`. */
export type OwnerGrid = Int16Array

export interface TurnState {
  readonly turn: number
  readonly year: number
  /** True when this turn had no autosave and reuses the previous board. */
  readonly interpolated: boolean
  readonly boardTurn: number | null
  readonly terrain: readonly string[]
  readonly owners: OwnerGrid
  readonly infrastructure: readonly string[]
  readonly cities: readonly CityTuple[]
  readonly units: readonly UnitTuple[]
  readonly cityNames: ReadonlyMap<string, string>
  readonly statsByPlayer: ReadonlyMap<number, PlayerStat>
  /** Tiles claimed by each player this turn, for the territory share bar. */
  readonly territoryByPlayer: ReadonlyMap<number, number>
}

export interface PlayerTrack {
  readonly player: PlayerEntry
  /** The colour the film draws this faction with; see `faction-color.ts`. */
  readonly renderColor: string
  readonly scores: readonly number[]
  readonly cities: readonly number[]
  readonly units: readonly number[]
  readonly techs: readonly number[]
  readonly peakScore: number
  readonly finalScore: number
  readonly everAlive: boolean
}

export interface Film {
  readonly meta: DatasetMeta
  readonly turns: readonly TurnState[]
  readonly tracks: readonly PlayerTrack[]
  readonly seatTracks: readonly PlayerTrack[]
  readonly colors: FactionColorPlan
  readonly events: EventLog
  readonly maxScore: number
  readonly landTiles: number
}

function decodeOwnerRow(row: string, width: number, into: OwnerGrid, offset: number): void {
  let cursor = 0
  for (const run of row.split(',')) {
    const separator = run.lastIndexOf(':')
    if (separator < 0) throw new Error(`invalid territory run "${run}"`)
    const ownerText = run.slice(0, separator)
    const count = Number(run.slice(separator + 1))
    const owner = ownerText === '-' ? -1 : Number(ownerText)
    if (!Number.isSafeInteger(owner) || !Number.isSafeInteger(count) || count < 1) {
      throw new Error(`invalid territory run "${run}"`)
    }
    if (cursor + count > width) throw new Error('territory row exceeds board width')
    into.fill(owner, offset + cursor, offset + cursor + count)
    cursor += count
  }
  if (cursor !== width) throw new Error('territory row does not match board width')
}

function decodeOwners(rows: readonly string[], width: number, height: number): OwnerGrid {
  const grid = new Int16Array(width * height).fill(-1)
  for (let y = 0; y < height; y += 1) {
    const row = rows[y]
    if (row === undefined) throw new Error('territory board is missing a row')
    decodeOwnerRow(row, width, grid, y * width)
  }
  return grid
}

function tallyTerritory(owners: OwnerGrid): ReadonlyMap<number, number> {
  const counts = new Map<number, number>()
  for (const owner of owners) {
    if (owner < 0) continue
    counts.set(owner, (counts.get(owner) ?? 0) + 1)
  }
  return counts
}

const EMPTY_NAMES: ReadonlyMap<string, string> = new Map()

export function buildFilm(
  meta: DatasetMeta, frames: readonly RawFrame[], events: EventLog,
): Film {
  const { width, height } = meta
  let terrain: readonly string[] = []
  let infrastructure: readonly string[] = []
  let owners: OwnerGrid | null = null
  let cities: readonly CityTuple[] = []
  let units: readonly UnitTuple[] = []
  let cityNames: ReadonlyMap<string, string> = EMPTY_NAMES
  let territory: ReadonlyMap<number, number> = new Map()

  const turns: TurnState[] = []
  for (const frame of frames) {
    if (frame.terrain) terrain = frame.terrain
    if (frame.infrastructure) infrastructure = frame.infrastructure
    if (frame.owners) {
      owners = decodeOwners(frame.owners, width, height)
      territory = tallyTerritory(owners)
    }
    if (frame.cities) cities = frame.cities
    if (frame.units) units = frame.units
    if (frame.cityNames) cityNames = frame.cityNames
    turns.push({
      turn: frame.turn,
      year: frame.year,
      interpolated: frame.interpolated,
      boardTurn: frame.boardTurn,
      terrain,
      owners: owners ?? new Int16Array(width * height).fill(-1),
      infrastructure,
      cities,
      units,
      cityNames,
      statsByPlayer: new Map(frame.stats.map((stat) => [stat.playerId, stat])),
      territoryByPlayer: territory,
    })
  }

  // A leading gap before the first autosave would otherwise render an empty
  // world; back-fill it from the first resolved board.
  const firstResolved = turns.find((turn) => turn.terrain.length > 0)
  if (firstResolved) {
    for (let index = 0; index < turns.length; index += 1) {
      const turn = turns[index]
      if (turn === undefined || turn.terrain.length > 0) break
      turns[index] = {
        ...turn,
        terrain: firstResolved.terrain,
        infrastructure: firstResolved.infrastructure,
      }
    }
  }

  const colors = planFactionColors(meta.players)
  const tracks = meta.players.map((player): PlayerTrack => {
    const scores: number[] = []
    const cityCounts: number[] = []
    const unitCounts: number[] = []
    const techCounts: number[] = []
    let everAlive = false
    for (const turn of turns) {
      const stat = turn.statsByPlayer.get(player.playerId)
      scores.push(stat?.score ?? 0)
      cityCounts.push(stat?.cities ?? 0)
      unitCounts.push(stat?.units ?? 0)
      techCounts.push((stat?.techs ?? 0) + (stat?.futureTechs ?? 0))
      if (stat?.alive === true) everAlive = true
    }
    return {
      player,
      renderColor: colors.colorByPlayer.get(player.playerId) ?? player.color,
      scores,
      cities: cityCounts,
      units: unitCounts,
      techs: techCounts,
      peakScore: scores.reduce((best, score) => Math.max(best, score), 0),
      finalScore: scores[scores.length - 1] ?? 0,
      everAlive,
    }
  })

  const landCodes = new Set(
    meta.terrainCatalog
      .filter((entry) => !/ocean|lake/i.test(entry.name))
      .map((entry) => entry.code),
  )
  const lastTerrain = turns[turns.length - 1]?.terrain ?? []
  let landTiles = 0
  for (const row of lastTerrain) {
    for (const code of row) if (landCodes.has(code)) landTiles += 1
  }

  // An export whose manifest never named its seats would otherwise leave the
  // standings panel empty; fall back to every faction that actually scored.
  const seatTracks = tracks.filter((track) => track.player.seat)
  const contenders = seatTracks.length > 0
    ? seatTracks
    : tracks.filter((track) => track.peakScore > 0)

  return {
    meta,
    turns,
    tracks,
    seatTracks: contenders,
    colors,
    events,
    maxScore: tracks.reduce((best, track) => Math.max(best, track.peakScore), 1),
    landTiles: Math.max(1, landTiles),
  }
}

/** Linear read of a track value between two turn indices, for smooth counters. */
export function sampleTrack(
  values: readonly number[], index: number, progress: number,
): number {
  const current = values[index] ?? 0
  const next = values[index + 1] ?? current
  return current + (next - current) * progress
}
