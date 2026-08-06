import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { EventLog, EventLogFootnote } from './EventLog'
import { displayPlayerColor } from '../display-color'
import type { GameEvent } from '../types'

const events: GameEvent[] = [
  {
    turn: 2,
    kind: 'city_founded',
    summary: 'pi-gpt-5.6-sol founded London',
    actors: ['place-1'],
    weight: 18,
    data: { cities: ['London'] },
  },
  {
    turn: 181,
    kind: 'war_declared',
    summary: 'pi-gpt-5.6-sol met In-game Deity AI — no treaty, at war',
    actors: ['place-1', 'place-2'],
    weight: 80,
    data: {},
  },
  {
    turn: 349,
    kind: 'city_captured',
    summary: 'In-game Deity AI captured the capital London from pi-gpt-5.6-sol',
    actors: ['place-2', 'place-1'],
    weight: 66,
    data: { cities: ['London'] },
  },
]

const colors = new Map([['place-1', '#0067A5'], ['place-2', '#F38400']])

function render(selectedTurn: number) {
  return renderToStaticMarkup(
    <EventLog
      colors={colors}
      events={events}
      onSelectTurn={() => undefined}
      selectedTurn={selectedTurn}
    />,
  )
}

describe('turn event log', () => {
  it('turn-stamps every event and renders its summary', () => {
    const markup = render(500)
    expect(markup).toContain('T2')
    expect(markup).toContain('T181')
    expect(markup).toContain('T349')
    expect(markup).toContain('captured the capital London')
    expect(markup).toContain('Captured')
  })

  it('makes each row a real button so a click can move the scrubber', () => {
    const markup = render(500)
    // Three event rows plus the three density stops.
    expect(markup.match(/<button/g)).toHaveLength(6)
    expect(markup).toContain('type="button"')
  })

  it('opens on every event, with the density stops offered', () => {
    const markup = render(500)
    expect(markup).toContain('founded London')
    expect(markup).toContain('Everything')
    expect(markup).toContain('Key moments')
    expect(markup).toContain('Major')
    expect(markup).toContain('3 of 3')
  })

  it('ranks each row visually by its weight', () => {
    const markup = render(500)
    // The war and the captured capital read as major; the founding does not.
    expect(markup.match(/text-\[12px\] font-semibold/g)).toHaveLength(2)
    expect(markup).toContain('text-[10px]')
    expect(markup).toContain('title="Weight 80"')
  })

  it('marks the event the scrubber sits on and dims what has not played', () => {
    const markup = render(181)
    expect(markup).toContain('aria-current="true"')
    expect(markup.match(/aria-current="true"/g)).toHaveLength(1)
    expect(markup).toContain('opacity-45')
    const current = markup.slice(markup.indexOf('aria-current="true"'))
    expect(current).toContain('no treaty, at war')
  })

  it('paints each row through the display substitution, not the raw color', () => {
    const markup = render(500)
    // The agent seat's recorded #0067A5 is pinned away from the ocean palette,
    // exactly as the map and the film paint it.
    expect(markup).toContain(`background:${displayPlayerColor('#0067A5')}`)
    expect(markup).not.toContain('#0067A5')
    expect(markup).toContain(`background:${displayPlayerColor('#F38400')}`)
  })

  it('marks nothing current before the first event', () => {
    expect(render(1)).not.toContain('aria-current')
  })
})

describe('capped log footnote', () => {
  it('says the log was capped and what is missing', () => {
    const markup = renderToStaticMarkup(
      <EventLogFootnote omitted={{ city_founded: 40 }} />,
    )
    expect(markup).toContain('capped')
    expect(markup).toContain('40 founded')
  })

  it('renders nothing when the whole log fit', () => {
    expect(renderToStaticMarkup(<EventLogFootnote omitted={{}} />)).toBe('')
  })
})
