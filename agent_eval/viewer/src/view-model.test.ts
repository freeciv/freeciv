import { describe, expect, it } from 'vitest'
import { mockReplay, mockWatch } from './mock'
import {
  controlProtocolLabel,
  gamePrimaryResult,
  matchModeLabel,
  normalizeGameId,
  placeLabel,
  timingModeLabel,
  visiblePickerGames,
} from './picker-model'
import {
  apiUrl,
  frameImageUrl,
  parseWatchRoute,
  resolveViewerRoute,
  watchUrl,
} from './route'
import {
  configuredPlaceFactions,
  frameAtOrBefore,
  mapFactions,
  matchHeaderLabel,
  maxKnownTechnologyDepth,
  playerMetric,
  scoreDisplay,
  scoreDisplayAtTurn,
  snapshotAtOrBefore,
  technologyState,
} from './view-model'

describe('watch route parsing', () => {
  it('routes the arena picker at root and reverse-proxy prefixes', () => {
    expect(resolveViewerRoute('/')).toEqual({
      kind: 'arena', context: { prefix: '' },
    })
    expect(resolveViewerRoute('/freeciv/')).toEqual({
      kind: 'arena', context: { prefix: '/freeciv' },
    })
    expect(resolveViewerRoute('/freeciv')).toEqual({
      kind: 'arena-redirect', target: '/freeciv/',
    })
  })

  it('supports root and reverse-proxy prefixes', () => {
    expect(parseWatchRoute('/watch/game_abcdefghijklmnopqrstuvwx')).toEqual({
      gameId: 'game_abcdefghijklmnopqrstuvwx', prefix: '',
    })
    const prefixed = parseWatchRoute('/freeciv/watch/game_abcdefghijklmnopqrstuvwx/')
    expect(prefixed).toEqual({
      gameId: 'game_abcdefghijklmnopqrstuvwx', prefix: '/freeciv',
    })
    expect(apiUrl(prefixed!, '/v1/games/test')).toBe('/freeciv/v1/games/test')
    expect(frameImageUrl(prefixed!, mockWatch.frames[0])).toBe(
      '/freeciv/v1/games/game_abcdefghijklmnopqrstuvwx/frames/0.png',
    )
    expect(resolveViewerRoute('/watch/game_abcdefghijklmnopqrstuvwx')).toEqual({
      kind: 'watch',
      context: { gameId: 'game_abcdefghijklmnopqrstuvwx', prefix: '' },
    })
  })

  it('rejects missing or malformed game IDs', () => {
    expect(parseWatchRoute('/watch/nope')).toBeNull()
    expect(parseWatchRoute('/v1/games/game_abcdefghijklmnopqrstuvwx')).toBeNull()
  })

  it('validates manual IDs and builds prefix-safe watch paths', () => {
    const gameId = normalizeGameId('  game_abcdefghijklmnopqrstuvwx  ')
    expect(gameId).toBe('game_abcdefghijklmnopqrstuvwx')
    expect(watchUrl('/freeciv', gameId!)).toBe(
      '/freeciv/watch/game_abcdefghijklmnopqrstuvwx',
    )
    expect(normalizeGameId('not a game id')).toBeNull()
  })

  it('preserves terminal outcome language instead of substituting a leader', () => {
    expect(gamePrimaryResult({
      ...mockWatch.game,
      state: 'invalid',
      outcome: { status: 'no_valid_winner', summary: 'No valid winner; Codex led at the last complete score' },
    })).toBe('No valid winner; Codex led at the last complete score')
  })

  it('uses truthful joined-aware labels for open lobby seats', () => {
    expect(placeLabel({
      ...mockWatch.game.resolved_places[0],
      joined: false,
      controller_label: null,
      model: null,
      player_name: 'AgentPlace1',
    })).toBe('Open agent seat')
  })

  it('hides only failed picker cards that never played a turn', () => {
    const failedAtZero = { ...mockWatch.game, state: 'failed', current_turn: 0 }
    const failedAfterPlay = { ...failedAtZero, game_id: 'game_failed_after_play_1234', current_turn: 3 }
    const liveLobby = { ...failedAtZero, game_id: 'game_live_lobby_123456789', state: 'lobby' }
    expect(visiblePickerGames([failedAtZero, failedAfterPlay, liveLobby]).map((game) => game.game_id)).toEqual([
      failedAfterPlay.game_id,
      liveLobby.game_id,
    ])
  })

  it('labels explicit turn timing without inferring old games', () => {
    expect(timingModeLabel({ timing_mode: 'default', action_timeout_s: 180 })).toBe('Default · 180s/turn')
    expect(timingModeLabel({ timing_mode: 'blitz', action_timeout_s: 60 })).toBe('Blitz · 60s/turn')
    expect(timingModeLabel({ timing_mode: 'infinite', action_timeout_s: null })).toBe('Infinite · no deadline')
    expect(timingModeLabel({ timing_mode: 'custom', action_timeout_s: 75 })).toBe('Custom · 75s/turn')
    expect(timingModeLabel({})).toBeNull()
  })

  it('names the control protocol and falls back to the server default', () => {
    expect(controlProtocolLabel('full-control-v2')).toEqual({
      protocol: 'full-control-v2',
      label: 'Full control',
      detail: 'Agents drive every unit',
      assumed: false,
    })
    expect(controlProtocolLabel('strategic-v1')).toEqual({
      protocol: 'strategic-v1',
      label: 'Strategic traits',
      detail: 'Agents steer the classic AI',
      assumed: false,
    })
    for (const legacy of [undefined, null, '', 'strategic-v9']) {
      expect(controlProtocolLabel(legacy)).toEqual({
        protocol: 'strategic-v1',
        label: 'Strategic traits',
        detail: 'Agents steer the classic AI',
        assumed: true,
      })
    }
  })

  it('folds the control protocol into the picker row subtitle', () => {
    expect(matchModeLabel({ mode: 'single', control_protocol: 'full-control-v2' }))
      .toBe('Single player vs native AI · Full control')
    expect(matchModeLabel({ mode: 'multiplayer', control_protocol: 'strategic-v1' }))
      .toBe('Multiplayer agent match · Strategic traits')
    expect(matchModeLabel({ mode: 'single' }))
      .toBe('Single player vs native AI · Strategic traits')
  })
})

