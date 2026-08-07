import { displayPlayerColor } from '../display-color'
import { useDisplayPalette } from '../display-palette'

interface ColorMarkProps {
  color?: string | null
  label: string
  size?: 'sm' | 'md' | 'lg'
}

const dimensions = { sm: 12, md: 16, lg: 22 }

export function ColorMark({ color, label, size = 'md' }: ColorMarkProps) {
  const dimension = dimensions[size]
  const displayed = displayPlayerColor(color, useDisplayPalette())
  return (
    <svg
      aria-label={`${label} color ${displayed ?? 'unknown'}`}
      className="flex-none overflow-visible"
      height={dimension}
      role="img"
      viewBox="0 0 20 20"
      width={dimension}
    >
      <circle cx="10" cy="10" fill={displayed ?? 'var(--color-muted)'} r="8" />
      <circle cx="10" cy="10" fill="none" r="8" stroke="var(--color-ink)" strokeOpacity="0.62" />
    </svg>
  )
}
