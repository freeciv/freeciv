import { describe, expect, it } from 'vitest'
import {
  DENSITY_FLOOR, heaviestPerWindow, isMustShow, planCaptions, topHighlights, weightTier,
} from './event-log'
import type { GameEvent } from './dataset/schema'

/**
 * `heaviestPerWindow`, `DENSITY_FLOOR` and `weightTier` are ports of the
 * viewer's `event-log.ts`. These pin the shared behaviour so the film's
 * captions and the viewer's "key moments" cannot drift apart.
 */

function event(
  turn: number, kind: string, weight: number, summary = kind,
  data: Record<string, unknown> = {},
): GameEvent {
  return { turn, kind, summary, actors: ['place-1'], weight, data }
}

/** A captured capital, marked the way the extractor marks it. */
function capitalCapture(turn: number, city = 'London'): GameEvent {
  return event(turn, 'city_captured', CAPITAL_CAPTURE, `captured the capital ${city}`,
    { capital_cities: [city], cities: [city] })
}

// The extractor's documented tiers, so a change to them fails here.
const ELIMINATION = 96
const SPACESHIP_LAUNCH = 88
const WAR = 80
const CAPITAL_CAPTURE = 66
const CAPTURE = 52
const FOUNDING = 8

describe('shared vocabulary with the viewer', () => {
  it('keeps the density floors the viewer selects with', () => {
    expect(DENSITY_FLOOR).toEqual({ all: 0, key: 30, major: 60 })
  })

  it('ranks the extractor tiers the way the viewer ranks them', () => {
    expect(weightTier(ELIMINATION)).toBe('major')
    expect(weightTier(SPACESHIP_LAUNCH)).toBe('major')
    expect(weightTier(WAR)).toBe('major')
    expect(weightTier(CAPITAL_CAPTURE)).toBe('major')
    expect(weightTier(CAPTURE)).toBe('notable')
    expect(weightTier(FOUNDING)).toBe('routine')
  })

  it('takes the heaviest event per window and skips empty windows', () => {
    const events = [
      event(1, 'city_founded', FOUNDING),
      event(4, 'war_declared', WAR),
      event(44, 'city_captured', CAPTURE),
    ]
    expect(heaviestPerWindow(events, 10).map((entry) => entry.turn)).toEqual([4, 44])
  })

  it('breaks a tie inside a window toward the earlier event', () => {
    const events = [event(2, 'city_captured', CAPTURE), event(8, 'city_destroyed', CAPTURE)]
    expect(heaviestPerWindow(events, 10).map((entry) => entry.turn)).toEqual([2])
  })

  it('passes everything through when the window is not a usable width', () => {
    const events = [event(1, 'war_declared', WAR)]
    expect(heaviestPerWindow(events, 0)).toEqual(events)
    expect(heaviestPerWindow(events, Number.NaN)).toEqual(events)
  })
})

describe('caption pacing', () => {
  const fast = { turnsPerSecond: 7.5, holdSeconds: 2.6 }
  const slow = { turnsPerSecond: 2.5, holdSeconds: 2.6 }

  it('folds away small beats when the film is running fast', () => {
    const plan = planCaptions([
      event(10, 'city_founded', FOUNDING),
      event(11, 'government_changed', 22),
    ], fast)
    expect(plan.weightFloor).toBe(DENSITY_FLOOR.key)
    expect(plan.captions).toHaveLength(0)
  })

  it('lets the small beats through when the timeline can breathe', () => {
    const plan = planCaptions([event(10, 'city_founded', FOUNDING)], slow)
    expect(plan.weightFloor).toBe(DENSITY_FLOOR.all)
    expect(plan.captions.map((caption) => caption.event.turn)).toEqual([10])
  })

  it('never lets two captions share a window, however busy the turn', () => {
    const burst = [
      event(200, 'city_captured', CAPTURE),
      event(201, 'city_captured', CAPTURE),
      event(202, 'city_captured', CAPTURE),
      event(203, 'player_eliminated', ELIMINATION),
    ]
    const plan = planCaptions(burst, fast)
    expect(plan.captions).toHaveLength(1)
    // The heaviest takes the slot; the rest are reported, not dropped silently.
    expect(plan.captions[0]?.event.kind).toBe('player_eliminated')
    expect(plan.captions[0]?.alsoInWindow).toBe(3)
  })

  it('counts a same-kind burst so it can collapse to one line', () => {
    const plan = planCaptions([
      event(300, 'city_captured', CAPTURE),
      event(301, 'city_captured', CAPTURE),
    ], fast)
    expect(plan.captions[0]?.sameKindInWindow).toBe(2)
  })

  it('spaces captions wider than the time one stays on screen', () => {
    const plan = planCaptions([event(1, 'war_declared', WAR)], fast)
    // 7.5 turns/s over a 2.6s hold is 19.5 turns of screen time; the window has
    // to exceed that or two captions would overlap.
    expect(plan.windowTurns).toBeGreaterThan(fast.turnsPerSecond * fast.holdSeconds)
  })

  it('keeps every major moment of the lnj arc at film speed', () => {
    // The real sequence: war, the capital falling, and the spaceship launch.
    const plan = planCaptions([
      event(181, 'war_declared', WAR),
      event(349, 'city_captured', CAPITAL_CAPTURE),
      event(494, 'spaceship_launched', SPACESHIP_LAUNCH),
      event(495, 'city_founded', FOUNDING),
    ], fast)
    expect(plan.captions.map((caption) => caption.event.kind)).toEqual([
      'war_declared', 'city_captured', 'spaceship_launched',
    ])
  })
})

