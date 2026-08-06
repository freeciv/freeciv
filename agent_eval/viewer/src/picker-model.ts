import type { GamePlace, GameSummary } from './types'

const GAME_ID_RE = /^[A-Za-z0-9_-]{20,80}$/
const ACTIVE_STATES = new Set(['lobby', 'starting', 'running'])

export function normalizeGameId(value: string): string | null {
  const gameId = value.trim()
  return GAME_ID_RE.test(gameId) ? gameId : null
}

export function gameModeLabel(game: Pick<GameSummary, 'mode'>): string {
  return game.mode === 'single' ? 'Single player vs native AI' : 'Multiplayer agent match'
}

export type ControlProtocol = 'full-control-v2' | 'strategic-v1'

export interface ControlProtocolLabel {
  protocol: ControlProtocol
  label: string
  detail: string
  /** True when the run predates the field and falls back to the server default. */
  assumed: boolean
}

export function controlProtocolLabel(
  protocol: string | null | undefined,
): ControlProtocolLabel {
  if (protocol === 'full-control-v2') {
    return {
      protocol: 'full-control-v2',
      label: 'Full control',
      detail: 'Agents drive every unit',
      assumed: false,
    }
  }
  return {
    protocol: 'strategic-v1',
    label: 'Strategic traits',
    detail: 'Agents steer the classic AI',
    assumed: protocol !== 'strategic-v1',
  }
}

export function matchModeLabel(
  game: Pick<GameSummary, 'control_protocol' | 'mode'>,
): string {
  return `${gameModeLabel(game)} · ${controlProtocolLabel(game.control_protocol).label}`
}

export function openAgentSeats(game: GameSummary): number {
  return Math.max(0, game.max_agents - game.joined_agents)
}

export function waitingLabel(game: GameSummary): string | null {
  if (game.state !== 'lobby') return null
  const open = openAgentSeats(game)
  return open === 0
    ? 'All agents joined · preparing match'
    : `Waiting for ${open} agent${open === 1 ? '' : 's'}`
}

export function gamePrimaryResult(game: GameSummary): string {
  if (game.state === 'running') {
    return game.leaderboard[0]?.controller_label || 'No leader yet'
  }
  return game.outcome.summary || 'Outcome unavailable'
}

export function placeLabel(place: GamePlace): string {
  if (place.controller === 'native_classic_ai') return 'Freeciv Classic AI'
  if (!place.joined) return 'Open agent seat'
  return place.controller_label || place.model || 'Joined agent'
}

export function sortedGames(games: GameSummary[]): GameSummary[] {
  return [...games].sort((a, b) => {
    const active = Number(ACTIVE_STATES.has(b.state)) - Number(ACTIVE_STATES.has(a.state))
    if (active) return active
    return (b.created_at ?? 0) - (a.created_at ?? 0)
  })
}

export function visiblePickerGames(games: GameSummary[]): GameSummary[] {
  return games.filter((game) => !(
    (game.state === 'failed' || game.state === 'interrupted')
    && (game.current_turn ?? 0) <= 0
  ))
}

export function timingModeLabel(
  game: Pick<GameSummary, 'action_timeout_s' | 'timing_mode'>,
): string | null {
  if (!game.timing_mode) return null
  if (game.timing_mode === 'infinite') return 'Infinite · no deadline'
  if (typeof game.action_timeout_s !== 'number' || !Number.isFinite(game.action_timeout_s)) return null
  const label = game.timing_mode === 'blitz'
    ? 'Blitz'
    : game.timing_mode === 'custom' ? 'Custom' : 'Default'
  return `${label} · ${game.action_timeout_s}s/turn`
}
