import type { ReactNode } from 'react'

export const MAP_REGION_ID = 'strategic-map-region'

interface MapSectionProps {
  /** Only rendered while expanded: the board renderer is a large lazy chunk. */
  board: ReactNode
  expanded: boolean
  legend: ReactNode
  live: boolean
  onToggle: (next: boolean) => void
  /** Turn transport, always rendered: it drives every other overview panel. */
  playback: ReactNode
  turn: number
}

export function MapSection({
  board,
  expanded,
  legend,
  live,
  onToggle,
  playback,
  turn,
}: MapSectionProps) {
  return (
    <section className={expanded ? 'strategy-grid' : 'strategy-grid map-collapsed'} aria-label="Strategic map">
      <article className="panel map-panel">
        <div className="panel-heading">
          <div>
            <p className="eyebrow">Omniscient strategic map</p>
            <h2>World state at turn {turn || '—'}</h2>
          </div>
          <div className="map-heading-actions">
            <span className={live ? 'live-indicator' : 'history-indicator'}>{live ? 'LIVE FOLLOW' : 'HISTORICAL'}</span>
            <button
              aria-controls={MAP_REGION_ID}
              aria-expanded={expanded}
              className="map-disclosure"
              onClick={() => onToggle(!expanded)}
              type="button"
            >
              {expanded ? 'Hide map' : 'Show map'}
            </button>
          </div>
        </div>
        <div id={MAP_REGION_ID}>
          {expanded ? board : (
            <p className="map-collapsed-note">
              The world board renderer stays unloaded until you open it. Turn selection below still drives
              the leaderboard, charts, and technology panels.
            </p>
          )}
        </div>
        {playback}
      </article>
      {expanded && legend}
    </section>
  )
}
