import { useEffect, useMemo, useRef } from 'react'
import { buildPalette, drawBoard } from '../board/draw'
import type { TurnState } from '../dataset/film'
import { buildBoardLayout, fitBoard } from '../dataset/geometry'
import type { DatasetMeta } from '../dataset/schema'

interface BoardCanvasProps {
  readonly meta: DatasetMeta
  readonly colorByPlayer: ReadonlyMap<number, string>
  readonly turn: TurnState
  readonly width: number
  readonly height: number
  /** Backing-store multiplier. 2 keeps 8px hexes crisp at 1080p. */
  readonly superSample: number
  readonly reveal: number
  readonly showLabels: boolean
}

export function BoardCanvas({
  meta, colorByPlayer, turn, width, height, superSample, reveal, showLabels,
}: BoardCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const layout = useMemo(
    () => buildBoardLayout(meta.width, meta.height), [meta.height, meta.width],
  )
  const palette = useMemo(() => buildPalette(meta, colorByPlayer), [colorByPlayer, meta])
  const fit = useMemo(
    () => fitBoard(layout, width * superSample, height * superSample),
    [height, layout, superSample, width],
  )

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const context = canvas.getContext('2d')
    if (!context) return
    drawBoard(context, canvas.width, canvas.height, {
      fit, layout, palette, reveal, showLabels, turn, uiScale: superSample,
    })
  }, [fit, layout, palette, reveal, showLabels, superSample, turn])

  return (
    <canvas
      height={Math.round(height * superSample)}
      ref={canvasRef}
      style={{ display: 'block', height, width }}
      width={Math.round(width * superSample)}
    />
  )
}
