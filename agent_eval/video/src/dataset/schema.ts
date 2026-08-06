/**
 * Types and narrowing parsers for the dataset written by
 * `python -m agent_eval.video_export`.
 *
 * Everything crossing the JSON boundary arrives as `unknown` and is narrowed
 * by hand here, so no `any` and no unchecked cast reaches a component. A
 * malformed export fails loudly at load time instead of rendering a blank
 * panel for ninety seconds.
 */

export interface TerrainEntry {
  readonly code: string
  readonly name: string
}

export interface PlayerEntry {
  readonly playerId: number
  readonly seatId: string | null
  readonly seat: boolean
  readonly name: string
  readonly nation: string
  readonly color: string
  readonly controllerLabel: string | null
  readonly controllerType: string | null
  readonly model: string | null
}

export interface DatasetMeta {
  readonly schemaVersion: number
  readonly gameId: string
  readonly controlProtocol: string
  readonly ruleset: string
  readonly objective: string
  readonly state: string
  readonly status: string
  readonly error: string | null
  readonly seeds: readonly number[]
  readonly startedAt: number | null
  readonly finishedAt: number | null
  readonly width: number
  readonly height: number
  readonly topology: string
  readonly wrap: string
  readonly terrainCatalog: readonly TerrainEntry[]
  readonly infrastructureBits: Readonly<Record<string, number>>
  readonly players: readonly PlayerEntry[]
  readonly firstTurn: number
  readonly lastTurn: number
  readonly frameCount: number
  readonly boardTurnCount: number
  readonly interpolatedTurnCount: number
  readonly boardDensity: number
}

/** `[x, y, playerId, size, capital]` */
export type CityTuple = readonly [number, number, number, number, number]
/** `[x, y, playerId, count]` */
export type UnitTuple = readonly [number, number, number, number]

export interface PlayerStat {
  readonly playerId: number
  readonly alive: boolean
  readonly government: string
  readonly techs: number
  readonly futureTechs: number
  readonly researching: string
  readonly bulbs: number
  readonly score: number
  readonly cities: number
  readonly citizens: number
  readonly population: number
  readonly units: number
  readonly gold: number
  readonly culture: number
}

export interface RawFrame {
  readonly turn: number
  readonly year: number
  readonly boardTurn: number | null
  readonly interpolated: boolean
  readonly terrain: readonly string[] | null
  readonly owners: readonly string[] | null
  readonly infrastructure: readonly string[] | null
  readonly cities: readonly CityTuple[] | null
  readonly units: readonly UnitTuple[] | null
  readonly cityNames: ReadonlyMap<string, string> | null
  readonly stats: readonly PlayerStat[]
}

class DatasetError extends Error {
  constructor(message: string) {
    super(`freeciv video dataset: ${message}`)
    this.name = 'DatasetError'
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}

function record(value: unknown, where: string): Record<string, unknown> {
  if (!isRecord(value)) throw new DatasetError(`${where} must be an object`)
  return value
}

function array(value: unknown, where: string): readonly unknown[] {
  if (!Array.isArray(value)) throw new DatasetError(`${where} must be an array`)
  return value
}

function integer(value: unknown, where: string): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    throw new DatasetError(`${where} must be a number`)
  }
  return value
}

function integerOr(value: unknown, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback
}

function text(value: unknown, fallback = ''): string {
  return typeof value === 'string' ? value : fallback
}

function nullableText(value: unknown): string | null {
  return typeof value === 'string' && value.length > 0 ? value : null
}

function stringRows(value: unknown, where: string): readonly string[] {
  return array(value, where).map((row, index) => {
    if (typeof row !== 'string') throw new DatasetError(`${where}[${index}] must be a string`)
    return row
  })
}

function optionalStringRows(value: unknown, where: string): readonly string[] | null {
  return value === null || value === undefined ? null : stringRows(value, where)
}

function numberTuple(value: unknown, length: number, where: string): readonly number[] {
  const entries = array(value, where)
  if (entries.length !== length) {
    throw new DatasetError(`${where} must hold ${length} numbers`)
  }
  return entries.map((entry, index) => integer(entry, `${where}[${index}]`))
}

function parseCities(value: unknown): readonly CityTuple[] | null {
  if (value === null || value === undefined) return null
  return array(value, 'frame.cities').map((entry, index) => {
    const [x, y, playerId, size, capital] = numberTuple(entry, 5, `frame.cities[${index}]`)
    return [x ?? 0, y ?? 0, playerId ?? 0, size ?? 0, capital ?? 0] as const
  })
}

function parseUnits(value: unknown): readonly UnitTuple[] | null {
  if (value === null || value === undefined) return null
  return array(value, 'frame.units').map((entry, index) => {
    const [x, y, playerId, count] = numberTuple(entry, 4, `frame.units[${index}]`)
    return [x ?? 0, y ?? 0, playerId ?? 0, count ?? 0] as const
  })
}

