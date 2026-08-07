/**
 * `play prompt` — the bootstrap card an operator pastes into a fresh agent.
 *
 * Ports `command_prompt` (client.py:6131-6178).  It touches nothing: no
 * session, no socket, no state.  Three defaults in, one card out, status 0.
 *
 * `--game_id` is the spelling the justfile veneer exposed (`[arg("game_id",
 * long)]`, justfile:70-72) and the one the card itself teaches, so both
 * spellings are accepted here — PLAN.md §3 rule 3.
 */
import { Command, Options } from '@effect/cli';
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { dualText, resolveDual } from 'src/options';
import { echo } from 'src/render/primitives';
import { promptText } from 'src/render/prompt-text';

export const promptCommand = Command.make(
  'prompt',
  {
    gameId: dualText('game-id'),
    name: Options.text('name').pipe(Options.withDefault('HARNESS-MODEL')),
    place: Options.text('place').pipe(Options.withDefault('')),
  },
  ({ gameId, name, place }): Effect.Effect<void, PlayerError> =>
    Effect.flatMap(resolveDual('game-id', gameId, 'GAME_ID'), (game) =>
      // `promptText` re-applies the `or "GAME_ID"` fallback, so an explicitly
      // empty `--game-id ''` still renders the placeholder, exactly as
      // `args.game_id or "GAME_ID"` does.
      echo(promptText(game, name, place))
    )
);
