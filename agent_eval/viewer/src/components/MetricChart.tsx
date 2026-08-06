import { displayPlayerColor } from '../display-color'
import type { ReplayPlayer, ReplaySnapshot } from '../types'
import { competitorLabel, isScoredPlayer, playerMetric, type MetricKey } from '../view-model'
import { ColorMark } from './ColorMark'

interface MetricChartProps {
  label: string
  metric: MetricKey
  snapshots: ReplaySnapshot[]
}

interface Series {
  key: string
  label: string
  color: string
  values: { turn: number; value: number }[]
}

function chartSeries(snapshots: ReplaySnapshot[], metric: MetricKey): Series[] {
  const players = new Map<string, ReplayPlayer>()
  for (const snapshot of snapshots) {
    for (const player of snapshot.players) {
      if (isScoredPlayer(player)) players.set(player.seat_id, player)
    }
  }
  return [...players.entries()].map(([seatId, player]) => ({
    key: seatId,
    label: competitorLabel(player),
    color: displayPlayerColor(player.player_color) ?? '#82919d',
    values: snapshots.flatMap((snapshot) => {
      const current = snapshot.players.find((candidate) => candidate.seat_id === seatId)
      return current ? [{ turn: snapshot.turn, value: playerMetric(current, metric) }] : []
    }),
  }))
}

function points(values: Series['values'], minTurn: number, maxTurn: number, maxValue: number) {
  const width = 560
  const height = 156
  const xRange = Math.max(1, maxTurn - minTurn)
  return values.map(({ turn, value }) => {
    const x = 10 + ((turn - minTurn) / xRange) * (width - 20)
    const y = height - 10 - (value / Math.max(1, maxValue)) * (height - 22)
    return `${x.toFixed(1)},${y.toFixed(1)}`
  }).join(' ')
}

export function MetricChart({ label, metric, snapshots }: MetricChartProps) {
  const series = chartSeries(snapshots, metric)
  const turns = snapshots.map((snapshot) => snapshot.turn)
  const minTurn = Math.min(...turns, 0)
  const maxTurn = Math.max(...turns, 1)
  const maxValue = Math.max(
    1,
    ...series.flatMap((item) => item.values.map((point) => point.value)),
  )

  return (
    <article className="panel metric-chart">
      <div className="panel-heading compact-heading">
        <div>
          <p className="eyebrow">Comparison telemetry</p>
          <h3>{label}</h3>
        </div>
        <strong className="text-[#75848e] font-bold text-[13px] leading-none font-readout">{maxValue.toLocaleString()}</strong>
      </div>
      {snapshots.length ? (
        <svg aria-label={`${label} by turn`} className="line-chart" role="img" viewBox="0 0 560 170">
          <line x1="10" x2="550" y1="146" y2="146" />
          <line x1="10" x2="550" y1="82" y2="82" />
          <line x1="10" x2="550" y1="18" y2="18" />
          {series.map((item) => (
            <polyline
              fill="none"
              key={item.key}
              points={points(item.values, minTurn, maxTurn, maxValue)}
              stroke={item.color}
              strokeLinecap="round"
              strokeLinejoin="round"
              strokeWidth="4"
              vectorEffect="non-scaling-stroke"
            />
          ))}
        </svg>
      ) : <p className="empty-copy">Waiting for the first replay snapshot.</p>}
      <div className="flex flex-wrap gap-x-[13px] gap-y-2 pt-[5px] px-[14px] pb-[13px] text-muted text-[9px]">
        {series.map((item) => (
          <span className="inline-flex items-center gap-[5px]" key={item.key}><ColorMark color={item.color} label={item.label} size="sm" />{item.label}</span>
        ))}
      </div>
    </article>
  )
}
