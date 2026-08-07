import { useEffect, useRef } from 'react'

/**
 * The terminal wash: a low-contrast plasma, pixelated into chunky rounded
 * cells, anchored to the bottom edge and dissolving upward.
 *
 * It exists so the light surface has a floor. A dense telemetry page on flat
 * grey reads as unfinished at the bottom; this gives the page somewhere to
 * end without drawing a line under it.
 *
 * Motion is not decoration here. The reference this is modelled on runs the
 * plasma slowly at rest and fast while the terminal is streaming, so movement
 * means "something is happening" -- and that is the only reason it moves here
 * too: a live match breathes, an archived one is a still. A background is seen
 * every second the page is open, so if it animated unconditionally it would be
 * noise by the frequency rule. Reduced-motion users always get the still.
 */

const CELL = 44
const GAP_MIN = 0.16

function plasma(x: number, y: number, time: number): number {
  // Frequencies are deliberately incommensurate: with tidy multiples the cells
  // fall into visible stripes and the wash reads as a loading skeleton rather
  // than as texture.
  const raw =
    Math.sin(x * 11.3 + time)
    + Math.sin(y * 5.1 - time * 0.7)
    + Math.sin((x * 7.9 + y * 4.3) + time * 0.5)
    + Math.sin(Math.hypot(x - 0.35, y - 1) * 13.7 - time)
  // -4..4 -> 0..1, then contrast 1.6 around the midpoint.
  const unit = (raw + 4) / 8
  return Math.min(1, Math.max(0, (unit - 0.5) * 1.6 + 0.5))
}

export function PlasmaWash({ live = false }: { live?: boolean }) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const context = canvas.getContext('2d')
    if (!context) return

    const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches
    const animated = live && !reduceMotion
    const wash = getComputedStyle(document.documentElement)
      .getPropertyValue('--color-wash').trim() || '#5d5963'

    let frame = 0
    let stop = false

    function paint(time: number) {
      const canvas = canvasRef.current
      if (!canvas || !context) return
      const ratio = Math.min(2, window.devicePixelRatio || 1)
      const width = canvas.clientWidth
      const height = canvas.clientHeight
      if (canvas.width !== width * ratio || canvas.height !== height * ratio) {
        canvas.width = width * ratio
        canvas.height = height * ratio
      }
      context.setTransform(ratio, 0, 0, ratio, 0, 0)
      context.clearRect(0, 0, width, height)
      context.fillStyle = wash

      const columns = Math.ceil(width / CELL)
      const rows = Math.ceil(height / CELL)
      for (let row = 0; row < rows; row += 1) {
        // 0 at the bottom row, 1 at the top: the wash dissolves upward.
        const depth = 1 - (row + 0.5) / rows
        const gap = GAP_MIN + (1 - GAP_MIN) * depth ** 0.55
        const size = CELL * (1 - gap)
        if (size < 1) continue
        for (let column = 0; column < columns; column += 1) {
          const value = plasma((column + 0.5) / columns, (row + 0.5) / rows, time)
          context.globalAlpha = value * 0.14 * (1 - depth ** 1.4)
          const x = column * CELL + (CELL - size) / 2
          const y = row * CELL + (CELL - size) / 2
          context.beginPath()
          context.roundRect(x, y, size, size, size * 0.2)
          context.fill()
        }
      }
      context.globalAlpha = 1
    }

    function tick(now: number) {
      if (stop) return
      paint(now / 1000 * 0.5)
      frame = requestAnimationFrame(tick)
    }

    if (animated) frame = requestAnimationFrame(tick)
    else paint(0)

    const onResize = () => { if (!animated) paint(0) }
    window.addEventListener('resize', onResize)
    return () => {
      stop = true
      cancelAnimationFrame(frame)
      window.removeEventListener('resize', onResize)
    }
  }, [live])

  return <canvas aria-hidden="true" className="plasma-wash" ref={canvasRef} />
}
