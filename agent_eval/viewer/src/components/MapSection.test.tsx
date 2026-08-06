import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { MAP_REGION_ID, MapSection } from './MapSection'

function Board(): never {
  throw new Error('the board renderer must not mount while the map is collapsed')
}

const props = {
  board: <Board />,
  legend: <aside className="panel faction-panel">legend</aside>,
  live: true,
  onToggle: () => undefined,
  playback: <div className="playback-bar">transport</div>,
  turn: 12,
}

describe('deferred strategic map section', () => {
  it('keeps the board and legend unmounted until the section is expanded', () => {
    const markup = renderToStaticMarkup(<MapSection {...props} expanded={false} />)
    expect(markup).toContain('Show map')
    expect(markup).toContain('aria-expanded="false"')
    expect(markup).toContain(`aria-controls="${MAP_REGION_ID}"`)
    expect(markup).toContain('stays unloaded until you open it')
    expect(markup).not.toContain('faction-panel')
  })

  it('keeps turn transport available while the map stays collapsed', () => {
    const markup = renderToStaticMarkup(<MapSection {...props} expanded={false} />)
    expect(markup).toContain('playback-bar')
    expect(markup).toContain('World state at turn 12')
  })

  it('mounts the board and legend once expanded', () => {
    const markup = renderToStaticMarkup(
      <MapSection {...props} board={<div className="strategic-map">board</div>} expanded />,
    )
    expect(markup).toContain('Hide map')
    expect(markup).toContain('aria-expanded="true"')
    expect(markup).toContain('strategic-map')
    expect(markup).toContain('faction-panel')
    expect(markup).toContain('playback-bar')
  })

  it('exposes the disclosure as a real button, not a tab', () => {
    const markup = renderToStaticMarkup(<MapSection {...props} expanded={false} />)
    expect(markup).toContain('<button')
    expect(markup).toContain('type="button"')
    expect(markup).not.toContain('role="tab"')
  })
})
