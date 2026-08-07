import { FormEvent, useEffect, useMemo, useState } from 'react'
import { fetchGames } from '../api'
import {
  gamePrimaryResult,
  matchModeLabel,
  normalizeGameId,
  openAgentSeats,
  placeLabel,
  sortedGames,
  timingModeLabel,
  visiblePickerGames,
  waitingLabel,
} from '../picker-model'
import { DisplayPaletteProvider, useDisplayPalette } from '../display-palette'
import { displayPlayerColor } from '../display-color'
import { watchUrl } from '../route'
import type { ArenaRouteContext, GameSummary } from '../types'
import { matchHeaderLabel } from '../view-model'
import { ColorMark } from './ColorMark'

/** Seat-strip cells share every rule except the "no agent yet" hatching. */
const SEAT_CELL = 'grid grid-cols-[auto_minmax(0,1fr)] gap-[9px] items-center py-[9px] px-[11px]'
const SEAT_TAKEN = 'bg-[var(--color-page)]'
const SEAT_OPEN = 'opacity-55 bg-[repeating-linear-gradient(-45deg,var(--color-page),var(--color-page)_5px,var(--color-panel)_5px,var(--color-panel)_10px)]'
const CLAMPED_LINE = 'block min-w-0 overflow-hidden text-ellipsis whitespace-nowrap'
const META_LABEL = 'block overflow-hidden text-ellipsis whitespace-nowrap mb-[5px] text-[var(--color-muted)] font-bold text-[7px] leading-none font-readout tracking-[.08em] uppercase'
const ROW_LABEL = 'text-[var(--color-muted)] font-bold text-[7px] leading-none font-readout tracking-[.08em] uppercase'

function stateLabel(state: string) {
  if (state === 'running' || state === 'starting') return 'Live'
  if (state === 'lobby') return 'Lobby'
  if (state === 'completed') return 'Complete'
  if (state === 'interrupted') return 'Interrupted'
  return state.replaceAll('_', ' ')
}

function validityLabel(value: boolean | null) {
  if (value === true) return 'Valid evaluation'
  if (value === false) return 'Invalid evaluation'
  return 'Validity pending'
}

/** Returned as whole literals so Tailwind can statically extract the classes. */
function validityTone(value: boolean | null): string {
  if (value === true) return 'text-[var(--color-green)]'
  if (value === false) return 'text-red'
  return 'text-[var(--color-muted)]'
}

/**
 * The card's left edge, in the colours this match resolved for its seats.
 *
 * Lives inside the provider so it reads the same plan `ColorMark` does -- the
 * rail and the seat dots can never disagree about who is which colour.
 */
function FactionRail({ places }: { places: GameSummary['resolved_places'] }) {
  const palette = useDisplayPalette()
  return (
    <span aria-hidden="true" className="faction-rail">
      {places.map((place) => (
        <span
          key={place.seat_id}
          style={{ background: displayPlayerColor(place.player_color, palette) ?? 'var(--color-line-2)' }}
        />
      ))}
    </span>
  )
}

