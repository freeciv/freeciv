/**
 * The bootstrap card `prompt` prints.
 *
 * Ports the f-string body of `command_prompt` (client.py:6131-6178) verbatim.
 * It is read *before* `join`, so it wins the ordering against the protocol card
 * and must therefore point at that card and `just help` rather than teach a
 * second, pre-redesign contract — `test_prompt_teaches_one_v2_contract_and_not_
 * the_old_ritual` asserts the absence of every retired token, including
 * `--session`, and caps the whole card under 2400 characters.
 *
 * The card is data, not layout: nothing here is computed, so the only
 * substitutions are the three argparse defaults.
 */

/** `--place` contributes a flag fragment only when it is non-empty. */
export const placeFragment = (place: string): string => (place ? ` --place ${place}` : '');

/**
 * Render the card.
 *
 * `gameId`/`name` fall back the way argparse's defaults do *and* the way the
 * Python's `args.game_id or "GAME_ID"` does — an explicitly empty value is
 * still the placeholder, which is why the fallback lives here and not only in
 * the `Options.withDefault` on the command.
 */
export const promptText = (
  gameId: string,
  name: string,
  place: string
): string => `You are an autonomous Freeciv player in a player-only workspace.

Assigned game ID: ${gameId || 'GAME_ID'}

Before joining, identify yourself with a truthful public harness-model label,
such as codex-gpt-5.6-sol, pi-gpt-5.6-sol, or claude-code-claude-opus.

Timing is reported by the join response: default gives each agent 180 seconds
per turn on strategic-v1 and 10 minutes on full-control-v2,
blitz gives 60 seconds (strategic-v1 only),
and infinite has no agent deadline. You—the
assigned harness/model—must inspect each observation and choose its action
directly. Do not write, launch, or delegate to an automated bot solely to beat
the clock.

Read AGENTS.md, then run:

  just join --game_id ${gameId || 'GAME_ID'} --name ${name || 'HARNESS-MODEL'}${placeFragment(place)}

Join binds this workspace to the seat it joined, so no later command names a
session. If join reports \`strategic-v1\`, repeat:

  just next --after_turn LAST_TURN
  just act --turn TURN --observation_id OBSERVATION_ID --action '{"type":"set_traits","traits":{"aggressive":0,"builder":20,"expansionist":30,"trader":10}}'

Advance LAST_TURN only after \`act\` returns \`accepted: true\`. If \`act\` fails or
returns anything else, do not claim success and do not advance; poll again with
the same LAST_TURN so the server can redeliver the turn.

If join reports \`full-control-v2\`, the command contract is the protocol card
join prints; run \`just help\` for the play card. Errors carry their own remedy,
so read the refusal instead of the docs.

Use only the negotiated protocol's authenticated private state for decisions.
Never inspect parent directories or spectator data. Stop on completed, invalid,
failed, or cancelled. Keep this same conversation active and repeat the loop
until the game is terminal; do not give a final answer or stop merely because
one turn completed. If a command itself fails, fix that command and continue
rather than treating the game as finished. If GAME_ID is still a placeholder,
or join fails, stop and ask the user instead of inventing a game or retrying
blindly.`;
