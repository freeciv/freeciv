/**
 * `play help` — the agent-facing play card.
 *
 * Folds in the justfile recipe `help: @cat docs/play.md` (justfile:396-399).
 * `docs/commands.md` is the harness-author reference and is deliberately *not*
 * printed here: every doc character an agent reads is part of its per-turn
 * budget.
 *
 * `cat` emits the file and nothing else, so this must too — the card constant
 * carries the file without its single trailing newline and `echo`
 * (`Console.log`) puts that newline back.
 */
import { Command } from '@effect/cli';
import type { Effect } from 'effect';
import { PLAY_CARD } from 'src/docs/play-card';
import { echo } from 'src/render/primitives';

export const helpCommand = Command.make('help', {}, (): Effect.Effect<void> => echo(PLAY_CARD));