function ArenaCard({ game, prefix }: { game: GameSummary; prefix: string }) {
  const waiting = waitingLabel(game)
  const timing = timingModeLabel(game)
  const turn = game.current_turn ?? 0
  const turnProgress = Math.min(100, Math.max(0, (turn / Math.max(game.turns, 1)) * 100))
  // Clearance is decided per match, so every card carries its own plan. The
  // index knows only the configured places, which is the whole seat roster.
  const factions = game.resolved_places.map((place) => ({
    playerId: place.place - 1,
    color: place.player_color,
  }))

  return (
    <DisplayPaletteProvider factions={factions}>
    <a className="arena-card group" href={watchUrl(prefix, game.game_id)}>
      <FactionRail places={game.resolved_places} />
      <div className="flex justify-between gap-3 items-center">
        <span className={`state-pill state-${game.state}`}><i />{stateLabel(game.state)}</span>
        <span className={`font-bold text-[8px] leading-none font-readout tracking-[.08em] uppercase ${validityTone(game.benchmark_valid)}`}>
          {validityLabel(game.benchmark_valid)}
        </span>
      </div>
      <div className="grid grid-cols-[1fr_auto] gap-[14px] items-start my-[19px] mx-0">
        <div>
          <p className="eyebrow">{matchModeLabel(game)}</p>
          <h2 className="m-0 text-[length:clamp(19px,2.2vw,27px)] leading-[1.15] tracking-[-.04em]">{matchHeaderLabel(game.resolved_places)}</h2>
        </div>
        <span
          aria-hidden="true"
          className="text-[var(--color-muted)] text-[21px] transition-[color,transform] duration-[.18s] group-hover:text-[var(--color-ink)] group-hover:[transform:translate(2px,-2px)]"
        >↗</span>
      </div>

      <div className="grid gap-px overflow-hidden border border-line bg-line" aria-label="Controllers and seats">
        {game.resolved_places.map((place) => (
          <div className={place.joined || place.controller === 'native_classic_ai' ? `${SEAT_CELL} ${SEAT_TAKEN}` : `${SEAT_CELL} ${SEAT_OPEN}`} key={place.seat_id}>
            <ColorMark color={place.player_color} label={placeLabel(place)} size="sm" />
            <span className={CLAMPED_LINE}><strong className={`${CLAMPED_LINE} text-[11px]`}>{placeLabel(place)}</strong><small className={`${CLAMPED_LINE} mt-0.5 text-[var(--color-muted)] text-[8px]`}>{place.model || place.player_name}</small></span>
          </div>
        ))}
      </div>

      <div className="grid grid-cols-[.55fr_.6fr_.65fr_1.4fr] gap-px mt-[14px] border border-[var(--color-line)] bg-[var(--color-line)] max-[1100px]:grid-cols-2">
        <div className="min-w-0 p-2.5 bg-[var(--color-page)]"><small className={META_LABEL}>Places</small><strong className={`${CLAMPED_LINE} text-[11px]`}>{game.places}</strong></div>
        <div className="min-w-0 p-2.5 bg-[var(--color-page)]"><small className={META_LABEL}>Agents</small><strong className={`${CLAMPED_LINE} text-[11px]`}>{game.joined_agents}/{game.max_agents}</strong></div>
        <div className="min-w-0 p-2.5 bg-[var(--color-page)]"><small className={META_LABEL}>Open seats</small><strong className={`${CLAMPED_LINE} text-[11px]`}>{openAgentSeats(game)}</strong></div>
        <div className="min-w-0 p-2.5 bg-[var(--color-page)]"><small className={META_LABEL}>{game.state === 'running' ? 'Leader' : 'Outcome'}</small><strong className={`${CLAMPED_LINE} text-[11px]`}>{gamePrimaryResult(game)}</strong></div>
      </div>

      {timing && (
        <div className="flex items-center justify-between gap-3 py-2 px-2.5 border border-[var(--color-line)] border-t-0 bg-[var(--color-page)]">
          <small className={ROW_LABEL}>Turn timing</small><strong className="text-[10px]">{timing}</strong>
        </div>
      )}

      {waiting ? (
        <div className="flex gap-[9px] items-center mt-[13px] py-[11px] px-3 border border-[var(--color-line-2)] text-[var(--color-amber)] bg-[var(--color-panel)] font-extrabold text-[9px] leading-none font-readout tracking-[.07em] uppercase">
          <span className="w-1.5 h-1.5 rounded-full bg-amber animate-[pulse_1.7s_infinite]" />{waiting}
        </div>
      ) : (
        <div className="mt-[17px] flex flex-col gap-2.5">
          <div className="flex items-end gap-3">
            <span className="turn-readout">
              {turn}<small> / {game.turns}</small>
            </span>
            <span className={`${ROW_LABEL} pb-[3px]`}>Turn</span>
            <strong className="ml-auto min-w-0 overflow-hidden pb-[3px] text-right text-[9px] text-ellipsis whitespace-nowrap text-[var(--color-ink-2)]">{game.outcome.summary}</strong>
          </div>
          <progress aria-label={`Turn ${turn} of ${game.turns}`} className="turn-track" max="100" value={turnProgress} />
        </div>
      )}
      <code className="mt-auto pt-[14px] text-[var(--color-line-2)] text-[8px] tracking-[.06em]">{game.game_id}</code>
    </a>
    </DisplayPaletteProvider>
  )
}

