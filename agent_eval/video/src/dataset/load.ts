/**
 * Dataset loading for both Remotion phases.
 *
 * `loadMeta` runs inside `calculateMetadata`, where the composition's length is
 * decided from the turn count. `useFilm` runs inside the composition and pulls
 * the multi-megabyte `frames.json` exactly once per renderer page, holding the
 * render open with `delayRender` until the film is built.
 *
 * Everything is read through `staticFile`, so a render never touches the
 * network or the live run directory.
 */

import { useEffect, useState } from 'react'
import { cancelRender, continueRender, delayRender, staticFile } from 'remotion'
import { buildFilm, type Film } from './film'
import { parseEvents, parseFrames, parseMeta, type DatasetMeta } from './schema'

export function datasetPath(gameId: string, file: string): string {
  return staticFile(`exports/${gameId}/${file}`)
}

async function fetchJson(url: string): Promise<unknown> {
  const response = await fetch(url)
  if (!response.ok) {
    throw new Error(`cannot read ${url}: HTTP ${response.status}`)
  }
  return await response.json() as unknown
}

export async function loadMeta(gameId: string): Promise<DatasetMeta> {
  return parseMeta(await fetchJson(datasetPath(gameId, 'meta.json')))
}

const filmCache = new Map<string, Promise<Film>>()

/** Meta plus every turn, parsed once per renderer page and reused per frame. */
export function loadFilm(gameId: string): Promise<Film> {
  const cached = filmCache.get(gameId)
  if (cached) return cached
  const pending = (async (): Promise<Film> => {
    const [meta, framesJson, eventsJson] = await Promise.all([
      loadMeta(gameId),
      fetchJson(datasetPath(gameId, 'frames.json')),
      // An export predating the event log still renders, just without captions.
      fetchJson(datasetPath(gameId, 'events.json')).catch(() => ({ events: [] })),
    ])
    return buildFilm(meta, parseFrames(framesJson), parseEvents(eventsJson))
  })()
  filmCache.set(gameId, pending)
  return pending
}

/** Resolve the film for a composition, keeping the frame open until it lands. */
export function useFilm(gameId: string): Film | null {
  const [film, setFilm] = useState<Film | null>(null)
  const [handle] = useState(() => delayRender(`loading dataset ${gameId}`))

  useEffect(() => {
    let live = true
    loadFilm(gameId)
      .then((loaded) => {
        if (!live) return
        setFilm(loaded)
        continueRender(handle)
      })
      .catch((reason: unknown) => {
        cancelRender(reason instanceof Error ? reason : new Error(String(reason)))
      })
    return () => {
      live = false
    }
  }, [gameId, handle])

  return film
}
