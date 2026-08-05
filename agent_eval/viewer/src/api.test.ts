import { afterEach, describe, expect, it, vi } from 'vitest'
import { fetchBoard, fetchGames, fetchWatchWithOptionalReplay } from './api'
import { mockWatch } from './mock'

afterEach(() => {
  vi.unstubAllGlobals()
})

describe('arena game index', () => {
  it('loads the same-origin public index under a proxy prefix', async () => {
    const payload = { schema_version: 1, games: [] }
    const fetchMock = vi.fn().mockResolvedValue(new Response(JSON.stringify(payload), {
      status: 200,
      headers: { 'Content-Type': 'application/json' },
    }))
    vi.stubGlobal('fetch', fetchMock)

    await expect(fetchGames({ prefix: '/freeciv' })).resolves.toEqual(payload)
    expect(fetchMock).toHaveBeenCalledWith('/freeciv/v1/games', {
      cache: 'no-store', signal: undefined,
    })
  })

  it('surfaces an index error while the picker can retain manual entry', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(
      JSON.stringify({ error: 'game index unavailable' }),
      { status: 404, statusText: 'Not Found' },
    )))

    await expect(fetchGames({ prefix: '' })).rejects.toThrow('game index unavailable')
  })
})

describe('semantic board endpoint', () => {
  it('loads exactly one selected turn through the same-origin prefix', async () => {
    const payload = { schema_version: 1, game_id: 'game_abcdefghijklmnop', turn: 42 }
    const fetchMock = vi.fn().mockResolvedValue(new Response(JSON.stringify(payload), { status: 200 }))
    vi.stubGlobal('fetch', fetchMock)

    await expect(fetchBoard({
      prefix: '/freeciv', gameId: 'game_abcdefghijklmnop',
    }, 42)).resolves.toEqual(payload)
    expect(fetchMock).toHaveBeenCalledWith(
      '/freeciv/v1/games/game_abcdefghijklmnop/board.json?turn=42',
      { cache: 'no-store', signal: undefined },
    )
  })
})

describe('legacy watch compatibility', () => {
  it('keeps watch data when replay is missing and derives frame turns', async () => {
    const legacyWatch = {
      ...mockWatch,
      frames: mockWatch.frames.map(({ turn: _turn, ...frame }) => frame),
    }
    vi.stubGlobal('fetch', vi.fn().mockImplementation((input: string) => {
      if (String(input).includes('/watch.json')) {
        return Promise.resolve(new Response(JSON.stringify(legacyWatch), { status: 200 }))
      }
      return Promise.resolve(new Response(
        JSON.stringify({ error: 'not found' }),
        { status: 404, statusText: 'Not Found' },
      ))
    }))

    const load = await fetchWatchWithOptionalReplay(
      { prefix: '', gameId: mockWatch.game.game_id }, 0,
    )
    expect(load.watch.game.game_id).toBe(mockWatch.game.game_id)
    expect(load.watch.frames[0].turn).toBe(3)
    expect(load.replay).toBeNull()
    expect(load.replayError).toBe('not found')
    expect(load.replayUnavailable).toBe(true)
  })
})