export function ArenaPicker({ route }: { route: ArenaRouteContext }) {
  const [games, setGames] = useState<GameSummary[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)
  const [manualId, setManualId] = useState('')
  const [manualError, setManualError] = useState<string | null>(null)

  useEffect(() => {
    let mounted = true
    let active: AbortController | null = null
    async function refresh() {
      active?.abort()
      const controller = new AbortController()
      active = controller
      try {
        const payload = await fetchGames(route, controller.signal)
        if (!mounted) return
        setGames(Array.isArray(payload.games) ? payload.games : [])
        setError(null)
      } catch (reason) {
        if (mounted && !controller.signal.aborted) {
          setError(reason instanceof Error ? reason.message : 'Unable to load games')
        }
      } finally {
        if (mounted) setLoading(false)
      }
    }
    void refresh()
    const timer = window.setInterval(() => void refresh(), 3000)
    return () => {
      mounted = false
      active?.abort()
      window.clearInterval(timer)
    }
  }, [route])

  const visibleGames = useMemo(() => visiblePickerGames(games), [games])
  const orderedGames = useMemo(() => sortedGames(visibleGames), [visibleGames])
  const activeCount = visibleGames.filter((game) => ['lobby', 'starting', 'running'].includes(game.state)).length

  function openManual(event: FormEvent) {
    event.preventDefault()
    const gameId = normalizeGameId(manualId)
    if (!gameId) {
      setManualError('Enter the full game ID (20–80 letters, numbers, _ or -).')
      return
    }
    window.location.assign(watchUrl(route.prefix, gameId))
  }

  return (
    <main className="w-[min(1420px,100%)] min-h-screen my-0 mx-auto p-[clamp(18px,4vw,58px)] max-[760px]:p-[10px]">
      <header className="arena-hero">
        <div className="flex items-center gap-4 min-w-0 self-center z-[1]">
          <div className="brand-mark arena-brand-mark" aria-hidden="true"><span /><span /><span /></div>
          <div className="min-w-0"><p className="eyebrow">Freeciv autonomous evaluation</p><h1>Agent Arena</h1></div>
        </div>
        <p className="z-[1] max-w-[520px] mt-0 mx-0 mb-2 text-[var(--color-ink-2)] text-[length:clamp(14px,1.6vw,18px)] leading-[1.65] max-[1100px]:max-w-[680px]">Watch model harnesses negotiate, expand, research, and compete against Freeciv’s native intelligence—turn by turn.</p>
        <div className="col-[1/-1] flex flex-wrap gap-x-6 gap-y-[9px] items-center z-[1] pt-[18px] border-t border-t-[var(--color-line)] text-[var(--color-muted)] font-bold text-[9px] leading-none font-readout tracking-[.11em] uppercase">
          <span className="inline-flex items-center gap-[7px]"><i className="w-1.5 h-1.5 rounded-full bg-[var(--color-green)] animate-[pulse_1.7s_infinite]" />{activeCount} active {activeCount === 1 ? 'match' : 'matches'}</span>
          <span className="inline-flex items-center gap-[7px]">{visibleGames.length} indexed</span>
          <span className="inline-flex items-center gap-[7px]">Auto-refresh 3s</span>
        </div>
      </header>

      <section className="grid grid-cols-[1fr_minmax(330px,550px)] gap-6 items-end mt-[34px] mx-0 mb-4 max-[760px]:grid-cols-1 max-[760px]:mt-[25px]" aria-labelledby="matches-title">
        <div><p className="eyebrow">Public spectator feed</p><h2 className="m-0 text-[30px] tracking-[-.04em]" id="matches-title">Matches</h2></div>
        <form className="game-id-form" onSubmit={openManual}>
          <label className="block mb-[7px] text-[var(--color-muted)] font-medium text-[9px] leading-none font-readout tracking-[.12em] uppercase" htmlFor="manual-game-id">Open by game ID</label>
          <div className="grid grid-cols-[1fr_auto] max-[460px]:grid-cols-1"><input id="manual-game-id" onChange={(event) => { setManualId(event.target.value); setManualError(null) }} placeholder="game_…" spellCheck="false" value={manualId} /><button type="submit">Open match</button></div>
          {manualError && <span className="block mt-[7px] text-red text-[11px]" role="alert">{manualError}</span>}
        </form>
      </section>

      {error && (
        <div className="flex gap-x-[22px] gap-y-[10px] items-baseline mb-[14px] py-[13px] px-4 border border-[var(--color-line-2)] border-l-4 border-l-amber bg-[var(--color-panel)] max-[760px]:items-start max-[760px]:flex-col" role="status">
          <strong className="text-[var(--color-amber)] text-[12px]">Live match index unavailable</strong>
          <span className="text-[var(--color-muted)] text-[11px]">{error}. You can still paste a known game ID above.</span>
        </div>
      )}

      {loading && !games.length ? (
        <section className="grid grid-cols-2 gap-[14px] max-[760px]:grid-cols-1" aria-label="Loading matches">
          {[0, 1, 2].map((index) => (
            <span
              className="min-h-[360px] border border-line bg-[linear-gradient(100deg,var(--color-panel)_20%,var(--color-panel-2)_45%,var(--color-panel)_70%)] bg-[length:300%_100%] animate-[shimmer_1.8s_infinite] last:hidden"
              key={index}
            />
          ))}
        </section>
      ) : orderedGames.length ? (
        <section className="grid grid-cols-2 gap-[14px] max-[760px]:grid-cols-1" aria-live="polite">
          {orderedGames.map((game) => <ArenaCard game={game} key={game.game_id} prefix={route.prefix} />)}
        </section>
      ) : !error && (
        <section className="flex min-h-[360px] flex-col items-center justify-center border border-dashed border-[var(--color-line)] text-muted text-center"><span className="status-glyph">◇</span><p className="eyebrow">Arena standing by</p><h2 className="m-0 text-[27px]">No games yet</h2><p className="text-[12px]">Start a game, then it will appear here automatically.</p></section>
      )}

      <footer className="flex justify-between gap-5 pt-3 px-1 pb-0 text-[var(--color-line-2)] text-[8px] leading-[1.4] font-readout tracking-[.1em] uppercase max-[760px]:flex-col"><span>FREECIV AGENT EVALUATION</span><span>Public spectator surface · agent credentials never exposed</span></footer>
    </main>
  )
}
