import { apiUrl, arenaApiUrl } from './route'
import type {
  ArenaRouteContext,
  BoardResponse,
  GameEventsResponse,
  GamesIndexResponse,
  ReplayResponse,
  ReplaySnapshot,
  RouteContext,
  TechnologyCatalog,
  WatchResponse,
} from './types'

export class HTTPError extends Error {
  constructor(readonly status: number, message: string) {
    super(message)
    this.name = 'HTTPError'
  }
}

async function fetchJson<T>(url: string, signal?: AbortSignal): Promise<T> {
  const response = await fetch(url, { cache: 'no-store', signal })
  if (!response.ok) {
    let detail = `${response.status} ${response.statusText}`
    try {
      const payload = (await response.json()) as { error?: string }
      if (payload.error) detail = payload.error
    } catch {
      // Keep the stable HTTP status when a proxy returns non-JSON.
    }
    throw new HTTPError(response.status, detail)
  }
  return (await response.json()) as T
}

export function fetchGames(
  route: ArenaRouteContext,
  signal?: AbortSignal,
): Promise<GamesIndexResponse> {
  return fetchJson<GamesIndexResponse>(arenaApiUrl(route, '/v1/games'), signal)
}

export function fetchWatch(
  route: RouteContext,
  signal?: AbortSignal,
): Promise<WatchResponse> {
  return fetchJson<WatchResponse>(
    apiUrl(route, `/v1/games/${encodeURIComponent(route.gameId)}/watch.json`),
    signal,
  ).then((payload) => ({
    ...payload,
    frames: (payload.frames ?? []).map((frame) => ({
      ...frame,
      turn: legacyFrameTurn(frame),
    })),
  }))
}

export function fetchEvents(
  route: RouteContext,
  signal?: AbortSignal,
): Promise<GameEventsResponse> {
  return fetchJson<GameEventsResponse>(
    apiUrl(route, `/v1/games/${encodeURIComponent(route.gameId)}/events.json`),
    signal,
  )
}

export function fetchBoard(
  route: RouteContext,
  turn: number,
  signal?: AbortSignal,
): Promise<BoardResponse> {
  const query = new URLSearchParams({ turn: String(Math.max(0, Math.trunc(turn))) })
  return fetchJson<BoardResponse>(
    apiUrl(
      route,
      `/v1/games/${encodeURIComponent(route.gameId)}/board.json?${query}`,
    ),
    signal,
  )
}

export function legacyFrameTurn(frame: { turn?: number | null; source_name: string }): number | null {
  if (typeof frame.turn === 'number' && Number.isFinite(frame.turn) && frame.turn >= 0) {
    return Math.trunc(frame.turn)
  }
  const match = frame.source_name.match(/^turn-(\d+)(?:-|\.|$)/)
  return match ? Number(match[1]) : null
}

export interface ReplayBatch {
  snapshots: ReplaySnapshot[]
  catalog?: TechnologyCatalog
  warnings: { turn?: number | null; message: string }[]
  available: boolean
  complete: boolean
}

export async function fetchReplaySince(
  route: RouteContext,
  afterTurn: number,
  signal?: AbortSignal,
): Promise<ReplayBatch> {
  const snapshots: ReplaySnapshot[] = []
  const warnings: { turn?: number | null; message: string }[] = []
  let catalog: TechnologyCatalog | undefined
  let cursor = afterTurn
  let available = false
  let complete = false

  for (let page = 0; page < 1000; page += 1) {
    const query = new URLSearchParams({
      after_turn: String(cursor),
      limit: '250',
    })
    const payload = await fetchJson<ReplayResponse>(
      apiUrl(
        route,
        `/v1/games/${encodeURIComponent(route.gameId)}/replay.json?${query}`,
      ),
      signal,
    )
    available ||= payload.available
    complete ||= Boolean(payload.complete)
    catalog = payload.catalog ?? catalog
    snapshots.push(...payload.snapshots)
    warnings.push(...(payload.replay_warnings ?? payload.warnings ?? []))
    if (!payload.has_more) break
    if (payload.next_after_turn <= cursor) {
      throw new Error('Replay pagination did not advance')
    }
    cursor = payload.next_after_turn
  }

  return { snapshots, catalog, warnings, available, complete }
}

export interface OptionalReplayLoad {
  watch: WatchResponse
  replay: ReplayBatch | null
  replayError: string | null
  replayUnavailable: boolean
}

export async function fetchWatchWithOptionalReplay(
  route: RouteContext,
  afterTurn: number,
  signal?: AbortSignal,
): Promise<OptionalReplayLoad> {
  const [watch, replay] = await Promise.allSettled([
    fetchWatch(route, signal),
    fetchReplaySince(route, afterTurn, signal),
  ])
  if (watch.status === 'rejected') throw watch.reason
  if (replay.status === 'fulfilled') {
    return {
      watch: watch.value,
      replay: replay.value,
      replayError: null,
      replayUnavailable: false,
    }
  }
  if (signal?.aborted) throw replay.reason
  return {
    watch: watch.value,
    replay: null,
    replayError: replay.reason instanceof Error
      ? replay.reason.message
      : 'Replay telemetry is unavailable',
    replayUnavailable: replay.reason instanceof HTTPError && replay.reason.status === 404,
  }
}
