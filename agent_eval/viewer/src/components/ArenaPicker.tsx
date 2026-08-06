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
import { watchUrl } from '../route'
import type { ArenaRouteContext, GameSummary } from '../types'
import { matchHeaderLabel } from '../view-model'
import { ColorMark } from './ColorMark'

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

function ArenaCard({ game, prefix }: { game: GameSummary; prefix: string }) {
  const waiting = waitingLabel(game)
  const timing = timingModeLabel(game)
  const turn = game.current_turn ?? 0
  const turnProgress = Math.min(100, Math.max(0, (turn / Math.max(game.turns, 1)) * 100))

  return (
    <a className="arena-card" href={watchUrl(prefix, game.game_id)}>
      <div className="arena-card-topline">
        <span className={`state-pill state-${game.state}`}><i />{stateLabel(game.state)}</span>
        <span className={`arena-validity validity-dot-${String(game.benchmark_valid)}`}>
          {validityLabel(game.benchmark_valid)}
        </span>
      </div>
      <div className="arena-card-title">
        <div>
          <p className="eyebrow">{matchModeLabel(game)}</p>
          <h2>{matchHeaderLabel(game.resolved_places)}</h2>
        </div>
        <span aria-hidden="true">↗</span>
      </div>

      <div className="arena-seat-strip" aria-label="Controllers and seats">
        {game.resolved_places.map((place) => (
          <div className={place.joined || place.controller === 'native_classic_ai' ? '' : 'seat-open'} key={place.seat_id}>
            <ColorMark color={place.player_color} label={placeLabel(place)} size="sm" />
            <span><strong>{placeLabel(place)}</strong><small>{place.model || place.player_name}</small></span>
          </div>
        ))}
      </div>

      <div className="arena-card-meta">
        <div><small>Places</small><strong>{game.places}</strong></div>
        <div><small>Agents</small><strong>{game.joined_agents}/{game.max_agents}</strong></div>
        <div><small>Open seats</small><strong>{openAgentSeats(game)}</strong></div>
        <div><small>{game.state === 'running' ? 'Leader' : 'Outcome'}</small><strong>{gamePrimaryResult(game)}</strong></div>
      </div>

      {timing && <div className="arena-timing-row"><small>Turn timing</small><strong>{timing}</strong></div>}

      {waiting ? <div className="waiting-banner"><span />{waiting}</div> : (
        <div className="turn-progress">
          <span><b>TURN {turn}</b><small> / {game.turns}</small></span>
          <progress aria-label={`Turn ${turn} of ${game.turns}`} className="turn-track" max="100" value={turnProgress} />
          <strong>{game.outcome.summary}</strong>
        </div>
      )}
      <code>{game.game_id}</code>
    </a>
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
    <main className="arena-shell">
      <header className="arena-hero">
        <div className="brand-block">
          <div className="brand-mark arena-brand-mark" aria-hidden="true"><span /><span /><span /></div>
          <div><p className="eyebrow">Freeciv autonomous evaluation</p><h1>Agent Arena</h1></div>
        </div>
        <p>Watch model harnesses negotiate, expand, research, and compete against Freeciv’s native intelligence—turn by turn.</p>
        <div className="arena-pulse-row">
          <span><i />{activeCount} active {activeCount === 1 ? 'match' : 'matches'}</span>
          <span>{visibleGames.length} indexed</span>
          <span>Auto-refresh 3s</span>
        </div>
      </header>

      <section className="arena-toolbar" aria-labelledby="matches-title">
        <div><p className="eyebrow">Public spectator feed</p><h2 id="matches-title">Matches</h2></div>
        <form className="game-id-form" onSubmit={openManual}>
          <label htmlFor="manual-game-id">Open by game ID</label>
          <div><input id="manual-game-id" onChange={(event) => { setManualId(event.target.value); setManualError(null) }} placeholder="game_…" spellCheck="false" value={manualId} /><button type="submit">Open match</button></div>
          {manualError && <span role="alert">{manualError}</span>}
        </form>
      </section>

      {error && (
        <div className="index-warning" role="status">
          <strong>Live match index unavailable</strong>
          <span>{error}. You can still paste a known game ID above.</span>
        </div>
      )}

      {loading && !games.length ? (
        <section className="arena-loading" aria-label="Loading matches">
          {[0, 1, 2].map((index) => <span key={index} />)}
        </section>
      ) : orderedGames.length ? (
        <section className="arena-grid" aria-live="polite">
          {orderedGames.map((game) => <ArenaCard game={game} key={game.game_id} prefix={route.prefix} />)}
        </section>
      ) : !error && (
        <section className="arena-empty"><span className="status-glyph">◇</span><p className="eyebrow">Arena standing by</p><h2>No games yet</h2><p>Start a game, then it will appear here automatically.</p></section>
      )}

      <footer><span>FREECIV AGENT EVALUATION</span><span>Public spectator surface · agent credentials never exposed</span></footer>
    </main>
  )
}
