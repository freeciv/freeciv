import { describe, expect, it } from 'vitest'
import { displayPlayerColor } from './display-color'

describe('displayPlayerColor', () => {
  it('remaps the agent seat blue that reads as water to periwinkle', () => {
    expect(displayPlayerColor('#0067A5')).toBe('#A78BFA')
  })

  it('matches the remap table without regard to case or surrounding space', () => {
    expect(displayPlayerColor('#0067a5')).toBe('#A78BFA')
    expect(displayPlayerColor('#0067A5')).toBe('#A78BFA')
    expect(displayPlayerColor(' #0067a5 ')).toBe('#A78BFA')
  })

  it('passes an unmapped color through byte for byte', () => {
    expect(displayPlayerColor('#F38400')).toBe('#F38400')
    expect(displayPlayerColor('#ff1493')).toBe('#ff1493')
    expect(displayPlayerColor('#FFD700')).toBe('#FFD700')
  })

  it('reports an absent color as absent instead of inventing one', () => {
    expect(displayPlayerColor(null)).toBeNull()
    expect(displayPlayerColor(undefined)).toBeNull()
    expect(displayPlayerColor('')).toBeNull()
  })

  it('is idempotent, so a display color survives a second pass unchanged', () => {
    expect(displayPlayerColor(displayPlayerColor('#0067A5'))).toBe('#A78BFA')
  })
})
