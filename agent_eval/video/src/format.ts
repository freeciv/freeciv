export function formatYear(year: number): string {
  if (year < 0) return `${Math.abs(year)} BC`
  if (year === 0) return '1 AD'
  return `AD ${year}`
}

export function formatCount(value: number): string {
  return Math.round(value).toLocaleString('en-US')
}

/** "1 city", "2 cities" — a count that names its own unit. */
export function countNoun(value: number, singular: string, plural: string): string {
  return `${formatCount(value)} ${value === 1 ? singular : plural}`
}

export function formatDuration(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds <= 0) return 'unknown'
  const hours = Math.floor(seconds / 3600)
  const minutes = Math.round((seconds % 3600) / 60)
  return hours > 0 ? `${hours}h ${minutes}m` : `${minutes}m`
}
