/**
 * Faction naming, ported from the replay viewer's `faction-label.ts` so a
 * rendered film and a live viewer tab credit each side identically.
 *
 * Every side reads as "<harness or model>: <Civilization>", with the
 * civilization taken from the game's own data. The native opponent is never
 * called by its wire name or its raw player name.
 */

import type { PlayerEntry } from './dataset/schema'

/** The native opponent's display name. The wire still says "Freeciv Classic
 * AI" (a stable contract); this is presentation only. */
export const NATIVE_AI_LABEL = 'In-game Deity AI'

export const DYNAMIC_LABEL = 'Freeciv dynamic'

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
  if (isNative(player)) return NATIVE_AI_LABEL
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

/** The full "harness-model: Civilization" label. */
export function factionDisplayLabel(player: PlayerEntry): string {
  const controller = controllerDisplayName(player)
  const nation = nationDisplayName(player)
  return nation && nation !== controller ? `${controller}: ${nation}` : controller
}
