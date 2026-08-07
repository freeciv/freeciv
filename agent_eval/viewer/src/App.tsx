import { useEffect, useMemo, useRef, useState } from 'react'
import { fetchEvents, fetchWatch, fetchWatchWithOptionalReplay } from './api'
import { ArenaPicker } from './components/ArenaPicker'
import { ColorMark } from './components/ColorMark'
import { EventLog, EventLogFootnote } from './components/EventLog'
import { MapSection } from './components/MapSection'
import { MetricChart } from './components/MetricChart'
import { ThemeToggle } from './components/ThemeToggle'
import { StrategicMap } from './components/StrategicMap'
import { TechnologyPanel } from './components/TechnologyPanel'
import { TechnologyProgressChart } from './components/TechnologyProgressChart'
import { buildDisplayPalette, displayPlayerColor, type DisplayFaction } from './display-color'
import { DisplayPaletteProvider } from './display-palette'
import { actorColors } from './event-log'
import { MAP_HASH, mapSectionOpen, rememberMapSection } from './map-preference'
import { controlProtocolLabel, placeLabel, timingModeLabel } from './picker-model'
import { frameImageUrl, resolveViewerRoute } from './route'
import type {
  GameEventsResponse,
  GamePlace,
  ReplayPlayer,
  ReplaySnapshot,
  RouteContext,
  TechnologyCatalog,
  WatchResponse,
} from './types'
import {
  METRICS,
  type MapFaction,
  competitorLabel,
  configuredPlaceFactions,
  frameAtOrBefore,
  isScoredPlayer,
  mapFactions,
  matchDurationLabel,
  matchHeaderLabel,
  maxKnownTechnologyDepth,
  playerMetric,
  scoreDisplayAtTurn,
  snapshotAtOrBefore,
  turnsAvailable,
} from './view-model'

const CENTER_STAGE = 'flex flex-col items-center justify-center min-h-screen p-6 text-center'
const CENTER_TITLE = 'my-0.5 mx-0 text-[length:clamp(25px,5vw,48px)]'
const CLAMPED_LINE = 'block overflow-hidden text-ellipsis whitespace-nowrap'
const WARNING_BOX = 'py-2.5 px-[13px] border border-[var(--color-line-2)] text-[var(--color-amber)] bg-[var(--color-panel-2)] text-[12px]'
const STAT_ROW = 'grid grid-cols-[auto_minmax(0,1fr)_auto] gap-1.5 items-center mt-[7px]'
const FACTION_ROW = 'grid grid-cols-[auto_1fr_auto] gap-2.5 items-center py-[11px] px-[9px] border-b border-b-[var(--color-line)] last:border-b-0'
const CLAMPED_LINE_RAIL = 'block min-w-0 overflow-hidden text-ellipsis whitespace-nowrap'

function mergeSnapshots(current: ReplaySnapshot[], incoming: ReplaySnapshot[]) {
  const merged = new Map(current.map((snapshot) => [snapshot.turn, snapshot]))
  for (const snapshot of incoming) merged.set(snapshot.turn, snapshot)
  return [...merged.values()].sort((a, b) => a.turn - b.turn)
}

function stateLabel(state: string) {
  if (state === 'running' || state === 'starting') return 'LIVE MATCH'
  if (state === 'lobby') return 'AWAITING PLAYERS'
  return state.toUpperCase()
}

function validityLabel(validity: boolean | null) {
  if (validity === true) return 'VALID EVALUATION'
  if (validity === false) return 'INVALID EVALUATION'
  return 'EVALUATION IN PROGRESS'
}

function playerForPlace(players: ReplayPlayer[], place: number) {
  return players.find((player) => player.place === place)
}

/**
 * The faction set the color plan is decided over: every faction the map knows,
 * plus the configured places, which carry the seat colors before any replay
 * telemetry has arrived. Places reuse the `place - 1` identity that
 * `configuredPlaceFactions` assigns. A color already claimed by a map faction
 * contributes nothing the second time, so listing both sources cannot disturb
 * the plan — it only widens it.
 */
function paletteFactions(
  factions: MapFaction[],
  places: GamePlace[],
): DisplayFaction[] {
  return [
    ...factions.map((faction) => ({
      playerId: faction.player_id,
      color: faction.player_color,
    })),
    ...places.map((place) => ({
      playerId: place.place - 1,
      color: place.player_color,
    })),
  ]
}

