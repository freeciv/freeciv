import type { ArenaRouteContext, ReplayFrame, RouteContext } from './types'

export type ViewerRoute =
  | { kind: 'arena'; context: ArenaRouteContext }
  | { kind: 'arena-redirect'; target: string }
  | { kind: 'watch'; context: RouteContext }
  | { kind: 'not-found' }

export function parseWatchRoute(pathname: string): RouteContext | null {
  const match = pathname.match(/^(.*)\/watch\/([^/]+)\/?$/)
  if (!match) return null
  try {
    const gameId = decodeURIComponent(match[2])
    if (!/^[A-Za-z0-9_-]{20,80}$/.test(gameId)) return null
    return { prefix: match[1].replace(/\/$/, ''), gameId }
  } catch {
    return null
  }
}

export function apiUrl(route: RouteContext, path: string): string {
  const suffix = path.startsWith('/') ? path : `/${path}`
  return `${route.prefix}${suffix}` || '/'
}

export function resolveViewerRoute(pathname: string): ViewerRoute {
  const watch = parseWatchRoute(pathname)
  if (watch) return { kind: 'watch', context: watch }
  if (!pathname.startsWith('/') || pathname.includes('/watch/')) {
    return { kind: 'not-found' }
  }
  if (pathname !== '/' && !pathname.endsWith('/')) {
    return { kind: 'arena-redirect', target: `${pathname}/` }
  }
  const prefix = pathname === '/' ? '' : pathname.replace(/\/$/, '')
  return { kind: 'arena', context: { prefix } }
}

export function arenaApiUrl(route: ArenaRouteContext, path: string): string {
  const suffix = path.startsWith('/') ? path : `/${path}`
  return `${route.prefix}${suffix}` || '/'
}

export function watchUrl(prefix: string, gameId: string): string {
  return `${prefix}/watch/${encodeURIComponent(gameId)}` || '/'
}

export function frameImageUrl(route: RouteContext, frame: ReplayFrame): string {
  return apiUrl(
    route,
    `/v1/games/${encodeURIComponent(route.gameId)}/frames/${frame.index}.png`,
  )
}
