import type { Film, TurnState } from '../dataset/film'
import { nationDisplayName } from '../faction-label'
import { formatCount, formatYear } from '../format'
import { SHELL, withAlpha } from '../theme'

interface TickerProps {
  readonly film: Film
  readonly turn: TurnState
  readonly turnIndex: number
  readonly width: number
}

/** Territory claimed per player, as a share of the map's land tiles. */
function TerritoryBar({ film, turn, width }: Omit<TickerProps, 'turnIndex'>) {
  const segments = film.tracks
    .map((track) => ({
      color: track.renderColor,
      label: nationDisplayName(track.player),
      tiles: turn.territoryByPlayer.get(track.player.playerId) ?? 0,
    }))
    .filter((segment) => segment.tiles > 0)

  return (
    <div className="flex flex-col gap-[6px]" style={{ width }}>
      <div className="flex justify-between font-mono text-[10px] tracking-[1.8px] text-muted">
        <span>TERRITORY</span>
        <span>
          {segments
            .map((segment) => `${segment.label} ${formatCount(segment.tiles)}`)
            .join('  ·  ') || 'UNCLAIMED WORLD'}
        </span>
      </div>
      <div
        className="flex h-[6px] overflow-hidden rounded-sm"
        style={{ background: withAlpha(SHELL.line, 0.8), width }}
      >
        {segments.map((segment) => (
          <div
            key={segment.label}
            style={{
              background: segment.color,
              width: `${((segment.tiles / Math.max(1, film.landTiles)) * 100).toFixed(3)}%`,
            }}
          />
        ))}
        <div className="flex-1" style={{ background: withAlpha('#445156', 0.5) }} />
      </div>
    </div>
  )
}

export function Ticker({ film, turn, turnIndex, width }: TickerProps) {
  const progress = film.turns.length <= 1 ? 1 : turnIndex / (film.turns.length - 1)
  const totalCities = turn.cities.length
  const totalUnits = turn.units.reduce((total, stack) => total + (stack[3] ?? 0), 0)

  return (
    <div className="flex flex-col gap-[14px]" style={{ width }}>
      <div className="flex items-end gap-[26px]">
        <div className="flex flex-col">
          <span className="font-mono text-[11px] tracking-[2.4px] text-muted">TURN</span>
          <span className="font-mono text-[62px] leading-[0.92] font-bold tracking-[-2px] text-ink tabular-nums">
            {turn.turn}
          </span>
        </div>
        <span className="pb-[6px] font-serif text-[34px] text-amber">
          {formatYear(turn.year)}
        </span>
        <div className="ml-auto flex flex-col gap-[4px] pb-[6px] text-right font-mono text-[12px] text-muted">
          <span>{formatCount(totalCities)} cities on the map</span>
          <span>{formatCount(totalUnits)} units afield</span>
          {turn.interpolated && (
            <span className="text-amber">
              board held from turn {turn.boardTurn ?? '—'}
            </span>
          )}
        </div>
      </div>
      <div className="h-[3px]" style={{ background: withAlpha(SHELL.line, 0.8), width }}>
        <div
          className="h-[3px] bg-cyan"
          style={{ width: `${(progress * 100).toFixed(3)}%` }}
        />
      </div>
      <TerritoryBar film={film} turn={turn} width={width} />
    </div>
  )
}
