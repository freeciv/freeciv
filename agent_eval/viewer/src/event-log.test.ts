import { describe, expect, it } from 'vitest'
import {
  actorColors,
  activeEventIndex,
  eventColor,
  eventKindLabel,
  eventRows,
  eventsAtDensity,
  heaviestPerWindow,
  omittedSummary,
  weightTier,
} from './event-log'
import type { GameEvent, GamePlace, ReplayPlayer } from './types'

function event(
  turn: number, kind: string, actors: string[] = [], weight = 50,
): GameEvent {
  return { turn, kind, summary: `${kind} on ${turn}`, actors, weight, data: {} }
}

const place: GamePlace = {
  place: 1,
  seat_id: 'place-1',
  player_name: 'AgentPlace1',
  controller: 'agent',
  joined: true,
  player_color: '#0067A5',
}

function player(overrides: Partial<ReplayPlayer>): ReplayPlayer {
  return {
    seat_id: 'place-1',
    player_id: 0,
    player_name: 'Elizabeth',
    nation: 'English',
    government: 'Republic',
    alive: true,
    score: 0,
    cities: 0,
    units: 0,
    gold: 0,
    culture: 0,
    known_tech_ids: [],
    gained_tech_ids: [],
    lost_tech_ids: [],
    research: { tech_id: null, name: '', bulbs: 0, cost: 0 },
    future_techs: 0,
    ...overrides,
  }
}

describe('event kind labels', () => {
  it('names the kinds the log emits', () => {
    expect(eventKindLabel('city_captured')).toBe('Captured')
    expect(eventKindLabel('war_declared')).toBe('War')
    expect(eventKindLabel('spaceship_launched')).toBe('Launch')
  })

  it('degrades a future kind to readable text instead of hiding it', () => {
    expect(eventKindLabel('trade_route_opened')).toBe('trade route opened')
  })
})

describe('actor colors', () => {
  it('lets recorded telemetry override the configured roster color', () => {
    const colors = actorColors([place], [player({ player_color: '#112233' })])
    expect(colors.get('place-1')).toBe('#112233')
    expect(colors.get('Elizabeth')).toBe('#112233')
  })

  it('keeps the roster color for a seat with no telemetry color', () => {
    expect(actorColors([place], []).get('place-1')).toBe('#0067A5')
  })

  it('resolves a barbarian slot under every name it ever carried', () => {
    // The slot is renamed with each uprising, so only folding in the latest
    // snapshot would leave earlier events unpainted.
    const colors = actorColors([], [
      player({ seat_id: 'dynamic-player-2', player_id: 2, player_name: 'Calico Jack', player_color: '#B8860B' }),
      player({ seat_id: 'dynamic-player-2', player_id: 2, player_name: 'Henry Morgan', player_color: '#B8860B' }),
    ])
    expect(colors.get('Calico Jack')).toBe('#B8860B')
    expect(colors.get('Henry Morgan')).toBe('#B8860B')
  })

  it('paints an event by its first resolvable actor', () => {
    const colors = actorColors([place], [])
    expect(eventColor(event(4, 'city_captured', ['place-9', 'place-1']), colors))
      .toBe('#0067A5')
    expect(eventColor(event(4, 'match_ended', []), colors)).toBeNull()
  })
})

describe('scrubber alignment', () => {
  const events = [event(2, 'city_founded'), event(9, 'war_declared'), event(30, 'city_captured')]

  it('marks the newest event the film has reached', () => {
    expect(activeEventIndex(events, 0)).toBe(-1)
    expect(activeEventIndex(events, 2)).toBe(0)
    expect(activeEventIndex(events, 8)).toBe(0)
    expect(activeEventIndex(events, 9)).toBe(1)
    expect(activeEventIndex(events, 500)).toBe(2)
  })

  it('splits rows into played, current, and upcoming', () => {
    const rows = eventRows(events, 9, new Map())
    expect(rows.map((row) => row.current)).toEqual([false, true, false])
    expect(rows.map((row) => row.upcoming)).toEqual([false, false, true])
    expect(rows[1]?.label).toBe('War')
  })

  it('leaves every row upcoming before the first event', () => {
    const rows = eventRows(events, 0, new Map())
    expect(rows.every((row) => row.upcoming)).toBe(true)
    expect(rows.some((row) => row.current)).toBe(false)
  })
})

describe('weight-driven density', () => {
  const story = [
    event(2, 'city_founded', [], 8),
    event(9, 'war_declared', [], 80),
    event(30, 'city_captured', [], 52),
    event(40, 'government_changed', [], 22),
    event(88, 'player_eliminated', [], 96),
  ]

  it('selects by weight floor rather than by kind', () => {
    expect(eventsAtDensity(story, 'all')).toHaveLength(5)
    expect(eventsAtDensity(story, 'key').map((row) => row.kind)).toEqual([
      'war_declared', 'city_captured', 'player_eliminated',
    ])
    expect(eventsAtDensity(story, 'major').map((row) => row.kind)).toEqual([
      'war_declared', 'player_eliminated',
    ])
  })

  it('folds away expansion at the key-moments stop without naming a kind', () => {
    const expansion = [
      event(2, 'city_founded', [], 18),
      event(4, 'city_founded', [], 8),
      event(9, 'city_destroyed', [], 30),
    ]
    expect(eventsAtDensity(expansion, 'key').map((row) => row.kind))
      .toEqual(['city_destroyed'])
  })

  it('ranks a row visually by the same thresholds', () => {
    expect(weightTier(96)).toBe('major')
    expect(weightTier(60)).toBe('major')
    expect(weightTier(52)).toBe('notable')
    expect(weightTier(29)).toBe('routine')
  })

  it('carries the tier onto each row', () => {
    expect(eventRows(story, 500, new Map()).map((row) => row.tier)).toEqual([
      'routine', 'major', 'notable', 'routine', 'major',
    ])
  })

  it('keeps the heaviest beat per fixed turn window for a film', () => {
    // One beat every 50 turns: the war beats the founding in its window, and
    // an empty window is simply skipped.
    expect(heaviestPerWindow(story, 50).map((row) => row.turn)).toEqual([9, 88])
    expect(heaviestPerWindow(story, 25).map((row) => row.turn)).toEqual([9, 30, 88])
    expect(heaviestPerWindow(story, 0)).toHaveLength(5)
  })
})

describe('capped log reporting', () => {
  it('says what was dropped in the reader\'s vocabulary', () => {
    expect(omittedSummary({ city_founded: 12, government_changed: 3 }))
      .toBe('12 founded, 3 government')
  })

  it('says nothing when nothing was dropped', () => {
    expect(omittedSummary({})).toBe('')
  })
})
