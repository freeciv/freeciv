/**
 * The two health projections the turn header is built from.
 *
 * Ports `_turn_health_epoch` and `_turn_health_context` (client.py:6570-6597).
 *
 * `turn` reads health twice — once to decide what to fetch, once after the
 * pages land — and reprints the header only if the second read still describes
 * the same phase.  The *epoch* is the seven fields that decide "same phase";
 * the *context* is what `--json` embeds under `"context"`.  Both live here
 * rather than in `turn` because `do --end --await` builds the identical header
 * and must not re-derive it.
 */
import type {
  HealthEnvelope,
  HealthSeat,
  JsonObject,
  PhaseBlock,
  PhaseEndEvent,
} from 'src/schema/index';

/** The seven values that decide whether two health reads describe one phase. */
export type TurnHealthEpoch = readonly [
  gameState: string,
  observationAvailable: boolean,
  legalActionsAvailable: boolean,
  phaseState: string | null,
  turn: number | null,
  phase: number | null,
  active: boolean | null,
];

export const turnHealthEpoch = (health: HealthEnvelope): TurnHealthEpoch => {
  const phase = health.phase;
  return [
    health.game_state,
    health.observation_available,
    health.legal_actions_available,
    phase === null ? null : phase.state,
    phase === null ? null : phase.turn,
    phase === null ? null : phase.phase,
    phase === null ? null : phase.active,
  ];
};

/**
 * CPython compares the two epochs with `==` on tuples; JavaScript needs this.
 *
 * Element-wise `===` is the whole comparison: every member is a scalar by
 * construction, so there is no case where a deep compare would say more.
 */
export const turnHealthEpochsEqual = (left: TurnHealthEpoch, right: TurnHealthEpoch): boolean =>
  left.every((value, index) => value === right[index]);

/**
 * What `--json` carries under `"context"`.
 *
 * The absent-evaluation fields are `null` rather than missing, exactly as
 * `health.get("objective")` renders them: a consumer branching on the key would
 * otherwise see a different shape from a game with no turn limit.
 *
 * **Its numbers are plain JavaScript numbers.**  `phase` and `last_phase_end`
 * are copied in verbatim, so this object carries all twelve of the supervisor's
 * Python floats (`timeout_s`, `deadline_at`, `elapsed_s`, `grace_s`, …).
 * `printV2Json`/`compactJson` print those as `600`, CPython prints `600.0`.
 */
export interface TurnHealthContext {
  readonly game_state: string;
  readonly objective: string | null;
  readonly max_turns: number | null;
  readonly turns_remaining: number | null;
  readonly agent: { readonly agent_id: string; readonly controller_label: string };
  readonly seat: HealthSeat;
  readonly sidecar: JsonObject;
  readonly observation_available: boolean;
  readonly legal_actions_available: boolean;
  readonly phase: PhaseBlock | null;
  readonly last_phase_end: PhaseEndEvent | null;
}

/**
 * The `"context"` part of every `turn` JSON payload.
 *
 * **Serializing it?  Do not hand it to `printV2Json`/`compactJson`.**  Use
 * `turnHealthContextPyValue` (or `pyValueWithFloats` +
 * `healthFloatPathsUnder('context')` over the whole payload) from
 * `src/services/health-json`, then `pyDumps`.  This function returns the object
 * the *renderers* need — typed, with plain numbers — and printing it directly
 * writes `"timeout_s":600` where CPython writes `"timeout_s":600.0`, on seven
 * or more tokens of every live `turn --json` line.  See NOTES §10.3.
 */
export const turnHealthContext = (health: HealthEnvelope): TurnHealthContext => ({
  game_state: health.game_state,
  objective: health.objective ?? null,
  max_turns: health.max_turns ?? null,
  turns_remaining: health.turns_remaining ?? null,
  agent: health.agent,
  seat: health.seat,
  sidecar: health.sidecar,
  observation_available: health.observation_available,
  legal_actions_available: health.legal_actions_available,
  phase: health.phase,
  last_phase_end: health.last_phase_end,
});
