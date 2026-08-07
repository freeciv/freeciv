import { useCurrentFrame, useVideoConfig } from 'remotion'
import { useMemo } from 'react'
import { SHELL } from '../theme'

/**
 * The arena's ground: a slow sine band, dithered to two tones.
 *
 * This reproduces the background of the Composio `/cli` page, which is a
 * `SolidColor` white floor with a `SineWave` painted over it through a
 * `Dither` node -- so what you see is not a gradient but a bitmap: every
 * pixel is one of exactly two colours, and the impression of a soft wave
 * comes entirely from the density of the ordered dither.
 *
 * It is reimplemented rather than imported, for two reasons. The library's
 * clock is an internal accumulator fed by `performance.now()` deltas with no
 * external time input, which is unusable in a renderer that must produce
 * frame N deterministically and out of order; and its licence forbids
 * redistributing the package's own code. The maths below is a closed form --
 * `f(x, y, frame)` with no feedback, no noise texture and no accumulated
 * state -- so a Remotion render is exactly reproducible and can be resumed,
 * parallelised, or re-rendered frame by frame with identical output.
 *
 * Painted on the CPU into an ImageData at block resolution and scaled up with
 * smoothing off. `step()` output is binary by design: any interpolation
 * anywhere turns the dither into flat grey, which is the one way to get this
 * wrong.
 */

/** Wave geometry, matching the `/cli` SineWave props. */
const ANGLE_DEGREES = 162
const FREQUENCY = 0.5
const AMPLITUDE = 0.1
/** Radians per second. Negative, so the band drifts against the reading eye. */
const PHASE_RATE = -0.4
/** `thickness: 0, softness: 1` collapses the smoothstep edges to -0.5..0.5. */
const EDGE_LOW = -0.5
const EDGE_HIGH = 0.5

/** Dither, matching the `/cli` Dither props: bayer8, threshold .59, spread .8. */
const THRESHOLD = 0.59
const SPREAD = 0.8

/**
 * The 8x8 Bayer matrix built by the recurrence the source uses, not a textbook
 * table: this nesting puts the coarsest term last, and substituting a
 * conventionally-ordered matrix visibly changes the grain.
 */
function buildBayer8(): Float64Array {
  const quad = (x: number, y: number): number => y * 3 + x * 2 - x * y * 4
  const table = new Float64Array(64)
  for (let y = 0; y < 8; y += 1) {
    for (let x = 0; x < 8; x += 1) {
      table[y * 8 + x] = (
        quad(x % 2, y % 2) * 16
        + quad(Math.floor((x % 4) / 2), Math.floor((y % 4) / 2)) * 4
        + quad(Math.floor(x / 4), Math.floor(y / 4))
      ) / 64
    }
  }
  return table
}

function smoothstep(low: number, high: number, value: number): number {
  const t = Math.min(1, Math.max(0, (value - low) / (high - low)))
  return t * t * (3 - 2 * t)
}

function rgbBytes(color: string): [number, number, number] {
  const hex = color.replace('#', '')
  const value = Number.parseInt(hex.length === 3
    ? hex.split('').map((c) => c + c).join('')
    : hex.slice(0, 6), 16)
  return [(value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff]
}

export interface DitherWaveProps {
  /** Edge of one dither cell, in output pixels. */
  readonly pixelSize?: number
  /** The "off" tone: the paper the wave is printed on. */
  readonly paper?: string
  /** The "on" tone: the ink. */
  readonly ink?: string
}

export function DitherWave({
  pixelSize = 3,
  paper = SHELL.page,
  ink = SHELL.ditherInk,
}: DitherWaveProps) {
  const frame = useCurrentFrame()
  const { fps, width, height } = useVideoConfig()

  const bayer = useMemo(buildBayer8, [])

  const dataUrl = useMemo(() => {
    const columns = Math.ceil(width / pixelSize)
    const rows = Math.ceil(height / pixelSize)
    const canvas = document.createElement('canvas')
    canvas.width = columns
    canvas.height = rows
    const context = canvas.getContext('2d')
    if (!context) return null

    const image = context.createImageData(columns, rows)
    const pixels = image.data
    const [paperR, paperG, paperB] = rgbBytes(paper)
    const [inkR, inkG, inkB] = rgbBytes(ink)

    const phase = PHASE_RATE * (frame / fps)
    const angle = (ANGLE_DEGREES * Math.PI) / 180
    const cosAngle = Math.cos(angle)
    const sinAngle = Math.sin(angle)
    const aspect = width / height

    for (let row = 0; row < rows; row += 1) {
      // Sampled at the cell centre, not the pixel centre -- the wave is read
      // once per dither cell, which is what makes the pattern a bitmap rather
      // than a smooth field that happens to be quantised.
      const centerY = row * pixelSize + pixelSize / 2
      // Screen UV is y-up, so the row index is flipped.
      const v = 1 - centerY / height
      const cy = v - 0.5
      for (let column = 0; column < columns; column += 1) {
        const centerX = column * pixelSize + pixelSize / 2
        const cx = (centerX / width) * aspect - 0.5 * aspect

        const rotatedX = cx * cosAngle - cy * sinAngle
        const rotatedY = cx * sinAngle + cy * cosAngle
        const wave = Math.sin(rotatedX * FREQUENCY * Math.PI * 2 + phase) * AMPLITUDE
        const distance = Math.abs(rotatedY - wave)
        // The band never reaches full opacity: with these edges the mask peaks
        // at 0.5 on the centreline, which is why the wave reads as a drift in
        // dither density rather than as a drawn stripe.
        const luminance = 1 - smoothstep(EDGE_LOW, EDGE_HIGH, distance)

        const threshold = 0.5 + (bayer[(row % 8) * 8 + (column % 8)]! - 0.5) * SPREAD
        const on = luminance + THRESHOLD - 0.5 >= threshold

        const offset = (row * columns + column) * 4
        pixels[offset] = on ? inkR : paperR
        pixels[offset + 1] = on ? inkG : paperG
        pixels[offset + 2] = on ? inkB : paperB
        pixels[offset + 3] = 255
      }
    }
    context.putImageData(image, 0, 0)
    return canvas.toDataURL()
  }, [bayer, frame, fps, width, height, pixelSize, paper, ink])

  return (
    <div className="absolute inset-0" style={{ background: paper }}>
      {dataUrl && (
        <img
          alt=""
          className="h-full w-full"
          src={dataUrl}
          style={{ imageRendering: 'pixelated' }}
        />
      )}
    </div>
  )
}
