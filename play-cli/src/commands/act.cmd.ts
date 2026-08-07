/**
 * `play act` — submit one strategic-v1 action for one turn.
 *
 * Ports `command_act` (client.py:11528-11580).  Like `next` and `result` it is
 * in `V2_JSON_ONLY_COMMANDS`: no `--json` flag, always the `_print_json` shape.
 *
 * Two safety properties are the whole point of the tail of this command, and
 * both end in the same sentence so the agent's loop can key off it:
 *
 * - an acknowledgement that is not *explicitly* `accepted: true` is a failure,
 *   never an optimistic success;
 * - an acknowledgement that echoes a different game, seat, place, controller or
 *   turn than the one this session just acted for is a failure too.
 *
 * In both cases `do not advance LAST_TURN`, so the supervisor redelivers the
 * turn instead of the agent silently skipping it.
 */
import { Command, Options } from '@effect/cli';
import { Effect } from 'effect';
import { STRATEGIC_V1 } from 'src/constants';
import { PlayerError, SessionMissingError, playerError } from 'src/errors';
import { dualText, resolveDualRequired } from 'src/options';
import { compactJson } from 'src/services/json-output';
import { parsePyArgument, printPyJson, pyField, pyToJson, v1Json } from 'src/services/v1-json';
import { isPyMapping } from 'src/services/canonical-body';
import { SessionStore } from 'src/services/session-store';
import { field, type JsonObject, type JsonValue } from 'src/schema/primitives';

/**
 * Python's `!=` over JSON values.
 *
 * The echoed fields are scalars today, but the comparison has to be total: a
 * supervisor that answers `place: [1]` must fail the check, not coerce into
 * passing it.  Serializing both sides is the cheapest total equality that also
 * treats `1` and `1.0` as the same number, exactly as CPython does.
 */
const sameJson = (left: JsonValue, right: JsonValue): boolean =>
  compactJson(left) === compactJson(right);

/**
 * The acknowledgement fields worth cross-checking, in the Python's order.
 *
 * These are read off the **raw on-disk session object**, never off the decoded
 * `Session`, because the two disagree in both directions and each disagreement
 * is a bug:
 *
 * - `decodeSession` fills a missing string with `''` (`readString(...) ?? ''`),
 *   so a session with no `controller_label` would compare `''` against the
 *   supervisor's real label and refuse every action.  CPython does
 *   `session.get("controller_label")` → `None` → the check is skipped.
 * - `decodeSession` narrows `place` to a number, so the non-digit place
 *   `just join --place north` writes (client.py:6246 sends a non-digit place
 *   verbatim) decodes to `null` — which would *disable* the seat check and let
 *   an acknowledgement for a different seat through.  Same hole for a
 *   non-string `seat_id` / `game_id` / `agent_id`.
 *
 * `field` is CPython's `dict.get`: absent → `null` → not cross-checked, exactly
 * like `expected_value is not None`.  A JSON `null` on disk is `None` in CPython
 * too, so the two collapse to the same skip.
 */
const echoedFields = (
  raw: JsonObject,
  turn: number
): ReadonlyArray<readonly [string, JsonValue]> =>
  [
    ['game_id', field(raw, 'game_id')],
    ['agent_id', field(raw, 'agent_id')],
    ['turn', turn],
    ['place', field(raw, 'place')],
    ['seat_id', field(raw, 'seat_id')],
    ['controller_label', field(raw, 'controller_label')],
  ] as const;

export const actCommand = Command.make(
  'act',
  {
    session: Options.text('session').pipe(
      Options.withDefault(''),
      Options.withDescription('the private session file this command acts against')
    ),
    turn: Options.integer('turn'),
    // `--observation_id` is the spelling the justfile veneer exposed
    // (justfile:116) and the one `prompt`'s card teaches; both are accepted.
    observationId: dualText('observation-id'),
    action: Options.text('action'),
  },
  ({
    session,
    turn,
    observationId,
    action,
  }): Effect.Effect<void, PlayerError | SessionMissingError, SessionStore> =>
    Effect.gen(function* () {
      const observation = yield* resolveDualRequired('observation-id', observationId);
      const sessions = yield* SessionStore;
      const http = yield* v1Json;
      const loaded = yield* sessions.resolve(session);
      if (loaded.session.controlProtocol !== STRATEGIC_V1) {
        return yield* Effect.fail(
          playerError(
            'just act is strategic-v1 only; this full-control-v2 session ' +
              'uses `just batch` and durable receipts'
          )
        );
      }
      if (turn <= 0 || observation === '') {
        return yield* Effect.fail(
          playerError('a positive turn and nonempty observation ID are required')
        );
      }
      // `parsePyArgument` keeps a `1.0` in the action a float all the way to
      // the request body, and answers a typo with CPython's own
      // `JSONDecodeError` sentence.
      const decoded = parsePyArgument(action);
      if (!decoded.ok) {
        return yield* Effect.fail(playerError(`--action must be valid JSON: ${decoded.message}`));
      }
      const parsed = decoded.value;
      if (!isPyMapping(parsed)) {
        return yield* Effect.fail(playerError('--action must be a JSON object'));
      }
      const value = yield* http.requestPyJson(
        'POST',
        `${loaded.session.serviceUrl}/v1/games/${loaded.session.gameId}/me/actions`,
        {
          token: loaded.session.agentToken,
          body: { turn, observation_id: observation, action: parsed },
          timeout: 30,
        }
      );
      if (pyField(value, 'accepted') !== true) {
        return yield* Effect.fail(
          playerError(
            'the supervisor did not acknowledge the action as accepted; ' +
              'do not advance LAST_TURN'
          )
        );
      }
      for (const [key, expected] of echoedFields(loaded.session.raw, turn)) {
        // `expected_value is not None and key in value and value[key] != …`
        if (expected === null) continue;
        if (!Object.prototype.hasOwnProperty.call(value, key)) continue;
        if (!sameJson(pyToJson(pyField(value, key)), expected)) {
          return yield* Effect.fail(
            playerError(
              `the accepted action acknowledgement has the wrong ${key}; ` +
                'do not advance LAST_TURN'
            )
          );
        }
      }
      yield* printPyJson(value);
    })
);
