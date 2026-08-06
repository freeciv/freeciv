import { describe, expect, it } from 'vitest'
import { factionDisplayLabel } from './faction-label'

describe('political map faction labels', () => {
  it('labels a harness seat as model: civilization', () => {
    expect(factionDisplayLabel({
      controller_label: 'pi-gpt-5.6-sol',
      controller_type: 'external',
      nation: 'Japanese',
      player_name: 'AgentPlace1',
    })).toBe('pi-gpt-5.6-sol: Japanese')
  })

  it('disambiguates native and dynamic factions by nation', () => {
    expect(factionDisplayLabel({
      controller_label: 'Freeciv Classic AI',
      controller_type: 'native',
      nation: 'Danish',
      player_name: 'NativePlace2',
    })).toBe('In-game Deity AI: Danish')
    expect(factionDisplayLabel({
      controller_label: 'Freeciv dynamic faction',
      controller_type: 'dynamic',
      nation: 'Pirate',
      player_name: 'Blackbeard',
    })).toBe('Freeciv dynamic: Pirate')
  })
})
