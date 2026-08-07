import { useMemo } from 'react'
import type { ReplayPlayer, ReplaySnapshot, Technology } from '../types'
import { agentFirstBy, isAgentController } from '../agent-order'
import { displayPlayerColor } from '../display-color'
import { useDisplayPalette } from '../display-palette'
import { factionDisplayLabel } from '../faction-label'
import { isScoredPlayer } from '../view-model'
import { ColorMark } from './ColorMark'

export interface TechnologyProgressPoint {
  turn: number
  knownClassicTechnologies: number
  futureTechs: number
}

export interface TechnologyProgressSeries {
  key: string
  label: string
  /** The recorded color. Paint it through `displayPlayerColor`. */
  color: string | null
  values: TechnologyProgressPoint[]
  selected: TechnologyProgressPoint | null
  latest: TechnologyProgressPoint
  acquiredInObservedWindow: number
  observedTurnSpan: number
  pacePerTenTurns: number | null
}

export type TechnologyProgressStatus =
  | 'ready'
  | 'catalog-unavailable'
  | 'snapshots-unavailable'
  | 'controllers-unavailable'

export interface TechnologyProgressModel {
  status: TechnologyProgressStatus
  catalogTotal: number
  selectedTurn: number | null
  minTurn: number | null
  maxTurn: number | null
  series: TechnologyProgressSeries[]
}

export interface TechnologyProgressChartProps {
  catalog?: readonly Technology[] | null
  snapshots: readonly ReplaySnapshot[]
  selectedTurn?: number | null
}

interface MutableSeries {
  player: ReplayPlayer
  known: Set<number>
  valuesByTurn: Map<number, TechnologyProgressPoint>
}

function pointAtOrBefore(
  values: readonly TechnologyProgressPoint[],
  turn: number,
): TechnologyProgressPoint | null {
  let low = 0
  let high = values.length - 1
  let selectedIndex = -1
  while (low <= high) {
    const middle = Math.floor((low + high) / 2)
    if (values[middle].turn <= turn) {
      selectedIndex = middle
      low = middle + 1
    } else {
      high = middle - 1
    }
  }
  return selectedIndex < 0 ? null : values[selectedIndex]
}

export function selectTechnologyProgressTurn(
  model: TechnologyProgressModel,
  selectedTurn?: number | null,
): TechnologyProgressModel {
  if (model.status !== 'ready') return model
  const normalizedSelectedTurn = typeof selectedTurn === 'number' && Number.isFinite(selectedTurn)
    ? Math.trunc(selectedTurn)
    : model.maxTurn!
  return {
    ...model,
    selectedTurn: normalizedSelectedTurn,
    series: model.series.map((series) => ({
      ...series,
      selected: pointAtOrBefore(series.values, normalizedSelectedTurn),
    })),
  }
}

/**
 * Builds monotonic, catalog-backed technology series from replay snapshots.
 *
 * A classic technology remains counted after it has been observed, even if a
 * later snapshot reports it as lost. Unknown IDs and Future Tech levels are
 * deliberately excluded from the classic-technology curve.
 */
