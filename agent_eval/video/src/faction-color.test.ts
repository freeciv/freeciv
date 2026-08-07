import { describe, expect, it } from 'vitest'
import { planFactionColors } from './faction-color'
import type { PlayerEntry } from './dataset/schema'

function faction(playerId: number, color: string, nation: string): PlayerEntry {
  return {
    playerId,
    seatId: `place-${playerId + 1}`,
    seat: nation !== 'Pirate' && nation !== 'Barbarian',
    name: `Player ${playerId}`,
    nation,
    color,
    controllerLabel: null,
    controllerType: null,
    model: null,
    aiDifficulty: null,
  }
}

const CRIMSON = '#E01B24'

/**
 * Freeciv deals the Pirate nation a different colour in every match -- #FA8072
 * in one export, #FF4F00 in another. A pin keyed on the recorded hex therefore
 * fixes exactly the game it was read off and leaves every other one to the
 * substitute pool, which is how raiders came out crimson in one film and aqua
 * mint in the next. These pin the identity, not the hex.
 */
describe('raiders are crimson whatever colour they were dealt', () => {
  it.each([
    ['#FA8072', 'the salmon one export records'],
    ['#FF4F00', 'the orange-red another export records'],
    ['#B8860B', 'a goldenrod that clears no terrain'],
  ])('%s (%s)', (recorded) => {
    const plan = planFactionColors([
      faction(0, '#0067A5', 'Portuguese'),
      faction(1, '#F38400', 'Aztec'),
      faction(2, recorded, 'Pirate'),
    ])
    expect(plan.colorByPlayer.get(2)).toBe(CRIMSON)
  })

  it('still moves anyone else off crimson', () => {
    // The pin joins the claimed set like any other colour, so a barbarian
    // dealt a nearby dark red does not end up indistinguishable from raiders.
    const plan = planFactionColors([
      faction(0, '#FF4F00', 'Pirate'),
      faction(1, '#8B0000', 'Barbarian'),
    ])
    expect(plan.colorByPlayer.get(0)).toBe(CRIMSON)
    expect(plan.colorByPlayer.get(1)).not.toBe(CRIMSON)
  })

  it('leaves the seated sides alone', () => {
    const plan = planFactionColors([
      faction(0, '#0067A5', 'Portuguese'),
      faction(1, '#F38400', 'Aztec'),
      faction(2, '#FF4F00', 'Pirate'),
    ])
    // The agent blue is still pinned to periwinkle; the native orange clears
    // terrain and keeps its own colour.
    expect(plan.colorByPlayer.get(0)).toBe('#A78BFA')
    expect(plan.colorByPlayer.get(1)).toBe('#F38400')
  })
})
