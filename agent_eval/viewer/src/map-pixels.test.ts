import { describe, expect, it } from 'vitest'
import { transformAtlasPixels } from './map-pixels'

describe('transformAtlasPixels', () => {
  it('makes a near-black source background transparent', () => {
    expect([...transformAtlasPixels(
      new Uint8ClampedArray([8, 10, 12, 255]),
      [],
    )]).toEqual([8, 10, 12, 0])
  })

  it('preserves exact protected faction colors including alpha', () => {
    expect([...transformAtlasPixels(
      new Uint8ClampedArray([0, 103, 165, 137]),
      ['#0067A5'],
    )]).toEqual([0, 103, 165, 137])
  })

  it('transforms non-protected source colors deterministically', () => {
    const source = new Uint8ClampedArray([0, 72, 164, 255])
    const first = transformAtlasPixels(source, [])
    const second = transformAtlasPixels(source, [])

    expect([...first]).toEqual([11, 75, 133, 255])
    expect(first).toEqual(second)
    expect([...source]).toEqual([0, 72, 164, 255])
  })

  it('preserves alpha while enhancing visible non-protected pixels', () => {
    expect(transformAtlasPixels(
      new Uint8ClampedArray([20, 150, 34, 91]),
      [],
    )[3]).toBe(91)
  })
})
