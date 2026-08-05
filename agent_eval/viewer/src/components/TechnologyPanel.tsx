import type { ReplayPlayer, ReplaySnapshot, Technology } from '../types'
import { competitorLabel, technologyState } from '../view-model'
import { ColorMark } from './ColorMark'

interface TechnologyPanelProps {
  catalog: Technology[]
  player?: ReplayPlayer
  scoredPlayers: ReplayPlayer[]
  selectedSeat: string
  setSelectedSeat: (seat: string) => void
  snapshots: ReplaySnapshot[]
}

export function TechnologyPanel({
  catalog,
  player,
  scoredPlayers,
  selectedSeat,
  setSelectedSeat,
  snapshots,
}: TechnologyPanelProps) {
  const research = player?.research
  const progress = research && research.cost > 0
    ? Math.min(research.bulbs, research.cost)
    : 0
  const acquisitions = snapshots.flatMap((snapshot) => {
    const current = snapshot.players.find((candidate) => candidate.seat_id === selectedSeat)
    return (current?.gained_tech_ids ?? []).map((id) => ({ turn: snapshot.turn, id }))
  })
  const techById = new Map(catalog.map((technology) => [technology.id, technology]))
  const depths = [...new Set(catalog.map((technology) => technology.depth ?? 0))].sort((a, b) => a - b)

  return (
    <section className="panel technology-panel" aria-labelledby="technology-title">
      <div className="panel-heading tech-heading">
        <div>
          <p className="eyebrow">Knowledge race</p>
          <h2 id="technology-title">Technology progression</h2>
        </div>
        <label className="select-label">
          Controller
          <select value={selectedSeat} onChange={(event) => setSelectedSeat(event.target.value)}>
            {scoredPlayers.map((candidate) => (
              <option key={candidate.seat_id} value={candidate.seat_id}>
                {competitorLabel(candidate)}
              </option>
            ))}
          </select>
        </label>
      </div>

      {!player ? (
        <div className="empty-state"><strong>No controller selected</strong><span>Replay telemetry has not arrived yet.</span></div>
      ) : (
        <>
          <div className="research-strip">
            <ColorMark color={player.player_color} label={competitorLabel(player)} size="lg" />
            <div className="research-copy">
              <span>Current research</span>
              <strong>{research?.name || 'No active target'}</strong>
              <small>{research ? `${research.bulbs.toLocaleString()} / ${research.cost.toLocaleString()} bulbs` : '—'}</small>
            </div>
            <progress aria-label="Research progress" max={Math.max(1, research?.cost ?? 1)} value={progress} />
            <div className="known-count">
              <strong>{player.known_tech_ids.length}</strong>
              <span>known</span>
            </div>
          </div>

          <div className="tech-grid-wrap" aria-label="Technology dependency progression">
            <div className="tech-depth-grid">
              {depths.map((depth) => (
                <div className="tech-column" key={depth}>
                  <p>Depth {depth}</p>
                  {catalog.filter((technology) => (technology.depth ?? 0) === depth).map((technology) => {
                    const state = technologyState(technology, player)
                    return (
                      <article className={`tech-node tech-${state}`} key={technology.id}>
                        <span>{state}</span>
                        <strong>{technology.name}</strong>
                        <small>{Math.round(technology.cost_base).toLocaleString()} base bulbs</small>
                      </article>
                    )
                  })}
                </div>
              ))}
            </div>
          </div>

          <div className="acquisition-block">
            <div>
              <p className="eyebrow">Verified acquisition log</p>
              <h3>New technologies by turn</h3>
            </div>
            <div className="acquisition-list">
              {acquisitions.length ? acquisitions.map(({ turn, id }, index) => (
                <span key={`${turn}-${id}-${index}`}>
                  <b>T{turn}</b>{techById.get(id)?.name ?? `Technology ${id}`}
                </span>
              )) : <span className="empty-copy">No new technology recorded through this turn.</span>}
            </div>
          </div>
        </>
      )}
    </section>
  )
}
