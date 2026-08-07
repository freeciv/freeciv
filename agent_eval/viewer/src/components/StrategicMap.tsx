import { lazy, Suspense, useCallback, useEffect, useMemo, useRef, useState, type CSSProperties } from 'react'
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
  // Shape of the rendered world, so the stage can be the map's own rectangle
  // rather than a fixed slab the map is letterboxed into. Null until a board
  // reports, and only meaningful for the WebGL board -- the raw-source image
  // and the loading states keep the default stage.
  const [boardAspect, setBoardAspect] = useState<number | null>(null)

  // Fullscreen promotes the whole app shell, not the stage alone.
  //
  // Fullscreen renders only the target's subtree, and the transport -- prev,
  // play, next, scrub, scores -- is a sibling of the map, fixed to the bottom
  // of the page. Fullscreening the stage produced a board you could not drive.
  // The shell contains both, and a `position: fixed` bar inside a fullscreen
  // ancestor pins to the screen edge, so the real transport comes along with
  // its real state rather than a second copy that has to be kept in sync.
  // CSS hides everything else in the shell for the duration.
  //
  // The board reframes itself for free: ThreeBoard watches its container with
  // a ResizeObserver, so entering and leaving both land on a correct fit.
  const stageRef = useRef<HTMLDivElement | null>(null)
  const [fullscreen, setFullscreen] = useState(false)
  useEffect(() => {
    const sync = () => setFullscreen(
      document.fullscreenElement != null
      && document.fullscreenElement === stageRef.current?.closest('.app-shell'),
    )
    document.addEventListener('fullscreenchange', sync)
    return () => document.removeEventListener('fullscreenchange', sync)
  }, [])
  const toggleFullscreen = useCallback(() => {
    const shell = stageRef.current?.closest('.app-shell')
    if (!(shell instanceof HTMLElement)) return
    if (document.fullscreenElement === shell) void document.exitFullscreen()
    // A rejected request is not an error worth surfacing -- the browser refuses
    // it outside a user gesture, and the map is still perfectly usable inline.
    else void shell.requestFullscreen?.().catch(() => undefined)
  }, [])
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
      <div className="flex flex-wrap justify-between gap-x-4 gap-y-[7px] min-h-[42px] py-[7px] px-2.5 border-b border-b-line bg-[var(--color-page)] max-[760px]:items-start max-[760px]:flex-col" aria-label="Map presentation controls">
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
          <button
            aria-pressed={fullscreen}
            disabled={!boardMode || semanticUnavailable}
            onClick={toggleFullscreen}
            type="button"
          >{fullscreen ? 'Exit full screen' : 'Full screen'}</button>
        </div>
      </div>

      <div
        className="map-stage"
        ref={stageRef}
        style={boardMode && boardAspect && !semanticUnavailable && !fullscreen
          ? ({
            // Inline, because the stylesheet sets an explicit height twice and
            // this has to beat both. Width stays the definite axis -- given
            // both dimensions free, aspect-ratio is entitled to satisfy itself
            // by growing the width, which once pushed the stage out of its
            // grid column and over the rail.
            width: '100%',
            height: 'auto',
            aspectRatio: String(boardAspect),
            minHeight: '300px',
            maxHeight: '76vh',
          } satisfies CSSProperties)
          : undefined}
      >
        {boardMode && committedBoard && !semanticUnavailable ? (
          <Suspense fallback={<div className="map-board-loading" role="status"><strong>Loading board renderer</strong><span>Preparing the local Three.js scene.</span></div>}>
            <ThreeBoard
              actionsRef={actionsRef}
              alt={alt}
              board={committedBoard}
              mode={effectiveMode === 'terrain' ? 'terrain' : 'political'}
              onAspect={setBoardAspect}
              onCommit={handleCommit}
              onFailure={handleWebglFailure}
            />
          </Suspense>
        ) : effectiveMode === 'raw' && rawSrc ? (
          <div className="grid w-full h-full p-[clamp(14px,2.7vw,30px)] overflow-auto place-items-center [scrollbar-color:var(--color-line-2)_var(--color-page)] [scrollbar-width:thin] max-[760px]:p-2.5">
            <div className="relative grid place-items-center w-full h-full max-w-full max-h-full [filter:drop-shadow(0_2px_0_rgba(221,194,144,.42))_drop-shadow(0_17px_25px_rgba(0,0,0,.6))]">
              <img alt={alt} className="[grid-area:1/1] block absolute inset-0 w-full h-full max-w-full max-h-full object-contain" src={rawSrc} />
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
          <div className="absolute top-2.5 right-2.5 z-[3] py-1.5 px-2 text-[var(--color-ink)] bg-[rgba(23,20,15,.86)] font-bold text-[8px] leading-none font-readout tracking-[.04em] pointer-events-none border border-[rgba(197,164,109,.45)]" role="status">
            Preparing turn {turn} · showing turn {renderedBoard?.turn ?? committedBoard?.turn}
          </div>
        )}
        {transitionFailed && (
          <div className="absolute top-2.5 right-2.5 z-[3] py-1.5 px-2 bg-[rgba(23,20,15,.86)] font-bold text-[8px] leading-none font-readout tracking-[.04em] pointer-events-none border border-[rgba(192,103,85,.58)] text-[var(--color-red)]" role="status">
            Turn {turn} board unavailable · showing turn {renderedBoard?.turn ?? committedBoard.turn}
          </div>
        )}
        {rawFallback && (
          <p className="absolute right-2.5 bottom-2.5 max-w-[min(360px,calc(100%-20px))] m-0 py-[7px] px-[9px] border border-[var(--color-line-2)] text-[var(--color-ink)] bg-[rgba(30,24,16,.93)] text-[9px]" role="status">
            Semantic WebGL board unavailable{loadError ? `: ${loadError}` : ''}. Showing the raw compatibility frame.
          </p>
        )}
      </div>

      <div className="grid grid-cols-[minmax(0,1fr)_auto] gap-3 items-center py-2 px-3 border-t border-t-[var(--color-line)] text-[var(--color-muted)] bg-[var(--color-page)] text-[8px] max-[760px]:grid-cols-[minmax(0,1fr)]">
        {boardMode && displayedBoard && !semanticUnavailable ? (
          <span>
            Native save data · {(displayedBoard.width * displayedBoard.height).toLocaleString()} tiles · {displayedBoard.cities.length.toLocaleString()} cities · {displayedBoard.unit_stacks.length.toLocaleString()} unit stacks
          </span>
        ) : effectiveMode === 'raw'
          ? <span>Raw mapimg compatibility frame. It is loaded only in this view.</span>
          : <span>Semantic board unavailable for this turn.</span>}
        <code className="max-w-[320px] overflow-hidden text-[var(--color-line-2)] text-[7px] text-ellipsis whitespace-nowrap max-[760px]:max-w-full" title={boardMode ? `semantic turn ${renderedBoard?.turn ?? committedBoard?.turn ?? turn}` : rawSourceName}>
          {boardMode ? `semantic turn ${renderedBoard?.turn ?? committedBoard?.turn ?? turn}` : rawSourceName}
        </code>
      </div>
    </div>
  )
}
