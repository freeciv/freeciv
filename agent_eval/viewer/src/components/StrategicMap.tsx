import { lazy, Suspense, useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { fetchBoard } from '../api'
import { LatestRequestGate, LruCache, priorAvailableTurns } from '../board-loader'
import type { BoardResponse, RouteContext } from '../types'
import type { BoardActions, BoardViewMode } from './ThreeBoard'

const ThreeBoard = lazy(() => import('./ThreeBoard').then((module) => ({
  default: module.ThreeBoard,
})))

type RenderMode = BoardViewMode | 'raw'
type LoadStatus = 'idle' | 'loading' | 'ready' | 'unavailable'

interface StrategicMapProps {
  alt: string
  availableTurns: readonly number[]
  rawSourceName?: string
  rawSrc?: string
  route: RouteContext
  turn: number
}

const boardCache = new LruCache<BoardResponse>(18)

function cacheKey(route: RouteContext, turn: number): string {
  return `${route.prefix}\0${route.gameId}\0${turn}`
}

function neighboringTurns(turns: readonly number[], turn: number): number[] {
  const index = turns.indexOf(turn)
  if (index < 0) return []
  return [turns[index - 1], turns[index + 1]].filter(
    (candidate): candidate is number => typeof candidate === 'number',
  )
}

export function StrategicMap({
  alt,
  availableTurns,
  rawSourceName,
  rawSrc,
  route,
  turn,
}: StrategicMapProps) {
  const actionsRef = useRef<BoardActions | null>(null)
  const requestGate = useRef(new LatestRequestGate())
  const [mode, setMode] = useState<RenderMode>('political')
  const [status, setStatus] = useState<LoadStatus>('idle')
  const [committedBoard, setCommittedBoard] = useState<BoardResponse | null>(null)
  const [renderedBoard, setRenderedBoard] = useState<BoardResponse | null>(null)
  const [loadError, setLoadError] = useState<string | null>(null)
  const [webglFailed, setWebglFailed] = useState(false)
  const key = useMemo(() => cacheKey(route, turn), [route, turn])

  useEffect(() => {
    const generation = requestGate.current.begin()
    const cached = boardCache.get(key)
    if (cached) {
      setCommittedBoard(cached)
      setStatus('ready')
      setLoadError(null)
      return
    }
    const controller = new AbortController()
    setStatus('loading')
    setLoadError(null)
    void (async () => {
      let exactError = 'Semantic board unavailable'
      try {
        const board = await fetchBoard(route, turn, controller.signal)
        if (board.game_id !== route.gameId || board.turn !== turn) {
          throw new Error('The semantic board did not match the selected turn')
        }
        boardCache.set(key, board)
        if (!requestGate.current.isCurrent(generation)) return
        setCommittedBoard(board)
        setStatus('ready')
        return
      } catch (reason: unknown) {
        if (controller.signal.aborted || !requestGate.current.isCurrent(generation)) return
        exactError = reason instanceof Error ? reason.message : exactError
      }

      for (const candidate of priorAvailableTurns(availableTurns, turn)) {
        if (controller.signal.aborted || !requestGate.current.isCurrent(generation)) return
        const candidateKey = cacheKey(route, candidate)
        const cachedPrior = boardCache.get(candidateKey)
        if (cachedPrior) {
          setCommittedBoard(cachedPrior)
          setStatus('unavailable')
          setLoadError(exactError)
          return
        }
        try {
          const board = await fetchBoard(route, candidate, controller.signal)
          if (board.game_id !== route.gameId || board.turn !== candidate) continue
          boardCache.set(candidateKey, board)
          if (!requestGate.current.isCurrent(generation)) return
          setCommittedBoard(board)
          setStatus('unavailable')
          setLoadError(exactError)
          return
        } catch {
          // A replay turn may not have a matching save. Keep walking backward.
        }
      }
      if (!controller.signal.aborted && requestGate.current.isCurrent(generation)) {
        setStatus('unavailable')
        setLoadError(exactError)
      }
    })()
    return () => controller.abort()
  }, [availableTurns, key, route, turn])

  useEffect(() => {
    if (status !== 'ready') return
    const timer = window.setTimeout(() => {
      for (const candidate of neighboringTurns(availableTurns, turn)) {
        const candidateKey = cacheKey(route, candidate)
        if (boardCache.has(candidateKey)) continue
        fetchBoard(route, candidate).then((board) => {
          if (board.game_id === route.gameId && board.turn === candidate) {
            boardCache.set(candidateKey, board)
          }
        }).catch(() => {
          // Neighbor prefetch is opportunistic; selected-turn errors are shown above.
        })
      }
    }, 160)
    return () => window.clearTimeout(timer)
  }, [availableTurns, route, status, turn])

  const handleCommit = useCallback((nextBoard: BoardResponse) => {
    setRenderedBoard(nextBoard)
  }, [])
  const handleWebglFailure = useCallback(() => setWebglFailed(true), [])
  const rawAvailable = Boolean(rawSrc)
  const rawFallback = rawAvailable && ((!committedBoard && status === 'unavailable') || webglFailed)
  const effectiveMode: RenderMode = mode === 'raw' && rawAvailable
    ? 'raw'
    : rawFallback ? 'raw' : mode === 'raw' ? 'political' : mode
  const boardMode = effectiveMode !== 'raw'
  const semanticUnavailable = webglFailed && !rawAvailable
  const displayedBoard = renderedBoard ?? committedBoard
  const transitionPending = boardMode && !semanticUnavailable
    && committedBoard !== null
    && status !== 'unavailable'
    && (committedBoard.turn !== turn || renderedBoard?.turn !== turn)
  const transitionFailed = boardMode
    && committedBoard !== null
    && status === 'unavailable'
    && committedBoard.turn !== turn

  return (
    <div className="strategic-map strategic-map-board">
      <div className="map-tools" aria-label="Map presentation controls">
        <div className="map-tool-group" aria-label="Map appearance">
          <span>View</span>
          <button aria-pressed={effectiveMode === 'political'} onClick={() => setMode('political')} type="button">Political</button>
          <button aria-pressed={effectiveMode === 'terrain'} onClick={() => setMode('terrain')} type="button">Terrain</button>
          <button aria-pressed={effectiveMode === 'raw'} disabled={!rawAvailable} onClick={() => setMode('raw')} type="button">Raw source</button>
        </div>
        <div className="map-tool-group" aria-label="Map zoom">
          <span>Camera</span>
          <button disabled={!boardMode || semanticUnavailable} onClick={() => actionsRef.current?.fit()} type="button">Fit</button>
          <button aria-label="Zoom in" disabled={!boardMode || semanticUnavailable} onClick={() => actionsRef.current?.zoomIn()} type="button">+</button>
          <button aria-label="Zoom out" disabled={!boardMode || semanticUnavailable} onClick={() => actionsRef.current?.zoomOut()} type="button">−</button>
        </div>
      </div>

      <div className="map-stage">
        {boardMode && committedBoard && !semanticUnavailable ? (
          <Suspense fallback={<div className="map-board-loading" role="status"><strong>Loading board renderer</strong><span>Preparing the local Three.js scene.</span></div>}>
            <ThreeBoard
              actionsRef={actionsRef}
              alt={alt}
              board={committedBoard}
              mode={effectiveMode === 'terrain' ? 'terrain' : 'political'}
              onCommit={handleCommit}
              onFailure={handleWebglFailure}
            />
          </Suspense>
        ) : effectiveMode === 'raw' && rawSrc ? (
          <div className="map-viewport map-raw-viewport">
            <div className="map-surface">
              <img alt={alt} className="map-render map-render-visible" src={rawSrc} />
            </div>
          </div>
        ) : (
          <div className="map-board-loading" role="status">
            <strong>{status === 'unavailable' || semanticUnavailable ? 'Semantic board unavailable' : 'Building semantic world board'}</strong>
            <span>{status === 'unavailable' || semanticUnavailable
              ? loadError ?? (semanticUnavailable ? 'WebGL could not create the semantic board.' : `No saved board exists for turn ${turn}.`)
              : `Reading terrain, territory, cities, and units for turn ${turn}.`}</span>
          </div>
        )}
        {transitionPending && (
          <div className="map-turn-buffer" role="status">
            Preparing turn {turn} · showing turn {renderedBoard?.turn ?? committedBoard?.turn}
          </div>
        )}
        {transitionFailed && (
          <div className="map-turn-buffer map-turn-buffer-error" role="status">
            Turn {turn} board unavailable · showing turn {renderedBoard?.turn ?? committedBoard.turn}
          </div>
        )}
        {rawFallback && (
          <p className="map-fallback-note" role="status">
            Semantic WebGL board unavailable{loadError ? `: ${loadError}` : ''}. Showing the raw compatibility frame.
          </p>
        )}
      </div>

      <div className="map-source-note">
        {boardMode && displayedBoard && !semanticUnavailable ? (
          <span>
            Native save data · {(displayedBoard.width * displayedBoard.height).toLocaleString()} tiles · {displayedBoard.cities.length.toLocaleString()} cities · {displayedBoard.unit_stacks.length.toLocaleString()} unit stacks
          </span>
        ) : effectiveMode === 'raw'
          ? <span>Raw mapimg compatibility frame. It is loaded only in this view.</span>
          : <span>Semantic board unavailable for this turn.</span>}
        <code title={boardMode ? `semantic turn ${renderedBoard?.turn ?? committedBoard?.turn ?? turn}` : rawSourceName}>
          {boardMode ? `semantic turn ${renderedBoard?.turn ?? committedBoard?.turn ?? turn}` : rawSourceName}
        </code>
      </div>
    </div>
  )
}
