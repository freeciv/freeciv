/**
 * The evaluation frame a full-control-v2 seat is judged against.
 *
 * Ports `_validate_evaluation_context` (client.py:1010-1056).
 *
 * PORT_MAP §0 assigned this function to U02, and PORT_MAP §4.4 then moved the
 * decoder itself into `src/schema/primitives.ts` because `decodeHealth` and
 * `SessionStore.resolveV2` both call it.  This module is the U02-facing seam:
 * it re-exports the shared decoder with the error type the command layer wants,
 * so `join` never grows a second copy of the predicate that would be free to
 * drift from the one health and the session loader use.
 */
import { Effect } from 'effect';
import { PlayerError, playerError } from 'src/errors';
import {
  decodeEvaluationContext,
  type EvaluationContext,
  type EvaluationOptions,
} from 'src/schema/primitives';

export type { EvaluationContext, EvaluationOptions };

/**
 * `null` when the payload carries no evaluation fields at all and `required` is
 * not set; a refusal when it carries some of them, or malformed ones, or ones
 * that contradict what was agreed at join time.
 */
export const validateEvaluationContext = (
  value: unknown,
  label: string,
  options: EvaluationOptions = {}
): Effect.Effect<EvaluationContext | null, PlayerError> =>
  Effect.mapError(
    decodeEvaluationContext(value, label, options),
    (error): PlayerError => playerError(error.message)
  );
