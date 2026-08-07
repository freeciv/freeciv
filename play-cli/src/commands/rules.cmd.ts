/**
 * `play rules` — the static gameplay rules and negotiated-protocol guidance.
 *
 * Folds in the justfile recipe `rules: @cat docs/gameplay.md`
 * (justfile:401-403), byte for byte, on the same terms as `help`.
 */
import { Command } from '@effect/cli';
import type { Effect } from 'effect';
import { GAMEPLAY_RULES } from 'src/docs/gameplay-rules';
import { echo } from 'src/render/primitives';

export const rulesCommand = Command.make(
  'rules',
  {},
  (): Effect.Effect<void> => echo(GAMEPLAY_RULES)
);
