import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import type { ReplayPlayer, ReplaySnapshot, Technology } from '../types'
import {
  buildTechnologyProgressSeries,
  selectTechnologyProgressTurn,
  TechnologyProgressChart,
} from './TechnologyProgressChart'

const catalog: Technology[] = [
  { id: 1, rule_name: 'Alphabet', name: 'Alphabet', cost_base: 10 },
  { id: 2, rule_name: 'Writing', name: 'Writing', cost_base: 20 },
  { id: 3, rule_name: 'Map_Making', name: 'Map Making', cost_base: 30 },
]

function player(
  seatId: string,
  place: number | null,
  known: number[],
  overrides: Partial<ReplayPlayer> = {},
): ReplayPlayer {
  return {
    seat_id: seatId,
    place,
    player_id: place ?? 9,
    player_name: seatId,
    player_color: seatId === 'agent' ? '#0067A5' : '#F38400',
    controller_label: seatId === 'agent' ? 'pi-gpt-5.6-sol' : 'Freeciv Classic AI',
    controller_type: place == null ? 'dynamic' : seatId === 'agent' ? 'external' : 'native',
    nation: 'Romans',
    government: 'Republic',
    alive: true,
    score: 10,
    cities: 1,
    units: 1,
    gold: 0,
    culture: 0,
    known_tech_ids: known,
    gained_tech_ids: [],
    lost_tech_ids: [],
    research: { tech_id: null, name: '', bulbs: 0, cost: 0 },
    future_techs: 0,
    scored: place != null,
    ...overrides,
  }
}

function snapshot(turn: number, players: ReplayPlayer[]): ReplaySnapshot {
  return { turn, year: -4000 + turn * 50, players }
}

const snapshots = [
  snapshot(3, [
    player('agent', 1, [1], { lost_tech_ids: [2], future_techs: 2 }),
    player('native', 2, [1, 2, 3]),
    player('pirates', null, [1, 2, 3], { scored: false }),
  ]),
  snapshot(1, [
    player('agent', 1, [1, 99]),
    player('native', 2, [1]),
  ]),
  snapshot(2, [
    player('agent', 1, [1, 2], { gained_tech_ids: [2] }),
    player('native', 2, [1, 2]),
  ]),
]

describe('technology progression model', () => {
  it('builds catalog-backed monotonic curves for scored controllers only', () => {
    const model = buildTechnologyProgressSeries(snapshots, catalog, 2)

    expect(model).toMatchObject({
      status: 'ready',
      catalogTotal: 3,
      selectedTurn: 2,
      minTurn: 1,
      maxTurn: 3,
    })
    expect(model.series.map((series) => [series.key, series.color])).toEqual([
      ['agent', '#0067A5'],
      ['native', '#F38400'],
    ])
    expect(model.series.map((series) => series.label)).toEqual([
      'pi-gpt-5.6-sol: Romans',
      'Romans (CPU)',
    ])
    expect(model.series[0].values).toEqual([
      { turn: 1, knownClassicTechnologies: 1, futureTechs: 0 },
      { turn: 2, knownClassicTechnologies: 2, futureTechs: 0 },
      { turn: 3, knownClassicTechnologies: 2, futureTechs: 2 },
    ])
    expect(model.series[0]).toMatchObject({
      selected: { turn: 2, knownClassicTechnologies: 2 },
      latest: { turn: 3, knownClassicTechnologies: 2, futureTechs: 2 },
      acquiredInObservedWindow: 1,
      observedTurnSpan: 2,
      pacePerTenTurns: 5,
    })
  })

  it('deduplicates the catalog denominator and uses state at or before the selected turn', () => {
    const duplicateCatalog = [...catalog, { ...catalog[0] }]
    const model = buildTechnologyProgressSeries(snapshots, duplicateCatalog, 1.5)

    expect(model.catalogTotal).toBe(3)
    expect(model.selectedTurn).toBe(1)
    expect(model.series[0].selected?.turn).toBe(1)
  })

  it('qualifies multiple native controllers by nation', () => {
    const model = buildTechnologyProgressSeries([
      snapshot(1, [
        player('native-danish', 1, [1], { nation: 'Danish' }),
        player('native-persian', 2, [1], { nation: 'Persian' }),
      ]),
    ], catalog)

    expect(model.series.map((series) => series.label)).toEqual([
      'Danish (CPU)',
      'Persian (CPU)',
    ])
  })

  it('selects sparse controller telemetry without rebuilding its curve', () => {
    const base = buildTechnologyProgressSeries([
      snapshot(2, [player('agent', 1, [1])]),
      snapshot(5, [player('agent', 1, [1, 2])]),
    ], catalog)

    expect(selectTechnologyProgressTurn(base, 1).series[0].selected).toBeNull()
    expect(selectTechnologyProgressTurn(base, 4).series[0].selected?.turn).toBe(2)
    expect(selectTechnologyProgressTurn(base, 99).series[0].selected?.turn).toBe(5)
  })

  it('marks an absent recorded player color explicitly instead of calling gray exact', () => {
    const model = buildTechnologyProgressSeries([
      snapshot(1, [player('agent', 1, [1], { player_color: null })]),
    ], catalog)

    expect(model.series[0].color).toBeNull()
    const markup = renderToStaticMarkup(
      <TechnologyProgressChart catalog={catalog} snapshots={[
        snapshot(1, [player('agent', 1, [1], { player_color: null })]),
      ]} />,
    )
    expect(markup).toContain('color unknown')
  })

  it('reports honest unavailable states', () => {
    expect(buildTechnologyProgressSeries(snapshots, undefined).status).toBe('catalog-unavailable')
    expect(buildTechnologyProgressSeries([], catalog).status).toBe('snapshots-unavailable')
    expect(buildTechnologyProgressSeries([
      snapshot(1, [player('pirates', null, [1], { scored: false })]),
    ], catalog).status).toBe('controllers-unavailable')
  })
})

describe('TechnologyProgressChart', () => {
  it('renders selected and latest completion, pace, future techs, and display colors', () => {
    const markup = renderToStaticMarkup(
      <TechnologyProgressChart catalog={catalog} selectedTurn={2} snapshots={snapshots} />,
    )

    expect(markup).toContain('Cumulative classic technologies known by turn')
    expect(markup).toContain('pi-gpt-5.6-sol')
    expect(markup).toContain('Romans (CPU)')
    // The recorded '#0067A5' is remapped for display only; the model keeps it.
    expect(markup).toContain('#A78BFA')
    expect(markup).not.toContain('#0067A5')
    expect(markup).toContain('#F38400')
    expect(markup).toContain('2 / 3 known')
    expect(markup).toContain('5.0 new / 10 turns')
    expect(markup).toContain('Future techs (outside curve)')
    expect(markup).toContain('2 latest')
    expect(markup.toLowerCase()).not.toContain('science')
  })

  it('does not draw an unverifiable curve without a catalog', () => {
    const markup = renderToStaticMarkup(
      <TechnologyProgressChart snapshots={snapshots} />,
    )

    expect(markup).toContain('Technology catalog unavailable')
    expect(markup).not.toContain('<polyline')
  })
})
