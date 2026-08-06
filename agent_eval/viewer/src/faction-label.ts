export interface FactionLabelSource {
  controller_label?: string | null
  controller_type?: string | null
  nation?: string | null
  player_name: string
}

/** The native opponent's display name. The wire still says
 * "Freeciv Classic AI" (a stable contract); this is presentation only. */
export const NATIVE_AI_LABEL = 'In-game Deity AI'

export function displayControllerLabel(label: string | null | undefined): string {
  const trimmed = label?.trim() ?? ''
  return /classic ai/i.test(trimmed) ? NATIVE_AI_LABEL : trimmed
}

export function factionDisplayLabel(source: FactionLabelSource): string {
  const controller = source.controller_label?.trim() ?? ''
  const nation = source.nation?.trim() || source.player_name.trim()
  if (/classic ai|deity ai/i.test(controller) || /native/i.test(source.controller_type ?? '')) {
    return `${NATIVE_AI_LABEL}: ${nation}`
  }
  if (/dynamic faction/i.test(controller) || /dynamic/i.test(source.controller_type ?? '')) {
    return `Freeciv dynamic: ${nation}`
  }
  const named = controller || source.player_name.trim() || nation
  const realNation = source.nation?.trim()
  return realNation && realNation !== named ? `${named}: ${realNation}` : named
}
