import { describe, expect, it } from 'vitest'
import { agentFirst, isAgentController, isNativeController } from './agent-order'
import { configuredPlaceFactions, mapFactions, matchHeaderLabel } from './view-model'
import { mockReplay, mockWatch } from './mock'
import type { GamePlace } from './types'

function place(overrides: Partial<GamePlace> & Pick<GamePlace, 'place'>): GamePlace {
  return {
    seat_id: `place-${overrides.place}`,
    player_name: `Place${overrides.place}`,
    controller: 'agent',
    joined: true,
    controller_label: `agent-${overrides.place}`,
    controller_type: 'external',
    model: `model-${overrides.place}`,
    player_color: '#0067A5',
    ...overrides,
  }
}

function nativePlace(number: number): GamePlace {
  return place({
    place: number,
    player_name: `NativePlace${number}`,
    controller: 'native_classic_ai',
    joined: false,
    controller_label: 'Freeciv Classic AI',
    controller_type: 'native',
    model: 'classic',
    player_color: '#F38400',
  })
}

const seats = (places: GamePlace[]) => places.map((entry) => entry.seat_id)

describe('agent-first ordering', () => {
  it('names a native controller from any of the fields that carry one', () => {
    expect(isNativeController({ controller: 'native_classic_ai' })).toBe(true)
    expect(isNativeController({ controller_type: 'native' })).toBe(true)
    // Older payloads type nothing and only name the controller.
    expect(isNativeController({ controller_label: 'Freeciv Classic AI' })).toBe(true)
    expect(isNativeController({ controller_label: 'codex-gpt-5.6-sol' })).toBe(false)
    expect(isNativeController({})).toBe(false)
    expect(isAgentController({ controller_type: 'dynamic' })).toBe(false)
    expect(isAgentController({ controller_type: 'external' })).toBe(true)
  })

  it('lifts the agent to player one when Freeciv seated the native first', () => {
    expect(seats(agentFirst([nativePlace(1), place({ place: 2 })]))).toEqual([
      'place-2', 'place-1',
    ])
  })

  it('leaves an already agent-first single-player match alone', () => {
    expect(seats(agentFirst([place({ place: 1 }), nativePlace(2)]))).toEqual([
      'place-1', 'place-2',
    ])
  })

  it('does not disturb an agent-vs-agent match', () => {
    const roster = [place({ place: 1 }), place({ place: 2 }), place({ place: 3 })]
    expect(seats(agentFirst(roster))).toEqual(['place-1', 'place-2', 'place-3'])
  })

  it('keeps several natives in their seat order behind the agent', () => {
    const roster = [nativePlace(1), nativePlace(2), place({ place: 3 }), nativePlace(4)]
    expect(seats(agentFirst(roster))).toEqual([
      'place-3', 'place-1', 'place-2', 'place-4',
    ])
  })

  it('keeps dynamic factions behind the seated sides rather than ahead of them', () => {
    // The film's rosters are seats only; the viewer's map key also carries the
    // factions Freeciv spawns itself, and a pirate is not a contender that
    // outranks the built-in AI.
    const roster = [
      nativePlace(1),
      place({ place: 2, controller_type: 'dynamic', controller_label: 'Freeciv dynamic faction' }),
      place({ place: 3 }),
    ]
    expect(seats(agentFirst(roster))).toEqual(['place-3', 'place-1', 'place-2'])
  })
})

describe('agent-first ordering across the viewer', () => {
  /** The mock roster with the native dealt the first seat. */
  const nativeSeatedFirst: GamePlace[] = [
    { ...mockWatch.game.resolved_places[1], place: 1, seat_id: 'place-1' },
    { ...mockWatch.game.resolved_places[0], place: 2, seat_id: 'place-2' },
  ]

  it('titles the page with the model even when the AI holds seat one', () => {
    // Only the ordering is asserted here; what the native side is called is
    // `matchHeaderLabel`'s own test.
    expect(matchHeaderLabel(nativeSeatedFirst).split('  vs  ')[0])
      .toBe('codex-gpt-5.6-sol')
  })

  it('opens the configured map key on the agent', () => {
    expect(configuredPlaceFactions(nativeSeatedFirst).map((faction) => faction.player_name))
      .toEqual(['AgentPlace1', 'NativePlace2'])
  })

  it('opens the resolved map key on the agent and keeps dynamics last', () => {
    const frame = {
      ...mockWatch.frames[0],
      map_players: [
        { player_id: 1, player_name: 'NativePlace2', player_color: '#F38400' },
        { player_id: 2, player_name: 'Blackbeard', player_color: '#FF1493' },
        { player_id: 0, player_name: 'AgentPlace1', player_color: '#0067A5' },
      ],
    }
    const factions = mapFactions(
      frame, mockReplay.snapshots[2], mockWatch.game.resolved_places,
    )
    expect(factions.map((faction) => faction.player_name))
      .toEqual(['AgentPlace1', 'NativePlace2', 'Blackbeard'])
  })
})
