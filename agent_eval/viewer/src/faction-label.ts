export interface FactionLabelSource {
  controller_label?: string | null
  controller_type?: string | null
  nation?: string | null
  player_name: string
}

export function factionDisplayLabel(source: FactionLabelSource): string {
  const controller = source.controller_label?.trim() ?? ''
  const nation = source.nation?.trim() || source.player_name.trim()
  if (/classic ai/i.test(controller) || /native/i.test(source.controller_type ?? '')) {
    return `Classic AI · ${nation}`
  }
  if (/dynamic faction/i.test(controller) || /dynamic/i.test(source.controller_type ?? '')) {
    return `Freeciv dynamic · ${nation}`
  }
  return controller || source.player_name.trim() || nation
}
