import type { GameEvent, GamePlace, ReplayPlayer } from './types'

/** Short chips for the log's right rail; unknown kinds render their raw name. */
const EVENT_KIND_LABELS: Readonly<Record<string, string>> = {
  alliance_formed: 'Alliance',
  armistice_agreed: 'Armistice',
  barbarian_uprising: 'Uprising',
  barbarians_cleared: 'Cleared',
  capital_moved: 'Capital',
  ceasefire_agreed: 'Cease-fire',
  city_captured: 'Captured',
  city_destroyed: 'Razed',
  city_founded: 'Founded',
  diplomacy_changed: 'Diplomacy',
  first_contact: 'Contact',
  government_changed: 'Government',
  lead_changed: 'Lead',
  match_ended: 'Match end',
  peace_agreed: 'Peace',
  player_eliminated: 'Eliminated',
  player_joined: 'Joined',
  score_surge: 'Surge',
  spaceship_arrived: 'Arrival',
  spaceship_launched: 'Launch',
  spaceship_lost: 'Spaceship lost',
  spaceship_progress: 'Spaceship',
  spaceship_started: 'Spaceship',
  war_declared: 'War',
  wonder_captured: 'Wonder taken',
  wonder_completed: 'Wonder',
  wonder_destroyed: 'Wonder lost',
}

export function eventKindLabel(kind: string): string {
  return EVENT_KIND_LABELS[kind] ?? kind.replaceAll('_', ' ')
}

/**
 * Recorded faction color per actor id.
 *
 * Events name an actor by seat id where the seat resolved and by recorded
 * player name otherwise, and a barbarian slot is renamed with every uprising,
 * so every snapshot's players are folded in — not only the latest one. Replay
 * telemetry wins over the configured roster: the save and its map header are
 * authoritative for the color a faction actually plays in.
 */
export function actorColors(
  places: readonly GamePlace[],
  players: readonly ReplayPlayer[],
): Map<string, string> {
  const colors = new Map<string, string>()
  for (const place of places) {
    if (place.player_color) colors.set(place.seat_id, place.player_color)
  }
  for (const player of players) {
    if (!player.player_color) continue
    colors.set(player.seat_id, player.player_color)
    colors.set(player.player_name, player.player_color)
  }
  return colors
}

export function eventColor(
  event: GameEvent,
  colors: ReadonlyMap<string, string>,
): string | null {
  for (const actor of event.actors) {
    const color = colors.get(actor)
    if (color) return color
  }
  return null
}

/**
 * Index of the newest event at or before `selectedTurn`, or -1 when the
 * scrubber sits before the first thing that ever happened.
 */
export function activeEventIndex(
  events: readonly GameEvent[],
  selectedTurn: number,
): number {
  let active = -1
  for (const [index, event] of events.entries()) {
    if (event.turn > selectedTurn) break
    active = index
  }
  return active
}

export interface EventRow {
  event: GameEvent
  /** True for the newest event the scrubber has reached. */
  current: boolean
  /** True for events the film has not played yet. */
  upcoming: boolean
  color: string | null
  label: string
  tier: WeightTier
}

export function eventRows(
  events: readonly GameEvent[],
  selectedTurn: number,
  colors: ReadonlyMap<string, string>,
): EventRow[] {
  const active = activeEventIndex(events, selectedTurn)
  return events.map((event, index) => ({
    event,
    current: index === active,
    upcoming: event.turn > selectedTurn,
    color: eventColor(event, colors),
    label: eventKindLabel(event.kind),
    tier: weightTier(event.weight),
  }))
}

export type EventDensity = 'all' | 'key' | 'major'

/**
 * Weight floor per density stop, on the extractor's 1-100 scale.
 *
 * "Key moments" reads the weight rather than naming kinds, so the panel and
 * the film agree on what matters from one shared number: at 30 it folds away
 * city foundings, government churn, the barbarian slot flapping in and out,
 * first contact, and score markers, and keeps every capture, razing, wonder,
 * pact, elimination and spaceship beat.
 */
export const DENSITY_FLOOR: Readonly<Record<EventDensity, number>> = {
  all: 0,
  key: 30,
  major: 60,
}

export const DENSITY_ORDER: readonly EventDensity[] = ['all', 'key', 'major']

export const DENSITY_LABEL: Readonly<Record<EventDensity, string>> = {
  all: 'Everything',
  key: 'Key moments',
  major: 'Major',
}

export function eventsAtDensity(
  events: readonly GameEvent[], density: EventDensity,
): GameEvent[] {
  const floor = DENSITY_FLOOR[density]
  return events.filter((event) => event.weight >= floor)
}

export type WeightTier = 'major' | 'notable' | 'routine'

/** Which visual rank a row gets, on the same boundaries as the density stops. */
export function weightTier(weight: number): WeightTier {
  if (weight >= DENSITY_FLOOR.major) return 'major'
  if (weight >= DENSITY_FLOOR.key) return 'notable'
  return 'routine'
}

/**
 * The heaviest event in each fixed-width turn window, for consumers that want
 * a fixed beat rate rather than a threshold — a 4x film keeping one moment
 * every N turns, for instance. Windows with nothing in them are skipped, and
 * ties inside a window go to the earlier event.
 */
export function heaviestPerWindow(
  events: readonly GameEvent[], windowTurns: number,
): GameEvent[] {
  if (!Number.isFinite(windowTurns) || windowTurns < 1) return [...events]
  const best = new Map<number, GameEvent>()
  for (const event of events) {
    const window = Math.floor(event.turn / windowTurns)
    const held = best.get(window)
    if (!held || event.weight > held.weight) best.set(window, event)
  }
  return [...best.values()].sort((left, right) => left.turn - right.turn)
}

export function omittedSummary(omitted: Readonly<Record<string, number>>): string {
  const parts = Object.entries(omitted)
    .sort(([left], [right]) => left.localeCompare(right))
    .map(([kind, count]) => `${count} ${eventKindLabel(kind).toLowerCase()}`)
  return parts.join(', ')
}