describe('replay view model', () => {
  it('selects state and frames at or before the requested turn', () => {
    expect(snapshotAtOrBefore(mockReplay.snapshots, 2)?.turn).toBe(2)
    expect(snapshotAtOrBefore(mockReplay.snapshots, 99)?.turn).toBe(3)
    expect(frameAtOrBefore(mockWatch.frames, 3)?.turn).toBe(3)
    expect(frameAtOrBefore(mockWatch.frames, 2)).toBeUndefined()
  })

  it('attributes ruler-renamed seats through player_id, not names', () => {
    // The native save renames agent players to their rulers ("Elizabeth"
    // for AgentPlace1), so a name join misclassifies exactly the agent
    // seats as dynamic factions. In multiplayer, each renamed ruler must
    // still resolve to its own harness-model.
    const places = [
      { place: 1, seat_id: 'place-1', player_name: 'AgentPlace1',
        player_color: '#0067A5', controller: 'agent', joined: true,
        controller_label: 'pi-gpt-5.6-sol', controller_type: 'external',
        model: 'gpt-5.6-sol' },
      { place: 2, seat_id: 'place-2', player_name: 'AgentPlace2',
        player_color: '#F38400', controller: 'agent', joined: true,
        controller_label: 'claude-fable-5', controller_type: 'external',
        model: 'fable-5' },
    ] as typeof mockWatch.game.resolved_places
    const snapshot = {
      turn: 10, year: -3000,
      players: [
        { ...mockReplay.snapshots[0].players[0], player_id: 0,
          seat_id: 'place-1', player_name: 'Elizabeth', nation: 'English' },
        { ...mockReplay.snapshots[0].players[0], player_id: 1,
          seat_id: 'place-2', player_name: 'Isabella', nation: 'Spanish' },
      ],
    }
    const frame = {
      ...mockWatch.frames[0],
      map_players: [
        { player_id: 0, player_name: 'Elizabeth', player_color: '#0067A5' },
        { player_id: 1, player_name: 'Isabella', player_color: '#F38400' },
      ],
    }
    const factions = mapFactions(frame, snapshot, places)
    expect(factions.map((faction) => faction.display_label)).toEqual([
      'pi-gpt-5.6-sol: English', 'claude-fable-5: Spanish',
    ])
    expect(factions.every((faction) => !faction.dynamic)).toBe(true)
  })

  it('keeps dynamic map factions distinct from scored competitors', () => {
    const factions = mapFactions(
      mockWatch.frames[0], mockReplay.snapshots[2], mockWatch.game.resolved_places,
    )
    expect(factions.map((faction) => faction.display_label)).toEqual([
      'codex-gpt-5.6-sol: Danish', 'In-game Deity AI: Romans', 'Freeciv dynamic: Pirate',
    ])
    expect(factions[2]).toMatchObject({
      player_name: 'Blackbeard', player_color: '#FF1493',
      detail: 'Blackbeard · Pirate', dynamic: true,
    })
  })

  it('summarizes duplicate native seats in the match header', () => {
    const native = mockWatch.game.resolved_places[1]
    expect(matchHeaderLabel([
      mockWatch.game.resolved_places[0],
      native,
      { ...native, place: 3, seat_id: 'place-3', player_name: 'NativePlace3' },
    ])).toBe('codex-gpt-5.6-sol  vs  In-game Deity AI ×2')
  })

  it('builds the legacy map key from scored roster identities and colors', () => {
    const factions = configuredPlaceFactions(mockWatch.game.resolved_places)
    expect(factions.map(({ display_label, player_color }) => ({
      display_label, player_color,
    }))).toEqual([
      { display_label: 'codex-gpt-5.6-sol', player_color: '#0067A5' },
      { display_label: 'In-game Deity AI: NativePlace2', player_color: '#F38400' },
    ])
    expect(factions.every((faction) => !faction.dynamic)).toBe(true)
  })

  it('builds an exact-color faction key from replay data for legacy frames', () => {
    const legacyFrame = { ...mockWatch.frames[0], map_players: undefined }
    const snapshot = {
      ...mockReplay.snapshots[2],
      players: [
        ...mockReplay.snapshots[2].players,
        {
          ...mockReplay.snapshots[2].players[2],
          seat_id: 'dynamic-player-3', player_id: 3,
          player_name: 'Goldfinger', player_color: '#FFD700', nation: 'Barbarian',
        },
      ],
    }
    const factions = mapFactions(
      legacyFrame, snapshot, mockWatch.game.resolved_places,
    )

    expect(factions.map(({ player_name, player_color, dynamic }) => ({
      player_name, player_color, dynamic,
    }))).toEqual([
      { player_name: 'AgentPlace1', player_color: '#0067A5', dynamic: false },
      { player_name: 'NativePlace2', player_color: '#F38400', dynamic: false },
      { player_name: 'Blackbeard', player_color: '#FF1493', dynamic: true },
      { player_name: 'Goldfinger', player_color: '#FFD700', dynamic: true },
    ])
  })

  it('normalizes citizen population and technology states', () => {
    const player = mockReplay.snapshots[2].players[0]
    expect(playerMetric(player, 'citizens')).toBe(6)
    expect(technologyState(mockReplay.catalog!.technologies[0], player)).toBe('known')
    expect(technologyState(mockReplay.catalog!.technologies[1], player)).toBe('current')
    expect(maxKnownTechnologyDepth(player, [])).toBeNull()
    expect(maxKnownTechnologyDepth(
      { ...player, known_tech_ids: [] }, mockReplay.catalog!.technologies,
    )).toBeNull()
  })

  it('uses authoritative scoreboard values and labels terminal scores', () => {
    expect(scoreDisplay('invalid', 94, 0)).toEqual({
      label: 'FINAL SCORE', value: 94,
    })
    expect(scoreDisplay('completed', 136, 120)).toEqual({
      label: 'FINAL SCORE', value: 136,
    })
    expect(scoreDisplay('running', 80, 42)).toEqual({
      label: 'SCORE', value: 80,
    })
    expect(scoreDisplay('running', undefined, 42)).toEqual({
      label: 'SCORE', value: 42,
    })
    expect(scoreDisplay('invalid', undefined, 42)).toEqual({
      label: 'FINAL SCORE UNAVAILABLE', value: null,
    })
    expect(scoreDisplay('failed', undefined, undefined)).toEqual({
      label: 'FINAL SCORE UNAVAILABLE', value: null,
    })
    expect(scoreDisplayAtTurn('completed', 136, 42, 2, 10)).toEqual({
      label: 'SCORE AT TURN', value: 42,
    })
  })
})