export function buildTechnologyProgressSeries(
  snapshots: readonly ReplaySnapshot[],
  catalog: readonly Technology[] | null | undefined,
  selectedTurn?: number | null,
): TechnologyProgressModel {
  const catalogIds = new Set((catalog ?? []).map((technology) => technology.id))
  if (!catalogIds.size) {
    return {
      status: 'catalog-unavailable',
      catalogTotal: 0,
      selectedTurn: null,
      minTurn: null,
      maxTurn: null,
      series: [],
    }
  }

  const orderedSnapshots = [...snapshots].sort((left, right) => left.turn - right.turn)
  if (!orderedSnapshots.length) {
    return {
      status: 'snapshots-unavailable',
      catalogTotal: catalogIds.size,
      selectedTurn: null,
      minTurn: null,
      maxTurn: null,
      series: [],
    }
  }

  const builders = new Map<string, MutableSeries>()
  for (const snapshot of orderedSnapshots) {
    for (const player of snapshot.players) {
      if (!isScoredPlayer(player)) continue
      const builder = builders.get(player.seat_id) ?? {
        player,
        known: new Set<number>(),
        valuesByTurn: new Map<number, TechnologyProgressPoint>(),
      }
      builder.player = player
      for (const id of player.known_tech_ids) {
        if (catalogIds.has(id)) builder.known.add(id)
      }
      for (const id of player.gained_tech_ids) {
        if (catalogIds.has(id)) builder.known.add(id)
      }
      const futureTechs = Number.isFinite(player.future_techs)
        ? Math.max(0, Math.trunc(player.future_techs))
        : 0
      builder.valuesByTurn.set(snapshot.turn, {
        turn: snapshot.turn,
        knownClassicTechnologies: builder.known.size,
        futureTechs,
      })
      builders.set(player.seat_id, builder)
    }
  }

  if (!builders.size) {
    return {
      status: 'controllers-unavailable',
      catalogTotal: catalogIds.size,
      selectedTurn: null,
      minTurn: null,
      maxTurn: null,
      series: [],
    }
  }

  const minTurn = Math.min(
    ...[...builders.values()].flatMap((builder) => [...builder.valuesByTurn.keys()]),
  )
  const maxTurn = Math.max(
    ...[...builders.values()].flatMap((builder) => [...builder.valuesByTurn.keys()]),
  )
  // Agent first, the same order the rest of the page states the sides in.
  const series = agentFirstBy(
    [...builders.entries()], ([, builder]) => isAgentController(builder.player),
  ).map(([seatId, builder]) => {
    const values = [...builder.valuesByTurn.values()].sort((left, right) => left.turn - right.turn)
    const first = values[0]
    const latest = values.at(-1)!
    const observedTurnSpan = latest.turn - first.turn
    const acquiredInObservedWindow = (
      latest.knownClassicTechnologies - first.knownClassicTechnologies
    )
    return {
      key: seatId,
      label: factionDisplayLabel(builder.player),
      color: builder.player.player_color ?? null,
      values,
      selected: null,
      latest,
      acquiredInObservedWindow,
      observedTurnSpan,
      pacePerTenTurns: observedTurnSpan > 0
        ? (acquiredInObservedWindow / observedTurnSpan) * 10
        : null,
    }
  })

  return selectTechnologyProgressTurn({
    status: 'ready',
    catalogTotal: catalogIds.size,
    selectedTurn: null,
    minTurn,
    maxTurn,
    series,
  }, selectedTurn)
}

const CHART = {
  width: 700,
  height: 250,
  left: 54,
  right: 682,
  top: 18,
  bottom: 214,
}

function xPosition(turn: number, minTurn: number, maxTurn: number) {
  if (maxTurn === minTurn) return (CHART.left + CHART.right) / 2
  return CHART.left + (
    (turn - minTurn) / (maxTurn - minTurn)
  ) * (CHART.right - CHART.left)
}

function yPosition(value: number, catalogTotal: number) {
  return CHART.bottom - (
    value / Math.max(1, catalogTotal)
  ) * (CHART.bottom - CHART.top)
}

function polylinePoints(
  values: readonly TechnologyProgressPoint[],
  minTurn: number,
  maxTurn: number,
  catalogTotal: number,
) {
  return values.map((point) => [
    xPosition(point.turn, minTurn, maxTurn).toFixed(1),
    yPosition(point.knownClassicTechnologies, catalogTotal).toFixed(1),
  ].join(',')).join(' ')
}

function EmptyTechnologyProgress({ status }: { status: TechnologyProgressStatus }) {
  const copy = {
    'catalog-unavailable': {
      title: 'Technology catalog unavailable',
      detail: 'Classic technology counts cannot be verified without the game catalog.',
    },
    'snapshots-unavailable': {
      title: 'Waiting for replay snapshots',
      detail: 'Technology progression will appear after the first saved turn.',
    },
    'controllers-unavailable': {
      title: 'No scored controller telemetry',
      detail: 'Replay snapshots do not yet identify a scored controller to compare.',
    },
    ready: {
      title: '',
      detail: '',
    },
  }[status]
  return (
    <div className="empty-state small-empty">
      <strong>{copy.title}</strong>
      <span>{copy.detail}</span>
    </div>
  )
}

