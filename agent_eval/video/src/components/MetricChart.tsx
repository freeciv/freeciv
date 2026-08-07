import { useMemo } from 'react'
import { SHELL, withAlpha } from '../theme'

export interface ChartSeries {
  readonly key: number
  readonly color: string
  readonly values: readonly number[]
}

interface MetricChartProps {
  readonly series: readonly ChartSeries[]
  readonly turnIndex: number
  readonly progress: number
  readonly width: number
  readonly height: number
  readonly totalTurns: number
  readonly firstTurn: number
  readonly label: string
  /** Fixed ceiling; when absent the axis grows with the revealed maximum. */
  readonly ceiling?: number
  readonly showTurnAxis?: boolean
}

function niceCeiling(value: number): number {
  if (value <= 10) return 10
  const magnitude = 10 ** Math.floor(Math.log10(value))
  for (const step of [1, 1.5, 2, 3, 4, 5, 7.5, 10]) {
    if (value <= step * magnitude) return step * magnitude
  }
  return 10 * magnitude
}

/**
 * One shared line chart for every per-turn metric the dataset carries. The
 * match panel reveals it up to the current turn; the end screen passes the
 * final turn and a fixed ceiling to draw the whole history at once.
 *
 * Two seats means two lines crossing constantly, so there is no area fill: two
 * translucent wedges overlapping turned every chart in the film into the same
 * brown smear and buried the lines that carry the reading. Weight and colour
 * carry the series instead. The value axis sits in the right margin so the
 * lines start flush with the left edge and the plot keeps its full width.
 */
export function MetricChart({
  series, turnIndex, progress, width, height, totalTurns, firstTurn, label,
  ceiling, showTurnAxis = true,
}: MetricChartProps) {
  const padding = {
    top: 26, right: 44, bottom: showTurnAxis ? 22 : 12, left: 14,
  }
  const plotWidth = width - padding.left - padding.right
  const plotHeight = height - padding.top - padding.bottom
  const revealed = turnIndex + progress

  const axisMax = useMemo(() => {
    if (ceiling !== undefined) return niceCeiling(ceiling)
    let peak = 1
    for (const line of series) {
      const upTo = Math.min(turnIndex + 1, line.values.length - 1)
      for (let index = 0; index <= upTo; index += 1) {
        peak = Math.max(peak, line.values[index] ?? 0)
      }
    }
    return niceCeiling(peak)
  }, [ceiling, series, turnIndex])

  const toX = (index: number): number =>
    padding.left + (totalTurns <= 1 ? 0 : (index / (totalTurns - 1)) * plotWidth)
  const toY = (value: number): number =>
    padding.top + plotHeight - Math.min(1, value / axisMax) * plotHeight

  return (
    <svg className="block" height={height} width={width}>
      <rect fill={SHELL.panelRaised} height={height} width={width} x={0} y={0} />
      {[0, 0.5, 1].map((fraction) => {
        const y = padding.top + plotHeight - fraction * plotHeight
        return (
          <g key={fraction}>
            <line
              stroke={withAlpha(SHELL.ink, fraction === 0 ? 0.1 : 0.055)}
              strokeDasharray={fraction === 0 ? undefined : '2 4'}
              strokeWidth={1}
              x1={padding.left}
              x2={width - padding.right}
              y1={y}
              y2={y}
            />
            <text
              className="font-mono"
              fill={SHELL.dim}
              fontSize={10}
              x={width - padding.right + 9}
              y={y + 3.5}
            >
              {Math.round(axisMax * fraction).toLocaleString('en-US')}
            </text>
          </g>
        )
      })}
      <text
        className="font-mono"
        fill={SHELL.muted}
        fontSize={10}
        fontWeight={500}
        letterSpacing={1.6}
        x={padding.left}
        y={15}
      >
        {label.toUpperCase()}
      </text>
      {series.map((line) => {
        const points: string[] = []
        const upTo = Math.min(Math.floor(revealed), line.values.length - 1)
        for (let index = 0; index <= upTo; index += 1) {
          points.push(`${toX(index).toFixed(2)},${toY(line.values[index] ?? 0).toFixed(2)}`)
        }
        if (upTo + 1 < line.values.length && progress > 0) {
          const current = line.values[upTo] ?? 0
          const next = line.values[upTo + 1] ?? current
          points.push(
            `${toX(revealed).toFixed(2)},${toY(current + (next - current) * progress).toFixed(2)}`,
          )
        }
        if (points.length < 2) return null
        const head = points[points.length - 1]?.split(',') ?? []
        const headX = Number(head[0] ?? 0)
        const headY = Number(head[1] ?? 0)
        return (
          <g key={line.key}>
            <polyline
              fill="none"
              points={points.join(' ')}
              stroke={line.color}
              strokeLinecap="square"
              strokeLinejoin="miter"
              strokeWidth={2.1}
            />
            {/* A flat marker, not a lit one: the head is where the reading is,
                and a halo ring around it only added light. */}
            <rect
              fill={line.color}
              height={7}
              width={7}
              x={headX - 3.5}
              y={headY - 3.5}
            />
          </g>
        )
      })}
      <line
        stroke={withAlpha(SHELL.ink, 0.16)}
        strokeDasharray="2 4"
        strokeWidth={1}
        x1={toX(revealed)}
        x2={toX(revealed)}
        y1={padding.top}
        y2={padding.top + plotHeight}
      />
      {showTurnAxis && (
        <>
          <text
            className="font-mono"
            fill={SHELL.dim}
            fontSize={10}
            x={padding.left}
            y={height - 7}
          >
            T{firstTurn}
          </text>
          <text
            className="font-mono"
            fill={SHELL.dim}
            fontSize={10}
            textAnchor="end"
            x={width - padding.right}
            y={height - 7}
          >
            T{firstTurn + totalTurns - 1}
          </text>
        </>
      )}
    </svg>
  )
}
