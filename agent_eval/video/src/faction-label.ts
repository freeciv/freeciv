/**
 * Faction naming, ported from the replay viewer's `faction-label.ts` so a
 * rendered film and a live viewer tab credit each side identically.
 *
 * An agent side reads as "<harness or model>: <Civilization>". The native
 * opponent reads nation-first, as "<Civilization> (CPU: <difficulty>)": it has
 * no name to credit, so the civilization leads and the controller trails as a
 * parenthetical. Neither side is ever called by its wire name or raw player
 * name.
 */

import type { PlayerEntry } from './dataset/schema'

/** The native opponent's base display name. The wire still says "Freeciv
 * Classic AI" (a stable contract); this is presentation only. */
export const NATIVE_AI_LABEL = 'CPU'

export const DYNAMIC_LABEL = 'Freeciv dynamic'

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

function isNative(player: PlayerEntry): boolean {
  const controller = player.controllerLabel?.trim() ?? ''
  return /classic ai|deity ai/i.test(controller)
    || /native/i.test(player.controllerType ?? '')
}

function isDynamic(player: PlayerEntry): boolean {
  const controller = player.controllerLabel?.trim() ?? ''
  return /dynamic faction/i.test(controller)
    || /dynamic/i.test(player.controllerType ?? '')
}

/** The controller half of the label: the harness, the model, or the AI. */
export function controllerDisplayName(player: PlayerEntry): string {
  if (isNative(player)) return nativeAiLabel(player.aiDifficulty)
  if (isDynamic(player)) return DYNAMIC_LABEL
  return player.controllerLabel?.trim()
    || player.model?.trim()
    || player.name.trim()
    || player.nation.trim()
}

/** The civilization half: the nation from the game data. */
export function nationDisplayName(player: PlayerEntry): string {
  return player.nation.trim() || player.name.trim()
}

/** The full one-line label for a side. */
export function factionDisplayLabel(player: PlayerEntry): string {
  const controller = controllerDisplayName(player)
  const nation = nationDisplayName(player)
  if (isNative(player)) {
    return nation ? `${nation} (${controller})` : controller
  }
  return nation && nation !== controller ? `${controller}: ${nation}` : controller
}