export function TechnologyProgressChart({
  catalog,
  snapshots,
  selectedTurn,
}: TechnologyProgressChartProps) {
  const baseModel = useMemo(
    () => buildTechnologyProgressSeries(snapshots, catalog),
    [snapshots, catalog],
  )
  const model = useMemo(
    () => selectTechnologyProgressTurn(baseModel, selectedTurn),
    [baseModel, selectedTurn],
  )
  const selectedLabel = model.selectedTurn == null ? '—' : `T${model.selectedTurn}`
  const palette = useDisplayPalette()

  return (
    <article className="panel metric-chart technology-progress-chart">
      <div className="panel-heading compact-heading">
        <div>
          <p className="eyebrow">Knowledge comparison</p>
          <h3>Classic technology progression</h3>
        </div>
        <strong className="text-[var(--color-muted)] font-bold text-[13px] leading-none font-readout">Selected {selectedLabel}</strong>
      </div>

      {model.status !== 'ready' ? <EmptyTechnologyProgress status={model.status} /> : (
        <>
          <svg
            aria-label="Cumulative classic technologies known by turn"
            className="line-chart technology-progress-svg"
            role="img"
            viewBox={`0 0 ${CHART.width} ${CHART.height}`}
          >
            <title>Classic technology progression by controller</title>
            <desc>
              Each line counts unique catalog technology identifiers observed through each turn.
              Future technology levels are excluded.
            </desc>
            {[0, 0.5, 1].map((ratio) => {
              const value = Math.round(model.catalogTotal * ratio)
              const y = yPosition(value, model.catalogTotal)
              return (
                <g key={ratio}>
                  <line x1={CHART.left} x2={CHART.right} y1={y} y2={y} />
                  <text fill="currentColor" fontSize="10" textAnchor="end" x={CHART.left - 8} y={y + 3}>
                    {value}
                  </text>
                </g>
              )
            })}
            <text fill="currentColor" fontSize="10" textAnchor="middle" x="15" y="118" transform="rotate(-90 15 118)">
              Classic technologies known
            </text>
            <text fill="currentColor" fontSize="10" textAnchor="start" x={CHART.left} y="238">
              Turn {model.minTurn}
            </text>
            <text fill="currentColor" fontSize="10" textAnchor="end" x={CHART.right} y="238">
              Turn {model.maxTurn}
            </text>
            {model.selectedTurn != null && model.selectedTurn >= model.minTurn! && model.selectedTurn <= model.maxTurn! ? (
              <line
                className="technology-selected-turn"
                stroke="currentColor"
                strokeDasharray="2 5"
                x1={xPosition(model.selectedTurn, model.minTurn!, model.maxTurn!)}
                x2={xPosition(model.selectedTurn, model.minTurn!, model.maxTurn!)}
                y1={CHART.top}
                y2={CHART.bottom}
              />
            ) : null}
            {model.series.map((item) => {
              const paint = displayPlayerColor(item.color, palette) ?? 'var(--color-muted)'
              return (
                <g key={item.key}>
                  <title>
                    {item.label}: {item.latest.knownClassicTechnologies} of {model.catalogTotal}
                    {' '}classic technologies at turn {item.latest.turn}
                  </title>
                  <polyline
                    fill="none"
                    points={polylinePoints(item.values, model.minTurn!, model.maxTurn!, model.catalogTotal)}
                    stroke={paint}
                    strokeLinecap="square"
                    strokeLinejoin="miter"
                    strokeWidth="1.75"
                    vectorEffect="non-scaling-stroke"
                  />
                  <rect
                    fill={paint}
                    height={6}
                    width={6}
                    x={xPosition(item.latest.turn, model.minTurn!, model.maxTurn!) - 3}
                    y={yPosition(item.latest.knownClassicTechnologies, model.catalogTotal) - 3}
                  />
                  {item.selected ? (
                    <circle
                      cx={xPosition(item.selected.turn, model.minTurn!, model.maxTurn!)}
                      cy={yPosition(item.selected.knownClassicTechnologies, model.catalogTotal)}
                      fill={paint}
                      r="5"
                      stroke="currentColor"
                      strokeWidth="1.5"
                    />
                  ) : null}
                </g>
              )
            })}
          </svg>

          <div className="technology-progress-summary">
            {model.series.map((item) => (
              <article className="technology-progress-controller" key={item.key}>
                <div>
                  <ColorMark color={item.color} label={item.label} size="sm" />
                  <strong>{item.label}</strong>
                </div>
                <dl>
                  <div>
                    <dt>Selected {selectedLabel}</dt>
                    <dd>{item.selected
                      ? `${item.selected.knownClassicTechnologies} / ${model.catalogTotal} known`
                      : 'No replay state'}</dd>
                  </div>
                  <div>
                    <dt>Latest T{item.latest.turn}</dt>
                    <dd>{item.latest.knownClassicTechnologies} / {model.catalogTotal} known</dd>
                  </div>
                  <div>
                    <dt>Observed acquisition pace</dt>
                    <dd>{item.pacePerTenTurns == null
                      ? 'Not enough turn span'
                      : `${item.pacePerTenTurns.toFixed(1)} new / 10 turns`}</dd>
                  </div>
                  <div>
                    <dt>Future techs (outside curve)</dt>
                    <dd>{item.selected?.futureTechs ?? '—'} selected · {item.latest.futureTechs} latest</dd>
                  </div>
                </dl>
              </article>
            ))}
          </div>
          <p className="empty-copy technology-progress-note">
            Curves count unique catalog technology IDs observed through each replay turn.
            Future techs are reported separately and do not affect classic completion.
          </p>
        </>
      )}
    </article>
  )
}