function parseCityNames(value: unknown): ReadonlyMap<string, string> | null {
  if (value === null || value === undefined) return null
  const source = record(value, 'frame.city_names')
  const names = new Map<string, string>()
  for (const [key, name] of Object.entries(source)) {
    if (typeof name === 'string') names.set(key, name)
  }
  return names
}

function parseStat(value: unknown, where: string): PlayerStat {
  const source = record(value, where)
  return {
    playerId: integer(source['player_id'], `${where}.player_id`),
    alive: source['alive'] !== false,
    government: text(source['government']),
    techs: integerOr(source['techs'], 0),
    futureTechs: integerOr(source['future_techs'], 0),
    researching: text(source['researching']),
    bulbs: integerOr(source['bulbs'], 0),
    score: integerOr(source['score'], 0),
    cities: integerOr(source['cities'], 0),
    citizens: integerOr(source['citizens'], 0),
    population: integerOr(source['population'], 0),
    units: integerOr(source['units'], 0),
    gold: integerOr(source['gold'], 0),
    culture: integerOr(source['culture'], 0),
  }
}

function parsePlayer(value: unknown, where: string): PlayerEntry {
  const source = record(value, where)
  const seatId = nullableText(source['seat_id'])
  return {
    playerId: integer(source['player_id'], `${where}.player_id`),
    seatId,
    seat: source['seat'] === true || (seatId?.startsWith('place-') ?? false),
    name: text(source['name'], 'Unknown'),
    nation: text(source['nation'], 'Unknown'),
    color: text(source['color'], '#8a949c'),
    controllerLabel: nullableText(source['controller_label']),
    controllerType: nullableText(source['controller_type']),
    model: nullableText(source['model']),
  }
}

export function parseMeta(value: unknown): DatasetMeta {
  const source = record(value, 'meta.json')
  const bits: Record<string, number> = {}
  for (const [key, bit] of Object.entries(record(source['infrastructure_bits'] ?? {}, 'meta.infrastructure_bits'))) {
    if (typeof bit === 'number' && Number.isFinite(bit)) bits[key] = bit
  }
  const width = integer(source['width'], 'meta.width')
  const height = integer(source['height'], 'meta.height')
  if (width < 1 || height < 1) throw new DatasetError('meta board dimensions are empty')
  return {
    schemaVersion: integerOr(source['schema_version'], 1),
    gameId: text(source['game_id'], 'unknown-game'),
    controlProtocol: text(source['control_protocol'], 'unknown'),
    ruleset: text(source['ruleset'], 'unknown'),
    objective: text(source['objective']),
    state: text(source['state'], 'unknown'),
    status: text(source['status'], 'unknown'),
    error: nullableText(source['error']),
    seeds: array(source['seeds'] ?? [], 'meta.seeds')
      .filter((seed): seed is number => typeof seed === 'number'),
    startedAt: typeof source['started_at'] === 'number' ? source['started_at'] : null,
    finishedAt: typeof source['finished_at'] === 'number' ? source['finished_at'] : null,
    width,
    height,
    topology: text(source['topology']),
    wrap: text(source['wrap']),
    terrainCatalog: array(source['terrain_catalog'] ?? [], 'meta.terrain_catalog')
      .map((entry, index) => {
        const terrain = record(entry, `meta.terrain_catalog[${index}]`)
        return { code: text(terrain['code']), name: text(terrain['name']) }
      }),
    infrastructureBits: bits,
    players: array(source['players'] ?? [], 'meta.players')
      .map((entry, index) => parsePlayer(entry, `meta.players[${index}]`)),
    firstTurn: integerOr(source['first_turn'], 1),
    lastTurn: integerOr(source['last_turn'], 1),
    frameCount: integerOr(source['frame_count'], 0),
    boardTurnCount: integerOr(source['board_turn_count'], 0),
    interpolatedTurnCount: integerOr(source['interpolated_turn_count'], 0),
    boardDensity: integerOr(source['board_density'], 1),
  }
}

export function parseFrames(value: unknown): readonly RawFrame[] {
  const source = record(value, 'frames.json')
  const entries = array(source['frames'], 'frames.frames')
  if (entries.length === 0) throw new DatasetError('frames.json holds no turns')
  return entries.map((entry, index) => {
    const frame = record(entry, `frames[${index}]`)
    const boardTurn = frame['board_turn']
    return {
      turn: integer(frame['turn'], `frames[${index}].turn`),
      year: integerOr(frame['year'], 0),
      boardTurn: typeof boardTurn === 'number' ? boardTurn : null,
      interpolated: frame['interpolated'] === true,
      terrain: optionalStringRows(frame['terrain'], `frames[${index}].terrain`),
      owners: optionalStringRows(frame['owners'], `frames[${index}].owners`),
      infrastructure: optionalStringRows(
        frame['infrastructure'], `frames[${index}].infrastructure`,
      ),
      cities: parseCities(frame['cities']),
      units: parseUnits(frame['units']),
      cityNames: parseCityNames(frame['city_names']),
      stats: array(frame['stats'] ?? [], `frames[${index}].stats`)
        .map((stat, statIndex) => parseStat(stat, `frames[${index}].stats[${statIndex}]`)),
    }
  })
}
