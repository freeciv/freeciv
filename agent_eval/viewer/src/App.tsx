import { useEffect, useMemo, useRef, useState } from 'react'
import { fetchWatch, fetchWatchWithOptionalReplay } from './api'
import { ArenaPicker } from './components/ArenaPicker'
import { ColorMark } from './components/ColorMark'
import { MapSection } from './components/MapSection'
import { MetricChart } from './components/MetricChart'
import { StrategicMap } from './components/StrategicMap'
import { TechnologyPanel } from './components/TechnologyPanel'
import { TechnologyProgressChart } from './components/TechnologyProgressChart'
import { MAP_HASH, mapSectionOpen, rememberMapSection } from './map-preference'
import { controlProtocolLabel, placeLabel, timingModeLabel } from './picker-model'
import { frameImageUrl, resolveViewerRoute } from './route'
import type {
  ReplayPlayer,
  ReplaySnapshot,
  RouteContext,
  TechnologyCatalog,
  WatchResponse,
} from './types'
import {
  METRICS,
  competitorLabel,
  configuredPlaceFactions,
  frameAtOrBefore,
  isScoredPlayer,
  mapFactions,
  matchHeaderLabel,
  maxKnownTechnologyDepth,
  playerMetric,
  scoreDisplayAtTurn,
  snapshotAtOrBefore,
  turnsAvailable,
} from './view-model'

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
      <main className="center-stage">
        <div className="loading-orbit" aria-hidden="true"><span /></div>
        <p className="eyebrow">Synchronizing spectator telemetry</p>
        <h1>Loading Freeciv agent arena</h1>
      </main>
    )
  }

  if (!watch) {
    return (
      <main className="center-stage error-stage">
        <span className="status-glyph">!</span>
        <p className="eyebrow">Replay unavailable</p>
        <h1>Could not open this match</h1>
        <p>{error ?? 'No watch data was returned by the supervisor.'}</p>
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
  const selectedYear = selectedSnapshot?.year
  const validityClass = game.benchmark_valid === true
    ? 'validity-valid'
    : game.benchmark_valid === false
      ? 'validity-invalid'
      : 'validity-pending'
  const historicalComparison = selectedTurn > 0 && lastTurn > 0 && selectedTurn < lastTurn
  const timing = timingModeLabel(game)
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
    <main className="app-shell">
      <nav className="product-bar" aria-label="Freeciv Agent Arena">
        <a href={route.prefix ? `${route.prefix}/` : '/'}><span className="product-sigil" aria-hidden="true">FC</span><strong>Freeciv Agent Arena</strong></a>
        <span>{game.mode === 'single' ? 'Single player evaluation' : 'Multiplayer evaluation'}</span>
        <code>{game.game_id}</code>
      </nav>
      <header className="match-header">
        <div className="brand-block">
          <div className="brand-mark" aria-hidden="true"><span /><span /><span /></div>
          <div>
            <p className="eyebrow">Freeciv autonomous arena</p>
            <h1>{matchHeaderLabel(game.resolved_places)}</h1>
          </div>
        </div>
        <div className="header-status">
          <span className={`state-pill state-${game.state}`}><i />{stateLabel(game.state)}</span>
          <span className="turn-readout"><small>TURN</small>{selectedTurn || '—'}<em>{selectedYear == null ? '' : ` / ${selectedYear}`}</em></span>
        </div>
        <p className="game-code">{game.game_id}</p>
      </header>

      {error && <div className="refresh-warning" role="status">Live refresh issue: {error}. Showing the latest retained data.</div>}

      <div className="match-content" id="match-content">

      <section className="result-ribbon" aria-label="Match outcome and validity">
        <div>
          <p className="eyebrow">Current result</p>
          <strong>{game.outcome.summary}</strong>
          {game.outcome.victory && (
            <span className="victory-chip" title={`Victory condition: ${game.outcome.victory.code}`}>
              Game ended: {game.outcome.victory.label}
              {game.outcome.victory.turn ? ` on turn ${game.outcome.victory.turn}` : ''}
              {game.outcome.victory.winners.length > 0
                ? ` · ${game.outcome.victory.winners.join(', ')}`
                : ''}
            </span>
          )}
          <span>{game.objective}</span>
        </div>
        <div className={`validity-chip ${validityClass}`}>
          <span>{validityLabel(game.benchmark_valid)}</span>
          <small>{game.benchmark_valid === false
            ? game.invalid_reasons.join(' · ') || game.error || 'Run is not benchmark eligible'
            : game.benchmark_valid === true
              ? 'Eligible for model comparison'
              : 'Final validity is decided when the match ends'}</small>
        </div>
      </section>

      <section className={`match-context${timing ? ' with-timing' : ''}`} aria-label="Match configuration and seat status">
        <div><p className="eyebrow">Mode</p><strong>{game.mode === 'single' ? 'Single player vs native AI' : 'Multiplayer agent match'}</strong><span>{game.places} total places · {game.max_agents} external agent {game.max_agents === 1 ? 'seat' : 'seats'}</span></div>
        <div><p className="eyebrow">Control protocol</p><strong>{protocol.label}</strong><span>{protocol.detail}{protocol.assumed ? ' · assumed for this archived run' : ''}</span></div>
        <div><p className="eyebrow">Agent lobby</p><strong>{game.joined_agents}/{game.max_agents} joined</strong><span>{game.state === 'lobby' && game.max_agents > game.joined_agents ? `Waiting for ${game.max_agents - game.joined_agents} agent${game.max_agents - game.joined_agents === 1 ? '' : 's'}` : game.state === 'lobby' ? 'All agents joined · preparing match' : 'Roster locked for this match'}</span></div>
        <div><p className="eyebrow">Turn horizon</p><strong>{game.current_turn ?? 0} / {game.turns}</strong><span>{game.state === 'running' ? 'Authoritative turn in progress' : stateLabel(game.state)}</span></div>
        {timing && <div><p className="eyebrow">Turn timing</p><strong>{timing}</strong><span>Harness action deadline</span></div>}
      </section>

      <aside className="overview-rail-card panel" aria-label="Current score comparison">
        <div className="panel-heading compact-heading"><div><p className="eyebrow">{historicalComparison ? 'Selected-turn comparison' : 'Current comparison'}</p><h2>Leaderboard</h2></div><span>{historicalComparison ? `T${selectedTurn}` : 'LATEST'}</span></div>
        <div className="comparison-rows">
          {comparisonRows.some((row) => row.score !== undefined) ? comparisonRows.map((row, index) => (
            <div key={row.place.seat_id}><b>{index + 1}</b><ColorMark color={row.place.player_color} label={placeLabel(row.place)} size="sm" /><span><strong>{placeLabel(row.place)}</strong><small>{row.place.model || row.place.player_name}</small></span><em>{row.score?.toLocaleString() ?? '—'}</em></div>
          )) : <p className="empty-copy">Scores appear after the first resolved turn.</p>}
        </div>
      </aside>

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
            <article className="competitor-card" key={place.seat_id}>
              <ColorMark color={place.player_color} label={label} size="lg" />
              <div className="competitor-identity">
                <strong>{label}</strong>
                <span>{place.player_name}{telemetry?.nation ? ` · ${telemetry.nation}` : ''}</span>
              </div>
              <div className="competitor-score">
                <small>{displayedScore.label}</small>
                <strong>{displayedScore.value?.toLocaleString() ?? '—'}</strong>
              </div>
              <span className="color-code">{place.player_color}</span>
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
                <svg aria-hidden="true" className="intelligence-accent" preserveAspectRatio="none" viewBox="0 0 100 2"><line stroke={player.player_color || '#82919d'} strokeWidth="2" x1="0" x2="100" y1="1" y2="1" /></svg>
                <div className="intelligence-title"><ColorMark color={player.player_color} label={competitorLabel(player)} size="lg" /><div><p className="eyebrow">Scored controller</p><h2>{competitorLabel(player)}</h2><span>{player.nation || player.player_name} · {player.government || 'Government unknown'}</span></div></div>
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
                <div className="recent-acquisitions">
                  <small>Acquisition history</small>
                  <div>{acquisitions.length ? acquisitions.slice(-4).reverse().map((acquisition, index) => <span key={`${acquisition.turn}-${acquisition.name}-${index}`}><b>T{acquisition.turn}</b>{acquisition.name}</span>) : <em>No new technology recorded yet</em>}</div>
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
          <article className="stat-card" key={metric.key}>
            <p>{metric.label}</p>
            {scoredPlayers.length ? scoredPlayers.map((player) => (
              <div key={player.seat_id}>
                <ColorMark color={player.player_color} label={competitorLabel(player)} size="sm" />
                <span>{competitorLabel(player)}</span>
                <strong>{playerMetric(player, metric.key).toLocaleString()}</strong>
              </div>
            )) : <span className="empty-copy">No telemetry</span>}
          </article>
        ))}
      </section>

      <section className="charts-grid" aria-label="Metric comparison charts">
        {METRICS.map((metric) => (
          <MetricChart key={metric.key} label={metric.label} metric={metric.key} snapshots={snapshots.filter((snapshot) => snapshot.turn <= selectedTurn)} />
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
            <p className="section-note">{basicTelemetry
              ? 'Scored controller colors come from the match roster. Dynamic faction identities were not recorded by this older supervisor.'
              : 'Scored controllers and Freeciv-created factions are listed separately. Dynamic factions never enter the benchmark leaderboard.'}</p>
            <div className="faction-list">
              {factions.length ? factions.map((faction) => (
                <article className={faction.dynamic ? 'faction-row dynamic-faction' : 'faction-row'} key={`${faction.player_id}-${faction.player_name}`}>
                  <ColorMark color={faction.player_color} label={faction.display_label} />
                  <div><strong>{faction.display_label}</strong><span>{faction.detail}</span></div>
                  <code>{faction.player_color}</code>
                </article>
              )) : <div className="empty-state small-empty"><strong>Legend pending</strong><span>Waiting for map header metadata.</span></div>}
            </div>
          </aside>
        )}
        playback={(
          <div className="playback-bar">
            <div className="playback-transport" aria-label="Replay transport controls">
              <button aria-label="Previous turn" className="step-button" disabled={previousTurn === undefined} onClick={() => stepReplay(previousTurn)} type="button">‹</button>
              <button aria-label={playing ? 'Pause replay' : 'Play replay'} className="play-button" disabled={availableTurns.length < 2} onClick={togglePlayback} type="button">
                {playing ? 'Ⅱ' : '▶'}
              </button>
              <button aria-label="Next turn" className="step-button" disabled={nextTurn === undefined} onClick={() => stepReplay(nextTurn)} type="button">›</button>
            </div>
            <label className="scrubber-label">
              <span>Turn {selectedTurn || '—'}</span>
              <input
                aria-label="Replay turn"
                disabled={!availableTurns.length}
                max={Math.max(0, availableTurns.length - 1)}
                min={0}
                onChange={(event) => chooseTurn(availableTurns[Number(event.target.value)] ?? selectedTurn)}
                type="range"
                value={selectedTurnIndex}
              />
            </label>
            <label className="speed-select">Speed
              <select aria-label="Playback speed" onChange={(event) => setSpeed(Number(event.target.value))} value={speed}>
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
          <span>{watch.timeline.length} resolved</span>
        </div>
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
        {warnings.length > 0 && (
          <div className="telemetry-warning" role="status">
            <strong>Replay telemetry warnings</strong>
            <span>{warnings.map((warning) => warning.turn ? `Turn ${warning.turn}: ${warning.message}` : warning.message).join(' · ')}</span>
          </div>
        )}
      </section>
      </div>

      <footer>
        <span>FREECIV AGENT EVALUATION</span>
        <span>Public spectator telemetry · not available to player agents</span>
      </footer>
    </main>
  )
}

function ArenaCanonicalRedirect({ target }: { target: string }) {
  useEffect(() => {
    window.location.replace(`${target}${window.location.search}${window.location.hash}`)
  }, [target])
  return (
    <main className="center-stage">
      <div className="loading-orbit" aria-hidden="true"><span /></div>
      <p className="eyebrow">Canonicalizing arena route</p>
      <h1>Opening Agent Arena</h1>
    </main>
  )
}

export default function App() {
  const route = resolveViewerRoute(window.location.pathname)
  if (route.kind === 'arena') return <ArenaPicker route={route.context} />
  if (route.kind === 'arena-redirect') return <ArenaCanonicalRedirect target={route.target} />
  if (route.kind === 'watch') return <MatchViewer route={route.context} />
  return (
    <main className="center-stage error-stage">
      <span className="status-glyph">!</span>
      <p className="eyebrow">Page unavailable</p>
      <h1>This is not an Agent Arena route</h1>
      <p>Open the arena index or use a complete /watch/GAME_ID URL.</p>
    </main>
  )
}