describe('end-screen highlights', () => {
  it('takes the heaviest of the whole match but reads in turn order', () => {
    const events = [
      event(2, 'city_founded', FOUNDING),
      event(494, 'spaceship_launched', SPACESHIP_LAUNCH),
      event(181, 'war_declared', WAR),
      event(349, 'city_captured', CAPITAL_CAPTURE),
    ]
    expect(topHighlights(events, 3).map((entry) => entry.turn)).toEqual([181, 349, 494])
  })

  it('caps one kind so a raiding streak cannot bury the arc', () => {
    // The real lnj shape: three pirate captures outweigh nothing else, but
    // straight weight order would push the war and the launch off the list.
    const events = [
      event(128, 'city_captured', CAPTURE, 'Trento'),
      event(132, 'city_captured', CAPTURE, 'Bergamo'),
      event(136, 'city_captured', CAPTURE, 'Monza'),
      event(181, 'war_declared', WAR),
      event(494, 'spaceship_launched', SPACESHIP_LAUNCH),
    ]
    const kinds = topHighlights(events, 4).map((entry) => entry.kind)
    expect(kinds.filter((kind) => kind === 'city_captured')).toHaveLength(2)
    expect(kinds).toContain('war_declared')
    expect(kinds).toContain('spaceship_launched')
  })

  it('relaxes the cap when there is genuinely nothing else', () => {
    const events = [
      event(1, 'city_captured', CAPTURE),
      event(2, 'city_captured', CAPTURE),
      event(3, 'city_captured', CAPTURE),
    ]
    expect(topHighlights(events, 3)).toHaveLength(3)
  })

  it('asks for more than happened without inventing any', () => {
    expect(topHighlights([event(1, 'war_declared', WAR)], 7)).toHaveLength(1)
    expect(topHighlights([], 7)).toEqual([])
  })
})

