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
    <div className="flex flex-col gap-[7px]" style={{ width }}>
      <div className="flex justify-between">
        <span className="label">Territory</span>
        <span className="label">
          {segments
            .map((segment) => `${segment.label} ${formatCount(segment.tiles)}`)
            .join('  ·  ') || 'unclaimed world'}
        </span>
      </div>
      <div
        className="flex h-[6px] overflow-hidden"
        style={{ background: SHELL.panelRaised, width }}
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
        <div className="flex-1" style={{ background: withAlpha(SHELL.ink, 0.05) }} />
      </div>
    </div>
  )
}

export function Ticker({ film, turn, turnIndex, width }: TickerProps) {
  const progress = film.turns.length <= 1 ? 1 : turnIndex / (film.turns.length - 1)
  const totalCities = turn.cities.length
  const totalUnits = turn.units.reduce((total, stack) => total + (stack[3] ?? 0), 0)

  return (
    <div className="flex flex-col gap-[15px]" style={{ width }}>
      {/* Two readings of the same axis, given the same shape: the turn counter
          the harness drives, and the in-fiction year it produced. */}
      <div className="flex items-end gap-[40px]">
        <div className="flex flex-col gap-[5px]">
          <span className="label">Turn</span>
          <span className="font-mono text-[58px] leading-[0.86] font-medium tracking-[-0.045em] text-ink tabular-nums">
            {turn.turn}
          </span>
        </div>
        <div className="flex flex-col gap-[5px]">
          <span className="label">Year</span>
          <span className="font-display text-[40px] leading-[0.86] font-normal tracking-[-0.03em] text-muted">
            {formatYear(turn.year)}
          </span>
        </div>
        <div className="ml-auto flex flex-col gap-[5px] pb-[3px] text-right font-mono text-[12px] text-muted">
          <span>{formatCount(totalCities)} cities on the map</span>
          <span>{formatCount(totalUnits)} units afield</span>
          {turn.interpolated && (
            <span className="text-amber">
              board held from turn {turn.boardTurn ?? '—'}
            </span>
          )}
        </div>
      </div>
      {/* The match clock. Maximum contrast rather than a brand accent: the only
          saturation in the shell belongs to the factions. */}
      <div className="h-[2px]" style={{ background: withAlpha(SHELL.ink, 0.09), width }}>
        <div
          className="h-[2px] bg-ink"
          style={{ width: `${(progress * 100).toFixed(3)}%` }}
        />
      </div>
      <TerritoryBar film={film} turn={turn} width={width} />
    </div>
  )
}
