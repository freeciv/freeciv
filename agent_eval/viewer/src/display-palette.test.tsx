import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { ColorMark } from './components/ColorMark'
import { DisplayPaletteProvider } from './display-palette'

const AGENT_BLUE = '#0067A5'
const PIRATE_GOLDENROD = '#B8860B'
const NATIVE_ORANGE = '#F38400'
const PERIWINKLE = '#A78BFA'
const MINT = '#5cf0d0'

const lnjFactions = [
  { playerId: 0, color: AGENT_BLUE },
  { playerId: 1, color: NATIVE_ORANGE },
  { playerId: 2, color: PIRATE_GOLDENROD },
  { playerId: 3, color: '#991199' },
]

// The label is deliberately not the hex: it would put the recorded color back
// into the markup and hide a swatch that failed to remap.
function swatches(recorded: string[], withPalette: boolean): string {
  const marks = recorded.map((color, index) => (
    <ColorMark color={color} key={color} label={`faction ${index}`} />
  ))
  return renderToStaticMarkup(
    withPalette
      ? <DisplayPaletteProvider factions={lnjFactions}>{marks}</DisplayPaletteProvider>
      : <>{marks}</>,
  )
}

describe('display palette context', () => {
  it('paints a faction the clearance rule moved, not just the pinned seat', () => {
    const markup = swatches([AGENT_BLUE, PIRATE_GOLDENROD, NATIVE_ORANGE], true)

    expect(markup).toContain(PERIWINKLE)
    expect(markup).toContain(MINT)
    expect(markup).toContain(NATIVE_ORANGE)
    expect(markup).not.toContain(AGENT_BLUE)
    expect(markup).not.toContain(PIRATE_GOLDENROD)
  })

  it('labels the swatch with the color it actually painted', () => {
    expect(swatches([PIRATE_GOLDENROD], true)).toContain(`color ${MINT}`)
  })

  it('still honors the pinned seat with no provider above it', () => {
    const markup = swatches([AGENT_BLUE, PIRATE_GOLDENROD], false)

    expect(markup).toContain(PERIWINKLE)
    // Clearance needs the whole faction set, so an isolated swatch cannot know
    // the goldenrod is unreadable. It keeps the recorded color rather than
    // guessing at a substitute the rest of the page would not agree with.
    expect(markup).toContain(PIRATE_GOLDENROD)
  })
})
