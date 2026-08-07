/**
 * The in-game year, read the way the game says it.
 *
 * Mirrors `agent_eval/video/src/format.ts` (`formatYear`) deliberately: the
 * film and the viewer are separate builds with no shared package, so the rule
 * is duplicated rather than imported. Change one, change the other — the two
 * surfaces show the same match and must not disagree about its date.
 *
 * Freeciv emits a signed year: the classic calendar starts at -4000 and steps
 * forward, skipping year 0 entirely (`calendar_skip_0`, see
 * `common/calendar.c`), so the zero branch below is defence, not a real case.
 */
export function formatYear(year: number): string {
  if (year < 0) return `${Math.abs(year)} BC`
  if (year === 0) return '1 AD'
  return `AD ${year}`
}
