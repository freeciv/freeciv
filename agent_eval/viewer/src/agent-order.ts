/**
 * The agent is player one.
 *
 * Seat order comes from Freeciv, which has no opinion about who the match is
 * about, so a single-player run can open on the built-in AI and introduce the
 * model second -- making the model the opponent in its own match page. Every
 * list of sides is stably partitioned instead: the model-driven sides move
 * ahead of everything else, and within each group the original seat order is
 * untouched. An agent-vs-agent match is therefore completely unaffected, and a
 * match with several native seats keeps their relative order.
 *
 * This mirrors the ordering the film applies in
 * `agent_eval/video/src/dataset/film.ts`. The two surfaces must agree, or the
 * same match reads differently in each. Change one, change the other.
 */

import { NATIVE_AI_LABEL, displayControllerLabel } from './faction-label'

/** The controller fields every side-shaped payload carries some subset of. */
export interface ControllerSource {
  /** Only configured places carry this; it is the authoritative answer. */
  controller?: string | null
  controller_label?: string | null
  controller_type?: string | null
}

/** True when Freeciv itself drives this side rather than a model harness. */
export function isNativeController(source: ControllerSource): boolean {
  if (source.controller === 'native_classic_ai') return true
  if (/native/i.test(source.controller_type ?? '')) return true
  // Older payloads name the controller without typing it. `displayControllerLabel`
  // is the viewer's existing test for "this label is the built-in AI" -- it
  // normalizes exactly those labels to `NATIVE_AI_LABEL` and leaves every model
  // name alone, so reusing it keeps one definition of native rather than two.
  return displayControllerLabel(source.controller_label) === NATIVE_AI_LABEL
}

/** True for the factions Freeciv spawns itself -- barbarians, pirates. */
export function isDynamicFaction(source: ControllerSource): boolean {
  return /dynamic/i.test(source.controller_type ?? '')
    || /dynamic faction/i.test(source.controller_label ?? '')
}

/** True when a model harness plays this side. */
export function isAgentController(source: ControllerSource): boolean {
  return !isNativeController(source) && !isDynamicFaction(source)
}

/**
 * Stable partition: agent-driven sides first, everything else in the order it
 * arrived. Deliberately a partition and not a ranking -- nothing here sorts by
 * score, and nothing here special-cases the single-player match.
 */
export function agentFirstBy<T>(
  sides: readonly T[],
  isAgent: (side: T) => boolean,
): T[] {
  return [...sides].sort(
    (left, right) => Number(isAgent(right)) - Number(isAgent(left)),
  )
}

/** `agentFirstBy` for anything that carries its own controller fields. */
export function agentFirst<T extends ControllerSource>(sides: readonly T[]): T[] {
  return agentFirstBy(sides, isAgentController)
}
