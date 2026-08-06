import { displayPlayerColor } from '../display-color'

interface ColorMarkProps {
  color?: string | null
  label: string
  size?: 'sm' | 'md' | 'lg'
}

const dimensions = { sm: 12, md: 16, lg: 22 }

export function ColorMark({ color, label, size = 'md' }: ColorMarkProps) {
  const dimension = dimensions[size]
  const displayed = displayPlayerColor(color)
  return (
    <svg
      aria-label={`${label} color ${displayed ?? 'unknown'}`}
      className="flex-none overflow-visible"
      height={dimension}
      role="img"
      viewBox="0 0 20 20"
      width={dimension}
    >
      <circle cx="10" cy="10" fill={displayed ?? '#65727c'} r="8" />
      <circle cx="10" cy="10" fill="none" r="8" stroke="#f4f7f8" strokeOpacity="0.62" />
    </svg>
  )
}
