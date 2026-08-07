/**
 * `play result` — the public, unauthenticated end-of-game result.
 *
 * Ports `command_result` (client.py:11581-11594).  It is the one command that
 * takes no session at all: the game ID arrives positionally or as `--game-id`,
 * and the request carries no bearer token, because a finished game's result is
 * public.  Third of the three `V2_JSON_ONLY_COMMANDS`, so the output is always
 * the `_print_json` shape.
 *
 * `just result GAME_ID` and `just result --game_id GAME_ID` are both real
 * (justfile:388-392, `test_v2_result_accepts_positional_or_named_id_and_state_
 * hints`), so both spellings of the flag are accepted alongside the positional.
 * Giving the positional and the flag *different* IDs is a refusal rather than a
 * silent precedence rule — there is no reading of that command line that is not
 * a mistake.
 */
import { Args, Command } from '@effect/cli';
import { Effect, Option } from 'effect';
import { GAME_ID_RE } from 'src/constants';
import { PlayerError, playerError } from 'src/errors';
import { dualText, resolveDual } from 'src/options';
import { printPyJson, v1Json } from 'src/services/v1-json';
import { serviceUrl } from 'src/services/http';

/** `_game_id` — a valid assigned game ID, or a refusal naming what is wrong. */
const validGameId = (value: string): Effect.Effect<string, PlayerError> =>
  GAME_ID_RE.test(value)
    ? Effect.succeed(value)
    : Effect.fail(playerError('a valid assigned game ID is required'));

export const resultCommand = Command.make(
  'result',
  {
    gameIdPositional: Args.text({ name: 'game_id' }).pipe(Args.optional),
    gameId: dualText('game-id'),
  },
  ({ gameIdPositional, gameId }): Effect.Effect<void, PlayerError> =>
    Effect.gen(function* () {
      const positional = Option.getOrElse(gameIdPositional, () => '').trim();
      const option = (yield* resolveDual('game-id', gameId, '')).trim();
      if (positional !== '' && option !== '' && positional !== option) {
        return yield* Effect.fail(playerError('result received two different game IDs'));
      }
      const game = yield* validGameId(option || positional);
      const http = yield* v1Json;
      const base = yield* serviceUrl();
      const value = yield* http.requestPyJson('GET', `${base}/v1/games/${game}/result`, {
        timeout: 10,
      });
      yield* printPyJson(value);
    })
);
