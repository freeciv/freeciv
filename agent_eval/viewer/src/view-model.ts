import type {
  GamePlace,
  GameStatus,
  MapPlayer,
  ReplayFrame,
  ReplayPlayer,
  ReplaySnapshot,
  Technology,
} from './types'
import { placeLabel } from './picker-model'
import { agentFirst, agentFirstBy, isNativeController } from './agent-order'
import { displayControllerLabel, factionDisplayLabel } from './faction-label'

export const METRICS = [
  { key: 'score', label: 'Score' },
  { key: 'cities', label: 'Cities' },
  { key: 'citizens', label: 'Citizens' },
  { key: 'units', label: 'Units' },
  { key: 'gold', label: 'Gold' },
  { key: 'culture', label: 'Culture' },
] as const

export type MetricKey = (typeof METRICS)[number]['key']

const TERMINAL_STATES = new Set(['completed', 'invalid', 'failed', 'cancelled'])

export interface ScoreDisplay {
  label: 'FINAL SCORE' | 'FINAL SCORE UNAVAILABLE' | 'SCORE' | 'SCORE AT TURN'
  value: number | null
}

export function scoreDisplay(
  gameState: string,
  leaderboardScore: number | undefined,
  telemetryScore: number | undefined,
): ScoreDisplay {
  if (TERMINAL_STATES.has(gameState)) {
    return leaderboardScore === undefined
      ? { label: 'FINAL SCORE UNAVAILABLE', value: null }
      : { label: 'FINAL SCORE', value: leaderboardScore }
  }
  return {
    label: 'SCORE',
    value: leaderboardScore ?? telemetryScore ?? null,
  }
}

export function scoreDisplayAtTurn(
  gameState: string,
  leaderboardScore: number | undefined,
  telemetryScore: number | undefined,
  selectedTurn: number,
  latestTurn: number,
): ScoreDisplay {
  if (selectedTurn > 0 && latestTurn > 0 && selectedTurn < latestTurn) {
    return { label: 'SCORE AT TURN', value: telemetryScore ?? null }
  }
  return scoreDisplay(gameState, leaderboardScore, telemetryScore)
}

export function playerMetric(player: ReplayPlayer, metric: MetricKey): number {
  if (metric === 'citizens') return player.citizens ?? player.population ?? 0
  return player[metric]
}

export function snapshotAtOrBefore(
  snapshots: ReplaySnapshot[],
  turn: number,
): ReplaySnapshot | undefined {
  let selected: ReplaySnapshot | undefined
  for (const snapshot of snapshots) {
    if (snapshot.turn > turn) break
    selected = snapshot
  }
  return selected
}

export function frameAtOrBefore(
  frames: ReplayFrame[],
  turn: number,
): ReplayFrame | undefined {
  let selected: ReplayFrame | undefined
  for (const frame of frames) {
    if (typeof frame.turn === 'number' && frame.turn <= turn) selected = frame
    if (typeof frame.turn === 'number' && frame.turn > turn) break
  }
  return selected
}

export function maxKnownTechnologyDepth(
  player: ReplayPlayer,
  technologies: Technology[],
): number | null {
  if (!player.known_tech_ids.length || !technologies.length) return null
  const known = new Set(player.known_tech_ids)
  const depths = technologies.flatMap((technology) => (
    known.has(technology.id) && typeof technology.depth === 'number'
      ? [technology.depth]
      : []
  ))
  return depths.length ? Math.max(...depths) : null
}

export function isScoredPlayer(player: ReplayPlayer): boolean {
  if (player.scored === false) return false
  return player.place != null && player.controller_type !== 'dynamic'
}

export function competitorLabel(player: ReplayPlayer): string {
  return (
    displayControllerLabel(player.controller_label) ||
    (isScoredPlayer(player) ? player.player_name : player.nation) ||
    'Freeciv dynamic faction'
  )
}

export interface MapFaction extends MapPlayer {
  display_label: string
  detail: string
  dynamic: boolean
}

export function configuredPlaceFactions(places: GamePlace[]): MapFaction[] {
  return agentFirst(places).map((place) => ({
    player_id: place.place - 1,
    player_name: place.player_name,
    player_color: place.player_color,
    seat_id: place.seat_id,
    place: place.place,
    controller_label: place.controller_label,
    controller_type: place.controller_type,
    scored: true,
    display_label: factionDisplayLabel({
      controller_label: placeLabel(place),
      controller_type: place.controller === 'native_classic_ai' ? 'native' : place.controller_type,
      player_name: place.player_name,
    }),
    detail: place.controller === 'native_classic_ai'
      ? `${place.player_name} · native controller`
      : `${place.player_name}${place.model ? ` · ${place.model}` : ''}`,
    dynamic: false,
  }))
}

export function matchDurationLabel(
  game: Pick<GameStatus, 'created_at' | 'finished_at'>,
  nowMs: number = Date.now(),
): string | null {
  if (typeof game.created_at !== 'number') return null
  const end = typeof game.finished_at === 'number' ? game.finished_at : nowMs / 1000
  const total = Math.max(0, Math.round(end - game.created_at))
  const days = Math.floor(total / 86_400)
  const hours = Math.floor((total % 86_400) / 3_600)
  const minutes = Math.floor((total % 3_600) / 60)
  if (days > 0) return `${days}d ${hours}h`
  if (hours > 0) return `${hours}h ${minutes}m`
  if (minutes > 0) return `${minutes}m`
  return `${total}s`
}

