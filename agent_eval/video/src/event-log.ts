/**
 * Match events: shared vocabulary with the viewer, plus the film's own
 * caption pacing.
 *
 * `heaviestPerWindow`, `DENSITY_FLOOR` and `weightTier` are ports of
 * `agent_eval/viewer/src/event-log.ts`. Keep them in step — the film's captions
 * and the viewer's "key moments" are meant to be the same notion of what
 * mattered, decided from the extractor's one shared weight rather than from two
 * separate opinions about which kinds are interesting.
 */

import type { GameEvent } from './dataset/schema'

/** Short chips; unknown kinds render their raw name. Mirrors the viewer. */
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

/** Weight floors, on the extractor's 1-100 scale. Mirrors the viewer. */
export const DENSITY_FLOOR = { all: 0, key: 30, major: 60 } as const

export type WeightTier = 'major' | 'notable' | 'routine'

export function weightTier(weight: number): WeightTier {
  if (weight >= DENSITY_FLOOR.major) return 'major'
  if (weight >= DENSITY_FLOOR.key) return 'notable'
  return 'routine'
}

/**
 * The heaviest event in each fixed-width turn window. Ported verbatim from the
 * viewer: windows with nothing in them are skipped, and ties inside a window go
 * to the earlier event.
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

export interface Caption {
  readonly event: GameEvent
  /** Events the window held besides this one, for the "+N more" chip. */
  readonly alsoInWindow: number
  /** Same-kind events in the window, for collapsing a burst into one line. */
  readonly sameKindInWindow: number
}

export interface CaptionPlan {
  readonly captions: readonly Caption[]
  readonly windowTurns: number
  readonly weightFloor: number
}

export interface CaptionPacing {
  /** Turns of match time elapsing per second of film. */
  readonly turnsPerSecond: number
  /** How long one caption stays legible on screen. */
  readonly holdSeconds: number
}

/**
 * Choose which events the film has room to caption.
 *
 * Two rules from the brief pull against each other: never stack a burst, but
 * always let the big moments through. They collide when two heavy events land
 * within one window. This resolves it by giving the slot to the heavier and
 * reporting the rest as a count -- the moment is never silently dropped, and a
 * caption never lands on top of another.
 *
 * The weight floor follows playback speed rather than a fixed kind list: a fast
 * film folds away foundings, government churn and first contact, while a slow
 * one has room to breathe and shows whatever its window picked.
 */
export function planCaptions(
  events: readonly GameEvent[], pacing: CaptionPacing,
): CaptionPlan {
  const turnsPerSecond = Math.max(0.01, pacing.turnsPerSecond)
  const holdSeconds = Math.max(0.1, pacing.holdSeconds)
  // A margin over the hold so two captions can never be on screen at once.
  const windowTurns = Math.max(1, Math.ceil(turnsPerSecond * holdSeconds * 1.15))
  const weightFloor = turnsPerSecond > 3 ? DENSITY_FLOOR.key : DENSITY_FLOOR.all

  const eligible = events.filter((event) => event.weight >= weightFloor)
  const counts = new Map<number, { total: number; byKind: Map<string, number> }>()
  for (const event of eligible) {
    const window = Math.floor(event.turn / windowTurns)
    const entry = counts.get(window) ?? { total: 0, byKind: new Map<string, number>() }
    entry.total += 1
    entry.byKind.set(event.kind, (entry.byKind.get(event.kind) ?? 0) + 1)
    counts.set(window, entry)
  }

  const captions = heaviestPerWindow(eligible, windowTurns).map((event): Caption => {
    const entry = counts.get(Math.floor(event.turn / windowTurns))
    return {
      event,
      alsoInWindow: Math.max(0, (entry?.total ?? 1) - 1),
      sameKindInWindow: entry?.byKind.get(event.kind) ?? 1,
    }
  })
  return { captions, windowTurns, weightFloor }
}

/**
 * The whole match's biggest moments, read in turn order.
 *
 * Straight weight order buries the arc: a raiding streak puts four near
 * identical captures at the top and pushes the war and the spaceship launch
 * out of view. Each kind is capped so the list reads as a story, and the cap
 * relaxes only if there is genuinely nothing else to show.
 */
export function topHighlights(
  events: readonly GameEvent[], limit: number, perKind = 2,
): GameEvent[] {
  const wanted = Math.max(0, limit)
  const ranked = [...events]
    .sort((left, right) => right.weight - left.weight || left.turn - right.turn)
  const chosen: GameEvent[] = []
  const used = new Map<string, number>()
  for (const event of ranked) {
    if (chosen.length >= wanted) break
    const seen = used.get(event.kind) ?? 0
    if (seen >= Math.max(1, perKind)) continue
    used.set(event.kind, seen + 1)
    chosen.push(event)
  }
  // Short only because the cap bit: refill from what the cap held back.
  for (const event of ranked) {
    if (chosen.length >= wanted) break
    if (!chosen.includes(event)) chosen.push(event)
  }
  return chosen.sort((left, right) => left.turn - right.turn)
}
