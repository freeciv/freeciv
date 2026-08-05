export type BenchmarkValidity = boolean | null

export interface GamePlace {
  place: number
  seat_id: string
  player_name: string
  controller: 'agent' | 'native_classic_ai'
  joined: boolean
  controller_label?: string | null
  controller_type?: string | null
  model?: string | null
  player_color: string
}

export interface LeaderboardEntry {
  rank: number
  score: number
  score_turn?: number | null
  place: number
  seat_id: string
  player_name: string
  player_color: string
  controller_label: string
  controller_type: string
  model?: string | null
}

export interface MatchVictory {
  code: string
  label: string
  winners: string[]
  turn?: number | null
  year?: number | null
}

export interface MatchOutcome {
  status: string
  summary: string
  leaders?: string[]
  margin?: number | null
  score_turn?: number | null
  victory?: MatchVictory | null
}

export interface GameStatus {
  schema_version: number
  game_id: string
  state: string
  benchmark_valid: BenchmarkValidity
  mode: string
  places: number
  max_agents: number
  joined_agents: number
  turns: number
  current_turn: number | null
  objective: string
  timing_mode?: 'default' | 'blitz' | 'infinite' | 'custom' | null
  action_timeout_s?: number | null
  error: string | null
  invalid_reasons: string[]
  resolved_places: GamePlace[]
  leaderboard: LeaderboardEntry[]
  outcome: MatchOutcome
  replay_url?: string
}

export type GameSummary = Pick<GameStatus,
  | 'game_id'
  | 'state'
  | 'benchmark_valid'
  | 'mode'
  | 'places'
  | 'max_agents'
  | 'joined_agents'
  | 'turns'
  | 'current_turn'
  | 'resolved_places'
  | 'leaderboard'
  | 'outcome'
  | 'timing_mode'
  | 'action_timeout_s'
> & {
  created_at?: number
  watch_path?: string
}

export interface GamesIndexResponse {
  schema_version: number
  games: GameSummary[]
}

export interface TurnTimelineEntry {
  turn: number
  year?: number
  responded_seats?: string[]
  timed_out_seats?: string[]
  resolved_at?: number
}

export interface MapPlayer {
  player_id: number
  player_name: string
  player_color: string
  seat_id?: string | null
  place?: number | null
  controller_label?: string | null
  controller_type?: string | null
  nation?: string | null
  scored?: boolean
}

export interface ReplayFrame {
  index: number
  turn?: number | null
  source_name: string
  png_url: string
  map_players?: MapPlayer[]
}

export interface WatchResponse {
  schema_version: number
  label: string
  game: GameStatus
  timeline: TurnTimelineEntry[]
  frames: ReplayFrame[]
  replay?: { url: string; available: boolean }
  video?: { available: boolean; url: string; kind: string }
}

export interface Technology {
  id: number
  rule_name: string
  name: string
  cost_base: number
  requires?: number[]
  depth?: number
}

export interface TechnologyCatalog {
  schema_version?: number
  technologies: Technology[]
}

export interface ResearchState {
  tech_id: number | null
  name: string
  bulbs: number
  cost: number
}

export interface ReplayPlayer {
  seat_id: string
  place?: number | null
  player_id: number
  player_name: string
  player_color?: string | null
  controller_label?: string | null
  controller_type?: string | null
  model?: string | null
  nation: string
  government: string
  alive: boolean
  score: number
  cities: number
  citizens?: number
  population?: number
  units: number
  gold: number
  culture: number
  known_tech_ids: number[]
  gained_tech_ids: number[]
  lost_tech_ids: number[]
  research: ResearchState
  future_techs: number
  scored?: boolean
}

export interface ReplaySnapshot {
  schema_version?: number
  game_id?: string
  turn: number
  year: number
  players: ReplayPlayer[]
}

export interface ReplayWarning {
  turn?: number | null
  message: string
}

export interface ReplayResponse {
  schema_version: number
  game_id: string
  available: boolean
  catalog?: TechnologyCatalog
  snapshots: ReplaySnapshot[]
  next_after_turn: number
  has_more: boolean
  complete?: boolean
  replay_warnings?: ReplayWarning[]
  warnings?: ReplayWarning[]
}

export interface BoardTerrain {
  code: string
  name: string
}

export interface BoardExtra {
  id: number
  name: string
}

export interface BoardCity {
  id: number
  x: number
  y: number
  player_id: number
  name: string
  size: number
  capital: boolean
}

export interface BoardUnitType {
  name: string
  count: number
}

export interface BoardUnitStack {
  x: number
  y: number
  player_id: number
  count: number
  types: BoardUnitType[]
}

export interface BoardPlayer {
  player_id: number
  player_name: string
  player_color?: string | null
  seat_id?: string | null
  place?: number | null
  controller_label?: string | null
  controller_type?: string | null
  model?: string | null
  nation?: string | null
  scored?: boolean
}

export interface BoardResponse {
  schema_version: number
  game_id: string
  turn: number
  width: number
  height: number
  topology: string
  wrap: string
  terrain_catalog: BoardTerrain[]
  terrain_rows: string[]
  altitude_rows: string[]
  owner_rows: string[]
  extras_catalog: BoardExtra[]
  extra_layers: string[][]
  cities: BoardCity[]
  unit_stacks: BoardUnitStack[]
  players: BoardPlayer[]
}

export interface RouteContext {
  gameId: string
  prefix: string
}

export interface ArenaRouteContext {
  prefix: string
}
