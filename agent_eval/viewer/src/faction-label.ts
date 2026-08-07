export interface FactionLabelSource {
  controller_label?: string | null
  controller_type?: string | null
  nation?: string | null
  player_name: string
  /** The freeciv server AI level, when the payload carries one. */
  ai_difficulty?: string | null
}

/** The native opponent's base display name. The wire still says
 * "Freeciv Classic AI" (a stable contract); this is presentation only. */
export const NATIVE_AI_LABEL = 'CPU'

/**
 * Freeciv's AI levels, named the way a Civilization player names them.
 *
 * `cheating` IS Civilization's Deity: it is the level where the AI gets the
 * bonuses the human does not, and freeciv simply calls that what it does
 * rather than what it feels like. Do not "correct" this mapping to a level
 * called `deity` -- freeciv has no such level, and dropping the entry would
 * silently demote every Deity match to an unqualified "CPU".
 *
 * Levels outside this table render as a plain "CPU": naming a difficulty we
 * have not deliberately translated is worse than naming none.
 */
const DIFFICULTY_NAMES: Readonly<Record<string, string>> = {
  hard: 'Hard',
  cheating: 'Deity',
}

/** "CPU", or "CPU: Deity" when the difficulty is one we name. */
export function nativeAiLabel(difficulty?: string | null): string {
  const name = DIFFICULTY_NAMES[difficulty?.trim().toLowerCase() ?? '']
  return name ? `${NATIVE_AI_LABEL}: ${name}` : NATIVE_AI_LABEL
}

/**
 * The native side of a match header: "CPU", "CPU: Hard", or "CPU: Hard ×2".
 *
 * Null when no native seat is playing, so an agent-vs-agent header never grows
 * a trailing "vs". The level is one game-wide server setting, so several
 * native seats share a single reading rather than repeating it per seat.
 */
export function nativeAiSummaryLabel(
  count: number, difficulty?: string | null,
): string | null {
  if (count < 1) return null
  const label = nativeAiLabel(difficulty)
  return count > 1 ? `${label} ×${count}` : label
}

export function displayControllerLabel(
  label: string | null | undefined, difficulty?: string | null,
): string {
  const trimmed = label?.trim() ?? ''
  // "deity ai" catches the label this module itself used to emit, so a payload
  // or a cache still carrying the old display string is still recognised as
  // the built-in AI rather than mistaken for a model name.
  return /classic ai|deity ai/i.test(trimmed) ? nativeAiLabel(difficulty) : trimmed
}

export function factionDisplayLabel(source: FactionLabelSource): string {
  const controller = source.controller_label?.trim() ?? ''
  const nation = source.nation?.trim() || source.player_name.trim()
  if (/classic ai|deity ai/i.test(controller) || /native/i.test(source.controller_type ?? '')) {
    // The native side reads nation-first -- "Italian (CPU: Hard)" -- because
    // the civilization is what a viewer tracks on the map and the controller
    // is a parenthetical, not a competitor's name.
    return `${nation} (${nativeAiLabel(source.ai_difficulty)})`
  }
  if (/dynamic faction/i.test(controller) || /dynamic/i.test(source.controller_type ?? '')) {
    return `Freeciv dynamic: ${nation}`
  }
  const named = controller || source.player_name.trim() || nation
  const realNation = source.nation?.trim()
  return realNation && realNation !== named ? `${named}: ${realNation}` : named
}