/** "A vs B", agents first: the model names the match, never the built-in AI. */
export function matchHeaderLabel(places: GamePlace[]): string {
  const agents = places.filter((place) => !isNativeController(place)).map(placeLabel)
  const nativeCount = places.length - agents.length
  const nativeLabel = nativeCount > 1 ? `In-game Deity AI ×${nativeCount}` : nativeCount === 1 ? 'In-game Deity AI' : null
  return [...agents, ...(nativeLabel ? [nativeLabel] : [])].join('  vs  ')
}

export function mapFactions(
  frame: ReplayFrame | undefined,
  snapshot: ReplaySnapshot | undefined,
  places: GamePlace[],
): MapFaction[] {
  const sourcePlayers: MapPlayer[] = frame?.map_players ?? (snapshot?.players ?? []).flatMap(
    (player) => player.player_color ? [{
      player_id: player.player_id,
      player_name: player.player_name,
      player_color: player.player_color,
      seat_id: player.seat_id,
      place: player.place,
      controller_label: player.controller_label,
      controller_type: player.controller_type,
      nation: player.nation,
      scored: player.scored,
    }] : [],
  )
  if (!sourcePlayers.length) return []
  const replayByName = new Map(
    (snapshot?.players ?? []).map((player) => [player.player_name, player]),
  )
  // The native save renames an agent-controlled player to its ruler
  // ("Elizabeth" for the seat configured as "AgentPlace1"), so a name join
  // misses exactly the seats that matter. player_id is stable across the
  // save and the replay telemetry, and the telemetry rows carry seat_id.
  const replayByPlayerId = new Map(
    (snapshot?.players ?? []).map((player) => [player.player_id, player]),
  )
  const placeByName = new Map(places.map((place) => [place.player_name, place]))
  const placeBySeat = new Map(places.map((place) => [place.seat_id, place]))
  const placeByNumber = new Map(places.map((place) => [place.place, place]))
  // A map faction's controller is only known once the frame, the replay row and
  // the configured place have been joined, so nativeness is decided here and
  // the agent-first partition is applied to the joined result.
  const joined = sourcePlayers.map((mapPlayer) => {
    const replayPlayer = replayByName.get(mapPlayer.player_name)
      ?? replayByPlayerId.get(mapPlayer.player_id)
    const place = placeByName.get(mapPlayer.player_name)
      ?? (mapPlayer.seat_id ? placeBySeat.get(mapPlayer.seat_id) : undefined)
      ?? (mapPlayer.place ? placeByNumber.get(mapPlayer.place) : undefined)
      ?? (replayPlayer?.seat_id ? placeBySeat.get(replayPlayer.seat_id) : undefined)
    if (place || mapPlayer.scored || replayPlayer?.scored) {
      const label = place?.controller_label || mapPlayer.controller_label
        || replayPlayer?.controller_label
        || (place?.controller === 'native_classic_ai'
          ? 'Freeciv Classic AI'
          : 'Unclaimed agent place')
      const controllerType = place?.controller === 'native_classic_ai'
        ? 'native'
        : mapPlayer.controller_type ?? replayPlayer?.controller_type
      return {
        agent: !isNativeController({
          controller_label: label, controller_type: controllerType,
        }),
        faction: {
          ...mapPlayer,
          display_label: factionDisplayLabel({
            controller_label: label,
            controller_type: controllerType,
            nation: mapPlayer.nation ?? replayPlayer?.nation,
            player_name: place?.player_name ?? mapPlayer.player_name,
          }),
          detail: `${place?.player_name ?? mapPlayer.player_name}${replayPlayer?.nation ? ` · ${replayPlayer.nation}` : ''}`,
          dynamic: false,
        },
      }
    }
    const nation = mapPlayer.nation || replayPlayer?.nation
    return {
      agent: false,
      faction: {
        ...mapPlayer,
        display_label: factionDisplayLabel({
          controller_label: 'Freeciv dynamic faction',
          controller_type: 'dynamic',
          nation,
          player_name: mapPlayer.player_name,
        }),
        detail: `${mapPlayer.player_name}${nation ? ` · ${nation}` : ''}`,
        dynamic: true,
      },
    }
  })
  return agentFirstBy(joined, (entry) => entry.agent).map((entry) => entry.faction)
}

export type TechnologyState = 'known' | 'current' | 'available' | 'locked'

export function technologyState(
  technology: Technology,
  player: ReplayPlayer,
): TechnologyState {
  if (player.known_tech_ids.includes(technology.id)) return 'known'
  if (player.research.tech_id === technology.id) return 'current'
  const known = new Set(player.known_tech_ids)
  if ((technology.requires ?? []).every((requirement) => known.has(requirement))) {
    return 'available'
  }
  return 'locked'
}

export function turnsAvailable(
  snapshots: ReplaySnapshot[],
  frames: ReplayFrame[],
): number[] {
  return [...new Set([
    ...snapshots.map((snapshot) => snapshot.turn),
    ...frames.flatMap((frame) => typeof frame.turn === 'number' ? [frame.turn] : []),
  ])].sort((a, b) => a - b)
}
