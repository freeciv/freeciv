import type {
  GamePlace,
  MapPlayer,
  ReplayFrame,
  ReplayPlayer,
  ReplaySnapshot,
  Technology,
} from './types'
import { placeLabel } from './picker-model'
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
  return places.map((place) => ({
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

export function matchHeaderLabel(places: GamePlace[]): string {
  const agents = places.filter((place) => place.controller !== 'native_classic_ai').map(placeLabel)
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
  const placeByName = new Map(places.map((place) => [place.player_name, place]))
  const placeBySeat = new Map(places.map((place) => [place.seat_id, place]))
  const placeByNumber = new Map(places.map((place) => [place.place, place]))
  return sourcePlayers.map((mapPlayer) => {
    const place = placeByName.get(mapPlayer.player_name)
      ?? (mapPlayer.seat_id ? placeBySeat.get(mapPlayer.seat_id) : undefined)
      ?? (mapPlayer.place ? placeByNumber.get(mapPlayer.place) : undefined)
    const replayPlayer = replayByName.get(mapPlayer.player_name)
    if (place || mapPlayer.scored || replayPlayer?.scored) {
      const label = place?.controller_label || mapPlayer.controller_label
        || replayPlayer?.controller_label
        || (place?.controller === 'native_classic_ai'
          ? 'Freeciv Classic AI'
          : 'Unclaimed agent place')
      return {
        ...mapPlayer,
        display_label: factionDisplayLabel({
          controller_label: label,
          controller_type: place?.controller === 'native_classic_ai'
            ? 'native'
            : mapPlayer.controller_type ?? replayPlayer?.controller_type,
          nation: mapPlayer.nation ?? replayPlayer?.nation,
          player_name: place?.player_name ?? mapPlayer.player_name,
        }),
        detail: `${place?.player_name ?? mapPlayer.player_name}${replayPlayer?.nation ? ` · ${replayPlayer.nation}` : ''}`,
        dynamic: false,
      }
    }
    const nation = mapPlayer.nation || replayPlayer?.nation
    return {
      ...mapPlayer,
      display_label: factionDisplayLabel({
        controller_label: 'Freeciv dynamic faction',
        controller_type: 'dynamic',
        nation,
        player_name: mapPlayer.player_name,
      }),
      detail: `${mapPlayer.player_name}${nation ? ` · ${nation}` : ''}`,
      dynamic: true,
    }
  })
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