function acquisitionHistory(
  snapshots: ReplaySnapshot[],
  seatId: string,
  catalog: TechnologyCatalog,
) {
  const names = new Map(catalog.technologies.map((technology) => [technology.id, technology.name]))
  return snapshots.flatMap((snapshot) => {
    const player = snapshot.players.find((candidate) => candidate.seat_id === seatId)
    return (player?.gained_tech_ids ?? []).map((id) => ({
      turn: snapshot.turn,
      name: names.get(id) ?? `Technology ${id}`,
    }))
  })
}

function MatchViewer({ route }: { route: RouteContext }) {
  const [watch, setWatch] = useState<WatchResponse | null>(null)
  const [snapshots, setSnapshots] = useState<ReplaySnapshot[]>([])
  const [catalog, setCatalog] = useState<TechnologyCatalog>({ technologies: [] })
  const [warnings, setWarnings] = useState<{ turn?: number | null; message: string }[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)
  const [basicTelemetry, setBasicTelemetry] = useState(false)
  const [selectedTurn, setSelectedTurn] = useState(0)
  const [live, setLive] = useState(true)
  const [playing, setPlaying] = useState(false)
  const [speed, setSpeed] = useState(1)
  const [selectedSeat, setSelectedSeat] = useState('')
  const [mapOpen, setMapOpen] = useState(() => mapSectionOpen(window.location.hash))
  const [eventLog, setEventLog] = useState<GameEventsResponse | null>(null)
  const nextReplayProbeAt = useRef(0)

  const latestReplayTurn = snapshots.at(-1)?.turn ?? 0

  useEffect(() => {
    const controller = new AbortController()
    async function initialLoad() {
      setLoading(true)
      try {
        const load = await fetchWatchWithOptionalReplay(route, 0, controller.signal)
        setWatch(load.watch)
        if (load.replay) {
          setSnapshots(load.replay.snapshots.sort((a, b) => a.turn - b.turn))
          if (load.replay.catalog) setCatalog(load.replay.catalog)
          setWarnings(load.replay.warnings)
        }
        setBasicTelemetry(load.replayUnavailable)
        if (load.replayUnavailable) nextReplayProbeAt.current = Date.now() + 60_000
        setError(load.replayError && !load.replayUnavailable
          ? `Replay telemetry unavailable: ${load.replayError}`
          : null)
      } catch (reason) {
        if (!controller.signal.aborted) {
          setError(reason instanceof Error ? reason.message : 'Unable to load replay')
        }
      } finally {
        if (!controller.signal.aborted) setLoading(false)
      }
    }
    void initialLoad()
    return () => controller.abort()
  }, [route])

  useEffect(() => {
    if (loading) return
    const controller = new AbortController()
    const timer = window.setInterval(async () => {
      try {
        if (basicTelemetry && Date.now() < nextReplayProbeAt.current) {
          setWatch(await fetchWatch(route, controller.signal))
          setError(null)
          return
        }
        const load = await fetchWatchWithOptionalReplay(
          route, latestReplayTurn, controller.signal,
        )
        setWatch(load.watch)
        if (load.replay) {
          setSnapshots((current) => mergeSnapshots(current, load.replay!.snapshots))
          if (load.replay.catalog) setCatalog(load.replay.catalog)
          setWarnings(load.replay.warnings)
        }
        setBasicTelemetry(load.replayUnavailable)
        nextReplayProbeAt.current = load.replayUnavailable
          ? Date.now() + 60_000
          : 0
        setError(load.replayError && !load.replayUnavailable
          ? `Replay telemetry unavailable: ${load.replayError}`
          : null)
      } catch (reason) {
        if (!controller.signal.aborted) {
          setError(reason instanceof Error ? reason.message : 'Live refresh failed')
        }
      }
    }, 2500)
    return () => {
      controller.abort()
      window.clearInterval(timer)
    }
  }, [basicTelemetry, latestReplayTurn, loading, route])

  const availableTurns = useMemo(
    () => turnsAvailable(snapshots, watch?.frames ?? []),
    [snapshots, watch?.frames],
  )
  const lastTurn = availableTurns.at(-1) ?? watch?.game.current_turn ?? 0

  useEffect(() => {
    if (live && lastTurn > 0) setSelectedTurn(lastTurn)
  }, [lastTurn, live])

  useEffect(() => {
    if (!playing || availableTurns.length < 2) return
    const timer = window.setInterval(() => {
      setSelectedTurn((current) => {
        const next = availableTurns.find((turn) => turn > current)
        if (next !== undefined) {
          setLive(next === lastTurn)
          return next
        }
        setPlaying(false)
        return current
      })
    }, 1000 / speed)
    return () => window.clearInterval(timer)
  }, [availableTurns, lastTurn, playing, speed])

  useEffect(() => {
    const controller = new AbortController()
    fetchEvents(route, controller.signal)
      .then(setEventLog)
      // A gateway or supervisor without the derived event log simply leaves
      // the resolution panel on its turn timeline.
      .catch(() => undefined)
    return () => controller.abort()
  }, [lastTurn, route])

  const eventColors = useMemo(
    () => actorColors(
      watch?.game.resolved_places ?? [],
      snapshots.flatMap((snapshot) => snapshot.players),
    ),
    [snapshots, watch?.game.resolved_places],
  )

  const selectedSnapshot = snapshotAtOrBefore(snapshots, selectedTurn)
  const selectedFrame = frameAtOrBefore(watch?.frames ?? [], selectedTurn)
  const exactSelectedFrame = selectedFrame?.turn === selectedTurn ? selectedFrame : undefined
  const selectedTurnIndex = Math.max(0, availableTurns.indexOf(selectedTurn))
  const scoredPlayers = (selectedSnapshot?.players ?? []).filter(isScoredPlayer)
  const selectedTechPlayer = scoredPlayers.find((player) => player.seat_id === selectedSeat)
    ?? scoredPlayers[0]

  useEffect(() => {
    if (!selectedSeat && scoredPlayers[0]) setSelectedSeat(scoredPlayers[0].seat_id)
  }, [scoredPlayers, selectedSeat])

  if (loading) {
    return (
      <main className={CENTER_STAGE}>
        <div className="loading-orbit" aria-hidden="true"><span /></div>
        <p className="eyebrow">Synchronizing spectator telemetry</p>
        <h1 className={CENTER_TITLE}>Loading Freeciv agent arena</h1>
      </main>
    )
  }

  if (!watch) {
    return (
      <main className={CENTER_STAGE}>
        <span className="status-glyph">!</span>
        <p className="eyebrow">Replay unavailable</p>
        <h1 className={CENTER_TITLE}>Could not open this match</h1>
        <p className="max-w-[580px] text-muted">{error ?? 'No watch data was returned by the supervisor.'}</p>
      </main>
    )
  }

  const game = watch.game
  const mappedFactions = mapFactions(
    selectedFrame, selectedSnapshot, game.resolved_places,
  )
  const factions = basicTelemetry && !mappedFactions.length
    ? configuredPlaceFactions(game.resolved_places)
    : mappedFactions
  // One plan for the whole match, decided over every faction at once, and
  // handed to the board, the charts, and every swatch below.
  const paletteInput = paletteFactions(factions, game.resolved_places)
  const palette = buildDisplayPalette(paletteInput)
  const selectedYear = selectedSnapshot?.year
  const validityClass = game.benchmark_valid === true
    ? 'validity-valid'
    : game.benchmark_valid === false
      ? 'validity-invalid'
      : 'validity-pending'
  const historicalComparison = selectedTurn > 0 && lastTurn > 0 && selectedTurn < lastTurn
  const timing = timingModeLabel(game)
  const duration = matchDurationLabel(game)
  const protocol = controlProtocolLabel(game.control_protocol)
  const comparisonRows = game.resolved_places.map((place) => {
    const telemetry = playerForPlace(selectedSnapshot?.players ?? [], place.place)
    const authoritative = game.leaderboard.find((entry) => entry.place === place.place)
    return {
      place,
      score: historicalComparison ? telemetry?.score : authoritative?.score ?? telemetry?.score,
    }
  }).sort((a, b) => (b.score ?? Number.NEGATIVE_INFINITY) - (a.score ?? Number.NEGATIVE_INFINITY))

  function chooseTurn(turn: number) {
    setSelectedTurn(turn)
    setLive(turn === lastTurn)
  }

  const previousTurn = [...availableTurns].reverse().find((turn) => turn < selectedTurn)
  const nextTurn = availableTurns.find((turn) => turn > selectedTurn)

  function stepReplay(turn: number | undefined) {
    if (turn === undefined) return
    setPlaying(false)
    chooseTurn(turn)
  }

  function togglePlayback() {
    if (playing) {
      setPlaying(false)
      return
    }
    if (availableTurns.length < 2) return
    const firstTurn = availableTurns[0]
    if (selectedTurn >= lastTurn && firstTurn !== undefined) {
      setSelectedTurn(firstTurn)
      setLive(false)
    }
    setPlaying(true)
  }

  function toggleMap(next: boolean) {
    setMapOpen(next)
    rememberMapSection(next)
    const { pathname, search, hash } = window.location
    const wanted = next ? MAP_HASH : ''
    if (hash !== wanted) {
      window.history.replaceState(null, '', `${pathname}${search}${wanted}`)
    }
  }

  return (
    <DisplayPaletteProvider factions={paletteInput}>
    <main className="app-shell">
      <nav className="grid grid-cols-[minmax(220px,1fr)_auto_minmax(220px,1fr)] gap-[18px] items-center min-h-[45px] py-0 px-[14px] border border-line border-b-0 text-[var(--color-muted)] bg-[var(--color-page)] font-bold text-[9px] leading-none font-readout tracking-[.1em] uppercase max-[1000px]:grid-cols-[1fr_auto] max-[650px]:grid-cols-1" aria-label="Freeciv Agent Arena">
        <a className="inline-flex gap-[9px] items-center text-ink no-underline max-[460px]:min-w-0" href={route.prefix ? `${route.prefix}/` : '/'}><span className="grid place-items-center w-[25px] h-[25px] border border-[var(--color-line-2)] text-[var(--color-ink)] text-[8px]" aria-hidden="true">FC</span><strong className="max-[460px]:overflow-hidden max-[460px]:text-ellipsis max-[460px]:whitespace-nowrap">Freeciv Agent Arena</strong></a>
        <span className="text-center max-[1000px]:hidden">{game.mode === 'single' ? 'Single player evaluation' : 'Multiplayer evaluation'}</span>
        <span className="flex gap-3 items-center justify-end min-w-0"><code className="overflow-hidden text-[var(--color-muted)] text-[8px] text-right text-ellipsis whitespace-nowrap max-[650px]:hidden">{game.game_id}</code><ThemeToggle /></span>
      </nav>
      <header className="relative grid grid-cols-[minmax(0,1fr)_auto] gap-x-7 gap-y-[18px] items-center py-[22px] px-6 overflow-hidden border border-line bg-[linear-gradient(110deg,var(--color-panel-2),var(--color-panel))] max-[760px]:grid-cols-1 max-[760px]:p-[17px] max-[650px]:p-4">
        <div className="brand-block">
          <div className="brand-mark" aria-hidden="true"><span /><span /><span /></div>
          <div>
            <p className="eyebrow">Freeciv autonomous arena</p>
            <h1 className="m-0 overflow-hidden text-[length:clamp(25px,3vw,41px)] font-medium tracking-[-.025em] text-ellipsis whitespace-nowrap [overflow-wrap:anywhere] max-[760px]:whitespace-normal max-[760px]:text-[length:clamp(22px,8vw,34px)] max-[760px]:leading-[1.05]">{matchHeaderLabel(game.resolved_places)}</h1>
          </div>
        </div>
        <div className="flex items-center gap-[14px] z-[1] max-[760px]:justify-between max-[460px]:items-start max-[460px]:flex-col">
          <span className={`state-pill state-${game.state}`}><i />{stateLabel(game.state)}</span>
          <span className="flex items-baseline gap-[7px] text-[var(--color-ink)] font-bold text-[30px] leading-none font-readout"><small className="text-muted text-[9px] not-italic tracking-[.14em]">TURN</small>{selectedTurn || '—'}<em className="text-muted text-[9px] not-italic tracking-[.14em]">{selectedYear == null ? '' : ` / ${selectedYear}`}</em></span>
        </div>
      </header>

      {error && <div className={`mt-2.5 ${WARNING_BOX}`} role="status">Live refresh issue: {error}. Showing the latest retained data.</div>}

      <div className="match-content" id="match-content">

      <div className="match-main">

      <section className="result-ribbon" aria-label="Match outcome and validity">
        <div>
          <p className="eyebrow">Current result</p>
          <strong>{game.outcome.summary}</strong>
          {game.outcome.victory && (
            <span className="inline-block mb-[7px] py-1 px-[9px] border border-[var(--color-line-2)] bg-[var(--color-panel-2)] text-[var(--color-green)] font-medium text-[11px] leading-[1.35] font-readout tracking-[.04em]" title={`Victory condition: ${game.outcome.victory.code}`}>
              Game ended: {game.outcome.victory.label}
              {game.outcome.victory.turn ? ` on turn ${game.outcome.victory.turn}` : ''}
              {game.outcome.victory.winners.length > 0
                ? ` · ${game.outcome.victory.winners.join(', ')}`
                : ''}
            </span>
          )}
          <span>{game.objective}</span>
        </div>
        <div className={`flex flex-col justify-center border-l-4 ${validityClass}`}>
          {/* No font-size here on purpose: `.result-ribbon > div > span` outranks the
              old `.validity-chip span` rule, so this has always rendered at 12px. */}
          <span className="font-extrabold leading-none font-readout tracking-[.08em]">{validityLabel(game.benchmark_valid)}</span>
          <small className="mt-2 text-[var(--color-ink-2)] leading-[1.4]">{game.benchmark_valid === false
            ? game.invalid_reasons.join(' · ') || game.error || 'Run is not benchmark eligible'
            : game.benchmark_valid === true
              ? 'Eligible for model comparison'
              : 'Final validity is decided when the match ends'}</small>
        </div>
      </section>

      <section className="match-context" aria-label="Match configuration and seat status">
        <div><p className="eyebrow">Mode</p><strong>{game.mode === 'single' ? 'Single player vs native AI' : 'Multiplayer agent match'}</strong><span>{game.places} total places · {game.max_agents} external agent {game.max_agents === 1 ? 'seat' : 'seats'}</span></div>
        <div><p className="eyebrow">Control protocol</p><strong>{protocol.label}</strong><span>{protocol.detail}{protocol.assumed ? ' · assumed for this archived run' : ''}</span></div>
        <div><p className="eyebrow">Agent lobby</p><strong>{game.joined_agents}/{game.max_agents} joined</strong><span>{game.state === 'lobby' && game.max_agents > game.joined_agents ? `Waiting for ${game.max_agents - game.joined_agents} agent${game.max_agents - game.joined_agents === 1 ? '' : 's'}` : game.state === 'lobby' ? 'All agents joined · preparing match' : 'Roster locked for this match'}</span></div>
        <div><p className="eyebrow">Turn horizon</p><strong>{game.current_turn ?? 0} / {game.turns}</strong><span>{game.state === 'running' ? 'Authoritative turn in progress' : stateLabel(game.state)}</span></div>
        {duration && <div><p className="eyebrow">Duration</p><strong>{duration}</strong><span>{typeof game.finished_at === 'number' ? 'Start to last recorded turn' : 'Running and counting'}</span></div>}
        {timing && <div><p className="eyebrow">Turn timing</p><strong>{timing}</strong><span>Harness action deadline</span></div>}
      </section>

      <section className="competitor-grid" aria-label="Scored competitors">
        {game.resolved_places.map((place) => {
          const telemetry = playerForPlace(selectedSnapshot?.players ?? [], place.place)
          const authoritativeScore = game.leaderboard.find(
            (entry) => entry.place === place.place,
          )?.score
          const displayedScore = scoreDisplayAtTurn(
            game.state, authoritativeScore, telemetry?.score, selectedTurn, lastTurn,
          )
          const label = placeLabel(place)
          return (
            <article className="relative grid grid-cols-[auto_minmax(0,1fr)_auto] gap-3 items-center min-h-[72px] py-[15px] px-[17px] overflow-hidden border-0 bg-[var(--color-panel)] max-[460px]:grid-cols-[auto_1fr]" key={place.seat_id}>
              <ColorMark color={place.player_color} label={label} size="lg" />
              <div className="min-w-0">
                <strong className={`${CLAMPED_LINE} text-[15px]`}>{label}</strong>
                <span className={`${CLAMPED_LINE} mt-1 text-muted text-[10px] leading-[1.2] font-readout`}>{place.player_name}{telemetry?.nation ? ` · ${telemetry.nation}` : ''}</span>
              </div>
              <div className="text-right max-[460px]:col-[2] max-[460px]:text-left">
                <small className="block text-muted text-[8px] leading-none font-readout tracking-[.14em]">{displayedScore.label}</small>
                <strong className="text-[var(--color-ink)] font-bold text-[25px] leading-[1.1] font-readout">{displayedScore.value?.toLocaleString() ?? '—'}</strong>
              </div>
              <span className="absolute right-[9px] bottom-[5px] text-[var(--color-line-2)] text-[8px] leading-none font-readout">{displayPlayerColor(place.player_color, palette)}</span>
            </article>
          )
        })}
      </section>

      {!basicTelemetry && scoredPlayers.length > 0 && (
        <section className="intelligence-grid" aria-label="Controller empire and research intelligence">
          {scoredPlayers.map((player) => {
            const visibleSnapshots = snapshots.filter((snapshot) => snapshot.turn <= selectedTurn)
            const acquisitions = acquisitionHistory(visibleSnapshots, player.seat_id, catalog)
            const researchPercent = player.research.cost > 0
              ? Math.min(100, Math.round((player.research.bulbs / player.research.cost) * 100))
              : 0
            return (
              <article className="intelligence-card" key={player.seat_id}>
                <svg aria-hidden="true" className="absolute top-0 right-0 bottom-auto left-0 w-full h-[3px]" preserveAspectRatio="none" viewBox="0 0 100 2"><line stroke={displayPlayerColor(player.player_color, palette) ?? '#82919d'} strokeWidth="2" x1="0" x2="100" y1="1" y2="1" /></svg>
                <div className="flex gap-3 items-center"><ColorMark color={player.player_color} label={competitorLabel(player)} size="lg" /><div><p className="eyebrow">Scored controller</p><h2 className="m-0 text-[19px] tracking-[-.03em]">{competitorLabel(player)}</h2><span className="block mt-[3px] text-muted text-[10px]">{player.nation || player.player_name} · {player.government || 'Government unknown'}</span></div></div>
                <div className="empire-metrics">
                  {METRICS.slice(1).map((metric) => <span key={metric.key}><small>{metric.label}</small><strong>{playerMetric(player, metric.key).toLocaleString()}</strong></span>)}
                </div>
                <div className="research-focus">
                  <div><small>Current research</small><strong>{player.research.name || 'No active target'}</strong><span>{player.research.bulbs.toLocaleString()} / {player.research.cost.toLocaleString()} bulbs</span></div>
                  <progress aria-label={`${competitorLabel(player)} research progress`} className="research-meter" max="100" value={researchPercent} />
                  <b>{researchPercent}%</b>
                </div>
                <div className="knowledge-facts">
                  <span><strong>{player.known_tech_ids.length}</strong><small>known technologies</small></span>
                  <span><strong>{maxKnownTechnologyDepth(player, catalog.technologies) ?? '—'}</strong><small>deepest dependency tier</small></span>
                  <span><strong>{acquisitions.length}</strong><small>verified acquisitions</small></span>
                </div>
                <div className="mt-[14px] pt-3 border-t border-t-[var(--color-line)]">
                  <small className="block mb-[7px] text-[var(--color-muted)] font-bold text-[7px] leading-none font-readout tracking-[.1em] uppercase">Acquisition history</small>
                  <div className="flex flex-wrap gap-[5px]">{acquisitions.length ? acquisitions.slice(-4).reverse().map((acquisition, index) => <span className="py-[5px] px-[7px] border border-line text-muted bg-[var(--color-page)] text-[8px]" key={`${acquisition.turn}-${acquisition.name}-${index}`}><b className="mr-[5px] text-[var(--color-muted)] font-medium text-[7px] leading-none font-readout">T{acquisition.turn}</b>{acquisition.name}</span>) : <em className="text-[var(--color-muted)] text-[9px] not-italic">No new technology recorded yet</em>}</div>
                </div>
              </article>
            )
          })}
        </section>
      )}

      {basicTelemetry && (
        <section className="legacy-telemetry-notice" aria-label="Older supervisor telemetry compatibility">
          <span aria-hidden="true">i</span>
          <div>
            <p className="eyebrow">Basic telemetry mode</p>
            <h2>Detailed telemetry was not recorded for this match</h2>
            <p>This older supervisor did not capture city, economy, comparison-chart, or technology data. Current authoritative scores, the strategic map, playback, controller colors, and the live turn timeline remain available.</p>
          </div>
        </section>
      )}

      {!basicTelemetry && <><section className="turn-stats" aria-label="Selected turn statistics">
        {METRICS.map((metric) => (
          <article className="min-w-[150px] py-[13px] px-[14px] border border-line bg-[var(--color-panel)]" key={metric.key}>
            <p className="mt-0 mx-0 mb-2.5 text-[var(--color-ink-2)] font-extrabold text-[9px] leading-none font-readout tracking-[.12em] uppercase">{metric.label}</p>
            {scoredPlayers.length ? scoredPlayers.map((player) => (
              <div className={STAT_ROW} key={player.seat_id}>
                <ColorMark color={player.player_color} label={competitorLabel(player)} size="sm" />
                <span className="overflow-hidden text-muted text-[9px] text-ellipsis whitespace-nowrap">{competitorLabel(player)}</span>
                <strong className="font-bold text-[16px] leading-none font-readout">{playerMetric(player, metric.key).toLocaleString()}</strong>
              </div>
            )) : <span className="empty-copy">No telemetry</span>}
          </article>
        ))}
      </section>

      <TechnologyProgressChart
        catalog={catalog.technologies.length ? catalog.technologies : null}
        selectedTurn={selectedTurn}
        snapshots={snapshots}
      />

      <TechnologyPanel
        catalog={catalog.technologies}
        player={selectedTechPlayer}
        scoredPlayers={scoredPlayers}
        selectedSeat={selectedTechPlayer?.seat_id ?? selectedSeat}
        setSelectedSeat={setSelectedSeat}
        snapshots={snapshots.filter((snapshot) => snapshot.turn <= selectedTurn)}
      />
      </>}

      </div>

      <div className="match-rail">
        <aside className="overview-rail-card panel overflow-hidden max-[650px]:overflow-x-auto" aria-label="Current score comparison">
          <div className="panel-heading compact-heading"><div><p className="eyebrow">{historicalComparison ? 'Selected-turn comparison' : 'Current comparison'}</p><h2>Leaderboard</h2></div><span>{historicalComparison ? `T${selectedTurn}` : 'LATEST'}</span></div>
          <div className="grid">
            {comparisonRows.some((row) => row.score !== undefined) ? comparisonRows.map((row, index) => (
              <div className="grid grid-cols-[18px_auto_minmax(0,1fr)_auto] gap-2 items-center py-2.5 px-3 border-b border-b-[var(--color-line)] last:border-b-0" key={row.place.seat_id}><b className="text-[var(--color-muted)] font-bold text-[10px] leading-none font-readout">{index + 1}</b><ColorMark color={row.place.player_color} label={placeLabel(row.place)} size="sm" /><span className={CLAMPED_LINE_RAIL}><strong className={`${CLAMPED_LINE_RAIL} text-[10px]`}>{placeLabel(row.place)}</strong><small className={`${CLAMPED_LINE_RAIL} mt-[3px] text-[var(--color-muted)] text-[8px]`}>{row.place.model || row.place.player_name}</small></span><em className="text-[var(--color-ink-2)] font-bold text-[14px] leading-none font-readout not-italic">{row.score?.toLocaleString() ?? '—'}</em></div>
            )) : <p className="empty-copy">Scores appear after the first resolved turn.</p>}
          </div>
        </aside>

        {!basicTelemetry && (
          <section className="charts-grid" aria-label="Metric comparison charts">
            {METRICS.map((metric) => (
              <MetricChart key={metric.key} label={metric.label} metric={metric.key} snapshots={snapshots.filter((snapshot) => snapshot.turn <= selectedTurn)} />
            ))}
          </section>
        )}
      </div>

      <MapSection
        expanded={mapOpen}
        live={live}
        onToggle={toggleMap}
        turn={selectedTurn}
        board={selectedTurn > 0 ? (
          <StrategicMap
            alt={`Strategic world map for turn ${selectedTurn}`}
            availableTurns={availableTurns}
            rawSourceName={exactSelectedFrame?.source_name}
            rawSrc={exactSelectedFrame ? frameImageUrl(route, exactSelectedFrame) : undefined}
            route={route}
            turn={selectedTurn}
          />
        ) : (
          <div className="map-stage"><div className="empty-state"><strong>No map frame yet</strong><span>The first map appears after Freeciv completes a capture turn.</span></div></div>
        )}
        legend={(
          <aside className="panel faction-panel">
            <div className="panel-heading compact-heading">
              <div><p className="eyebrow">Map color key</p><h2>All map factions</h2></div>
              <span>{factions.length}</span>
            </div>
            <p className="m-0 py-[13px] px-4 border-b border-b-[var(--color-line)] text-muted text-[11px] leading-[1.5]">{basicTelemetry
              ? 'Scored controller colors come from the match roster. Dynamic faction identities were not recorded by this older supervisor.'
              : 'Scored controllers and Freeciv-created factions are listed separately. Dynamic factions never enter the benchmark leaderboard.'}</p>
            <div className="p-2 max-[1100px]:grid max-[1100px]:grid-cols-2 max-[760px]:grid-cols-1">
              {factions.length ? factions.map((faction) => (
                <article className={faction.dynamic ? `${FACTION_ROW} dynamic-faction` : FACTION_ROW} key={`${faction.player_id}-${faction.player_name}`}>
                  <ColorMark color={faction.player_color} label={faction.display_label} />
                  <div className="min-w-0"><strong className="block [overflow-wrap:anywhere] text-[12px] leading-[1.3]">{faction.display_label}</strong><span className="block mt-[3px] overflow-hidden text-muted text-[10px] text-ellipsis whitespace-nowrap">{faction.detail}</span></div>
                  <code className="text-[var(--color-muted)] text-[9px]">{displayPlayerColor(faction.player_color, palette)}</code>
                </article>
              )) : <div className="empty-state small-empty"><strong>Legend pending</strong><span>Waiting for map header metadata.</span></div>}
            </div>
          </aside>
        )}
        playback={(
          <div className="playback-bar">
            <div className="flex items-center gap-[5px]" aria-label="Replay transport controls">
              <button aria-label="Previous turn" className="step-button" disabled={previousTurn === undefined} onClick={() => stepReplay(previousTurn)} type="button">‹</button>
              <button aria-label={playing ? 'Pause replay' : 'Play replay'} className="play-button" disabled={availableTurns.length < 2} onClick={togglePlayback} type="button">
                {playing ? 'Ⅱ' : '▶'}
              </button>
              <button aria-label="Next turn" className="step-button" disabled={nextTurn === undefined} onClick={() => stepReplay(nextTurn)} type="button">›</button>
            </div>
            <label className="grid grid-cols-[auto_1fr] gap-2.5 items-center text-muted font-bold text-[9px] leading-none font-readout tracking-[.04em] max-[460px]:grid-cols-1 max-[460px]:gap-[5px]">
              <span>Turn {selectedTurn || '—'}</span>
              <input
                aria-label="Replay turn"
                disabled={!availableTurns.length}
                max={Math.max(0, availableTurns.length - 1)}
                min={0}
                onChange={(event) => chooseTurn(availableTurns[Number(event.target.value)] ?? selectedTurn)}
                className="w-full accent-[var(--color-ink-2)]"
                type="range"
                value={selectedTurnIndex}
              />
            </label>
            <label className="text-muted font-bold text-[9px] leading-none font-readout tracking-[.06em] uppercase max-[760px]:hidden">Speed
              <select className="ml-[7px] py-[5px] px-1.5 border border-line text-ink bg-[var(--color-page)]" aria-label="Playback speed" onChange={(event) => setSpeed(Number(event.target.value))} value={speed}>
                <option value={0.5}>0.5×</option><option value={1}>1×</option><option value={2}>2×</option><option value={4}>4×</option>
              </select>
            </label>
            <button className="live-button" onClick={() => { setLive(true); setSelectedTurn(lastTurn) }} type="button">Latest</button>
          </div>
        )}
      />

      <section className="panel event-panel">
        <div className="panel-heading compact-heading">
          <div><p className="eyebrow">Resolution log</p><h2>Turn events</h2></div>
          <span>{eventLog?.events.length
            ? `${eventLog.total_events} events`
            : `${watch.timeline.length} resolved`}</span>
        </div>
        {eventLog?.events.length ? (
          <>
            <EventLog
              colors={eventColors}
              events={eventLog.events}
              onSelectTurn={(turn) => { setPlaying(false); chooseTurn(turn) }}
              selectedTurn={selectedTurn}
            />
            <EventLogFootnote omitted={eventLog.truncated ? eventLog.omitted_counts : {}} />
          </>
        ) : (
          <div className="event-stream">
            {watch.timeline.slice(-12).reverse().map((event) => (
              <article key={event.turn}>
                <b>T{event.turn}</b>
                <span>{event.year ?? '—'}</span>
                <strong>{event.timed_out_seats?.length ? `Timeout: ${event.timed_out_seats.join(', ')}` : 'All submitted'}</strong>
              </article>
            ))}
            {!watch.timeline.length && <p className="empty-copy">No resolved turns yet.</p>}
          </div>
        )}
        {warnings.length > 0 && (
          <div className={`m-3 ${WARNING_BOX}`} role="status">
            <strong className="block">Replay telemetry warnings</strong>
            <span className="block mt-1 text-[10px]">{warnings.map((warning) => warning.turn ? `Turn ${warning.turn}: ${warning.message}` : warning.message).join(' · ')}</span>
          </div>
        )}
      </section>
      </div>

      <footer className="flex justify-between gap-5 pt-3 px-1 pb-0 text-[var(--color-line-2)] text-[8px] leading-[1.4] font-readout tracking-[.1em] uppercase max-[760px]:flex-col">
        <span>FREECIV AGENT EVALUATION</span>
        <span>Public spectator telemetry · not available to player agents</span>
      </footer>
    </main>
    </DisplayPaletteProvider>
  )
}

function ArenaCanonicalRedirect({ target }: { target: string }) {
  useEffect(() => {
    window.location.replace(`${target}${window.location.search}${window.location.hash}`)
  }, [target])
  return (
    <main className={CENTER_STAGE}>
      <div className="loading-orbit" aria-hidden="true"><span /></div>
      <p className="eyebrow">Canonicalizing arena route</p>
      <h1 className={CENTER_TITLE}>Opening Agent Arena</h1>
    </main>
  )
}

export default function App() {
  const route = resolveViewerRoute(window.location.pathname)
  if (route.kind === 'arena') return <ArenaPicker route={route.context} />
  if (route.kind === 'arena-redirect') return <ArenaCanonicalRedirect target={route.target} />
  if (route.kind === 'watch') return <MatchViewer route={route.context} />
  return (
    <main className={CENTER_STAGE}>
      <span className="status-glyph">!</span>
      <p className="eyebrow">Page unavailable</p>
      <h1 className={CENTER_TITLE}>This is not an Agent Arena route</h1>
      <p className="max-w-[580px] text-muted">Open the arena index or use a complete /watch/GAME_ID URL.</p>
    </main>
  )
}
