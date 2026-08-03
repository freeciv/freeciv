export class LatestRequestGate {
  private generation = 0

  begin(): number {
    this.generation += 1
    return this.generation
  }

  isCurrent(generation: number): boolean {
    return generation === this.generation
  }
}

export class LruCache<T> {
  private readonly values = new Map<string, T>()

  constructor(private readonly capacity: number) {
    if (!Number.isInteger(capacity) || capacity < 1) throw new Error('capacity must be positive')
  }

  get(key: string): T | undefined {
    const value = this.values.get(key)
    if (value === undefined) return undefined
    this.values.delete(key)
    this.values.set(key, value)
    return value
  }

  set(key: string, value: T): void {
    this.values.delete(key)
    this.values.set(key, value)
    while (this.values.size > this.capacity) {
      const oldest = this.values.keys().next().value as string | undefined
      if (oldest === undefined) break
      this.values.delete(oldest)
    }
  }

  has(key: string): boolean {
    return this.values.has(key)
  }
}

export function priorAvailableTurns(
  turns: readonly number[],
  selectedTurn: number,
  limit = 32,
): number[] {
  if (!Number.isInteger(limit) || limit < 1) return []
  return [...new Set(turns)]
    .filter((turn) => Number.isInteger(turn) && turn > 0 && turn < selectedTurn)
    .sort((left, right) => right - left)
    .slice(0, limit)
}