describe('must-show beats', () => {
  const fast = { turnsPerSecond: 7.5, holdSeconds: 2.6 }
  const crawl = { turnsPerSecond: 60, holdSeconds: 2.6 }

  it('recognises the guaranteed kinds from the payload, not the weight', () => {
    expect(isMustShow(capitalCapture(349))).toBe(true)
    expect(isMustShow(event(10, 'player_eliminated', ELIMINATION))).toBe(true)
    expect(isMustShow(event(10, 'spaceship_launched', SPACESHIP_LAUNCH))).toBe(true)
    expect(isMustShow(event(10, 'spaceship_arrived', 94))).toBe(true)
    expect(isMustShow(event(10, 'match_ended', 100))).toBe(true)
    expect(isMustShow(event(10, 'war_declared', 92, 'war', { broke_pact: 'peace' }))).toBe(true)
    // An ordinary capture and an ordinary war are not guaranteed.
    expect(isMustShow(event(10, 'city_captured', CAPTURE))).toBe(false)
    expect(isMustShow(event(10, 'war_declared', WAR))).toBe(false)
  })

  it('captions a capital capture even when heavier events crowd its window', () => {
    const plan = planCaptions([
      capitalCapture(349),
      event(350, 'player_eliminated', ELIMINATION),
      event(351, 'spaceship_launched', SPACESHIP_LAUNCH),
    ], fast)
    const kinds = plan.captions.map((caption) => caption.event.kind)
    expect(kinds).toContain('city_captured')
    expect(kinds).toContain('player_eliminated')
    expect(kinds).toContain('spaceship_launched')
  })

  it('queues a displaced beat instead of dropping it', () => {
    const plan = planCaptions([capitalCapture(349), capitalCapture(350, 'Paris')], fast)
    expect(plan.captions).toHaveLength(2)
    // The second keeps its true turn but plays a beat later.
    expect(plan.captions[1]?.event.turn).toBe(350)
    expect(plan.captions[1]?.displayTurn).toBeGreaterThan(plan.captions[0]!.displayTurn)
  })

  it('survives a floor that would otherwise cut it', () => {
    // At a crawl the floor is `key`; a must-show is never subject to it.
    const light = event(10, 'city_captured', 1, 'capital', { capital_cities: ['X'] })
    const plan = planCaptions([light], crawl)
    expect(plan.weightFloor).toBe(DENSITY_FLOOR.key)
    expect(plan.captions.map((caption) => caption.event)).toEqual([light])
  })

  it('takes the slot from an ordinary event sharing its window', () => {
    // The ordinary capture is the one that folds into "+N more"; the
    // guaranteed beat is always the caption that actually plays.
    const plan = planCaptions([
      event(340, 'city_captured', CAPTURE),
      capitalCapture(341),
    ], fast)
    expect(plan.captions).toHaveLength(1)
    expect(plan.captions[0]?.mustShow).toBe(true)
    expect(plan.captions[0]?.event.turn).toBe(341)
    expect(plan.captions[0]?.alsoInWindow).toBe(1)
  })

  it('never counts a queued beat as another caption\'s +N chip', () => {
    // Both are guaranteed, so both play -- and neither is reported as the
    // other's overflow, which would double-count the same moment.
    const plan = planCaptions([capitalCapture(349), capitalCapture(350, 'Paris')], fast)
    expect(plan.captions).toHaveLength(2)
    for (const caption of plan.captions) expect(caption.alsoInWindow).toBe(0)
  })

  it('keeps captions from ever overlapping, queued or not', () => {
    const plan = planCaptions([
      capitalCapture(100), capitalCapture(101, 'B'), capitalCapture(102, 'C'),
      event(140, 'city_captured', CAPTURE),
      event(300, 'player_eliminated', ELIMINATION),
    ], fast)
    for (let index = 0; index + 1 < plan.captions.length; index += 1) {
      const current = plan.captions[index]!
      const next = plan.captions[index + 1]!
      expect(current.displayTurn + current.holdTurns).toBeLessThanOrEqual(next.displayTurn)
    }
  })

  it('holds a landmark longer than a routine caption when there is room', () => {
    const solo = planCaptions([capitalCapture(200)], { ...fast, landmarkHoldSeconds: 4.2 })
    const routine = planCaptions([event(200, 'war_declared', WAR)], fast)
    expect(solo.captions[0]?.mustShow).toBe(true)
    expect(solo.captions[0]!.holdTurns).toBeGreaterThan(routine.captions[0]!.holdTurns)
  })

  it('reports a burst around a guaranteed beat without absorbing it', () => {
    // The lnj T349 shape: the capital falls, another city falls 13 turns later
    // in the same window. The burst count must not rewrite the capital's text.
    const plan = planCaptions([
      capitalCapture(349),
      event(362, 'city_captured', CAPTURE),
    ], fast)
    expect(plan.captions).toHaveLength(1)
    const caption = plan.captions[0]!
    expect(caption.mustShow).toBe(true)
    expect(caption.event.summary).toContain('capital')
    // The count is still reported, for the renderer to show as "+N more".
    expect(caption.alsoInWindow).toBe(1)
    expect(caption.sameKindInWindow).toBe(2)
  })

  it('keeps both films\' capital captures at their real turns', () => {
    // lnj T349 and a8 T477, each against the busiest window it actually had.
    for (const turn of [349, 477]) {
      const plan = planCaptions([
        capitalCapture(turn),
        event(turn, 'capital_moved', 34),
        event(turn, 'wonder_captured', 44),
        event(turn + 3, 'city_captured', CAPTURE),
      ], fast)
      const capital = plan.captions.find((caption) => caption.mustShow)
      expect(capital?.event.turn).toBe(turn)
      expect(capital?.displayTurn).toBe(turn)
    }
  })
})
