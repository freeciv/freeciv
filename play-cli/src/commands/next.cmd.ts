/**
 * `play next` — the strategic-v1 long-poll for the next observation.
 *
 * Ports `command_next` (client.py:11498-11527).  `next` is in
 * `V2_JSON_ONLY_COMMANDS`, so it declares no `--json` flag and always prints the
 * `_print_json` shape: two-space indent, keys sorted, one trailing newline.
 *
 * The order of the checks is part of the contract.  The session is resolved and
 * proved strategic-v1 *before* the argument bounds are checked, because the
 * useful refusal for a full-control-v2 workspace is the one naming `just health`
 * — not a complaint about `--wait-s`.
 */
import { Command, Options } from '@effect/cli';
import { Console, Effect, Option } from 'effect';
import { STRATEGIC_V1, TERMINAL_STATES } from 'src/constants';
import { PlayerError, SessionMissingError, playerError } from 'src/errors';
import { dualFloat, dualInteger, resolveDual, resolveDualOption } from 'src/options';
import { compactJson } from 'src/services/json-output';
import { printPyJson, pyField, pyToJson, v1Json } from 'src/services/v1-json';
import type { PyObject } from 'src/services/canonical-body';
import { SessionStore } from 'src/services/session-store';
import { field, type JsonObject, type JsonValue } from 'src/schema/primitives';

/**
 * `value.get(key) not in {None, session[key]}` — the identity guard both of
 * `next`'s checks are written as.
 *
 * The expected side is read off the **raw** session object rather than the
 * decoded `Session`, for the reason spelled out in `act.cmd.ts`: `decodeSession`
 * substitutes `''` for a missing or non-string field, which both invents
 * mismatches CPython does not see and, where it narrows a type, can erase ones
 * it does.  `field` is `dict.get`, so absent and JSON `null` collapse to `None`
 * exactly as they do in CPython.
 *
 * Note the asymmetry with `act`: here an expected `None` does **not** disable
 * the check — the set is then just `{None}`, so any non-null echo is a mismatch.
 *
 * The echo is projected back to a plain JSON value first: CPython's `!=` reads
 * `180 == 180.0` as equal, so the identity guard must not see the float
 * spelling the printer is careful to keep.
 */
const echoBelongsElsewhere = (response: PyObject, session: JsonObject, key: string): boolean => {
  const echoed = pyField(response, key);
  if (echoed === null) return false;
  const expected: JsonValue = field(session, key);
  return compactJson(pyToJson(echoed)) !== compactJson(expected);
};

/**
 * `str(value)` for a Python number, which JavaScript does not agree with.
 *
 * `--wait-s` is `type=float`, so a supplied `120` reaches `urlencode` as
 * `120.0` and the query string reads `wait_s=120.0`.  The *default* is the
 * literal `int` 120 that argparse never ran through `type`, so an omitted flag
 * sends `wait_s=120`.  Both bytes are real on the Python's wire and the
 * supervisor's query parser is the only thing that ever sees them, so the port
 * reproduces the difference rather than normalizing it away.
 */
export const pythonFloatText = (value: number): string =>
  Number.isInteger(value) && Number.isFinite(value)
    ? `${Object.is(value, -0) ? '-0' : String(value)}.0`
    : String(value);

/** The default `--wait-s` argparse never coerced: an `int`, printed as one. */
export const DEFAULT_WAIT_S = 120;

export const nextCommand = Command.make(
  'next',
  {
    session: Options.text('session').pipe(
      Options.withDefault(''),
      Options.withDescription('the private session file this command acts against')
    ),
    afterTurn: dualInteger('after-turn'),
    waitS: dualFloat('wait-s'),
  },
  ({
    session,
    afterTurn,
    waitS,
  }): Effect.Effect<void, PlayerError | SessionMissingError, SessionStore> =>
    Effect.gen(function* () {
      const sessions = yield* SessionStore;
      const http = yield* v1Json;
      const loaded = yield* sessions.resolve(session);
      if (loaded.session.controlProtocol !== STRATEGIC_V1) {
        return yield* Effect.fail(
          playerError(
            'just next is strategic-v1 only; this full-control-v2 session ' +
              'uses `just health`, `just state`, and `just legal`'
          )
        );
      }
      const after = yield* resolveDual('after-turn', afterTurn, 0);
      const supplied = yield* resolveDualOption('wait-s', waitS);
      const wait = Option.getOrElse(supplied, () => DEFAULT_WAIT_S);
      if (after < 0 || !(wait >= 0 && wait <= 300)) {
        return yield* Effect.fail(
          playerError('after-turn must be >= 0 and wait-s must be in [0, 300]')
        );
      }
      const waitText = Option.isSome(supplied)
        ? pythonFloatText(supplied.value)
        : String(DEFAULT_WAIT_S);
      const query = `after_turn=${after}&wait_s=${encodeURIComponent(waitText)}`;
      const value = yield* http.requestPyJson(
        'GET',
        `${loaded.session.serviceUrl}/v1/games/${loaded.session.gameId}/me/next?${query}`,
        {
          token: loaded.session.agentToken,
          // The poll itself is the timeout: five seconds of slack over the
          // window the supervisor was asked to hold the connection open.
          timeout: Math.max(10, wait + 5),
        }
      );
      if (echoBelongsElsewhere(value, loaded.session.raw, 'game_id')) {
        return yield* Effect.fail(playerError('the next response belongs to a different game'));
      }
      if (echoBelongsElsewhere(value, loaded.session.raw, 'agent_id')) {
        return yield* Effect.fail(
          playerError('the next response belongs to a different agent seat')
        );
      }
      yield* printPyJson(value);
      const state = pyField(value, 'state');
      if (typeof state === 'string' && TERMINAL_STATES.has(state)) {
        // stdout stays pure JSON for the machine consumer; the human-facing
        // "stop the play loop" hint goes to stderr, leading blank line and all.
        yield* Console.error(`\nGame is ${state}; stop the play loop.`);
      }
    })
);
