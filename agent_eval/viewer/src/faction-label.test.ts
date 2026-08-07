import { describe, expect, it } from 'vitest'
import {
  displayControllerLabel, factionDisplayLabel, nativeAiSummaryLabel,
} from './faction-label'

describe('political map faction labels', () => {
  it('labels a harness seat as model: civilization', () => {
    expect(factionDisplayLabel({
      controller_label: 'pi-gpt-5.6-sol',
      controller_type: 'external',
      nation: 'Japanese',
      player_name: 'AgentPlace1',
    })).toBe('pi-gpt-5.6-sol: Japanese')
  })

  it('labels the native side nation-first with its difficulty', () => {
    expect(factionDisplayLabel({
      controller_label: 'Freeciv Classic AI',
      controller_type: 'native',
      nation: 'Italian',
      player_name: 'NativePlace2',
      ai_difficulty: 'hard',
    })).toBe('Italian (CPU: Hard)')
  })

  it("names freeciv's cheating level Deity, as Civilization does", () => {
    expect(factionDisplayLabel({
      controller_label: 'Freeciv Classic AI',
      controller_type: 'native',
      nation: 'Italian',
      player_name: 'NativePlace2',
      ai_difficulty: 'cheating',
    })).toBe('Italian (CPU: Deity)')
  })

  it('drops the difficulty when the payload does not carry one', () => {
    // Every archive written before `ai_difficulty` existed lands here.
    const native = {
      controller_label: 'Freeciv Classic AI',
      controller_type: 'native',
      nation: 'Danish',
      player_name: 'NativePlace2',
    }
    expect(factionDisplayLabel(native)).toBe('Danish (CPU)')
    expect(factionDisplayLabel({ ...native, ai_difficulty: null }))
      .toBe('Danish (CPU)')
    expect(factionDisplayLabel({ ...native, ai_difficulty: 'novice' }))
      .toBe('Danish (CPU)')
  })

  it('leaves the dynamic faction naming alone', () => {
    expect(factionDisplayLabel({
      controller_label: 'Freeciv dynamic faction',
      controller_type: 'dynamic',
      nation: 'Pirate',
      player_name: 'Blackbeard',
    })).toBe('Freeciv dynamic: Pirate')
  })

  it('renames a bare controller label with its difficulty', () => {
    expect(displayControllerLabel('Freeciv Classic AI')).toBe('CPU')
    expect(displayControllerLabel('Freeciv Classic AI', 'cheating'))
      .toBe('CPU: Deity')
    expect(displayControllerLabel('pi-gpt-5.6-sol')).toBe('pi-gpt-5.6-sol')
    // The label this module used to emit is still recognised, so a payload or
    // a cache carrying the old string is not mistaken for a model name.
    expect(displayControllerLabel('In-game Deity AI')).toBe('CPU')
  })

  it('summarizes however many native seats are playing', () => {
    expect(nativeAiSummaryLabel(0)).toBeNull()
    expect(nativeAiSummaryLabel(0, 'hard')).toBeNull()
    expect(nativeAiSummaryLabel(1)).toBe('CPU')
    expect(nativeAiSummaryLabel(1, 'hard')).toBe('CPU: Hard')
    expect(nativeAiSummaryLabel(2, 'cheating')).toBe('CPU: Deity ×2')
    expect(nativeAiSummaryLabel(3)).toBe('CPU ×3')
  })
})
