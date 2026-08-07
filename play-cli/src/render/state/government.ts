/**
 * The `governments` section's one extra line.
 *
 * Ports `_government_scope_lines` (client.py:5281-5300) and, because it is the
 * only caller that has landed, `_player_scope_alias` (8030-8039).
 *
 * The section reports that a change is possible and then names no way to make
 * one, because `government.*` is enumerated only inside this seat's own player
 * scope and never in the global catalog — so an agent that reads `can_change
 * yes` and searches for the action by kind finds nothing and concludes the
 * rules forbid it.  This line closes that loop.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject, type JsonValue } from 'src/render/primitives';
import { aliasMap } from 'src/services/aliases';
import { cachedDescriptors } from 'src/services/catalog-cache';
import type { V2ClientState } from 'src/services/session-store';

/**
 * Name this seat's own player actor from whatever the cache already holds.
 *
 * PORT_MAP §0 lists `_player_scope_alias` on U11's row (it sits inside the
 * 7817-8287 span), but `_government_scope_lines` is defined in terms of it and
 * U10 lands first; it is exported here so U11 imports it rather than declaring
 * a second copy.  See PORT_MAP §7 and NOTES §U10.
 */
export const playerScopeAlias = (
  state: V2ClientState
): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const aliases = yield* aliasMap(state);
    for (const descriptor of cachedDescriptors(state)) {
      const subject = descriptor['subject'] ?? null;
      const actor = isJsonObject(subject) ? (subject['actor'] ?? null) : null;
      const actorId = isJsonObject(actor) ? (actor['id'] ?? null) : null;
      if (typeof actorId === 'string' && actorId.startsWith('player_')) {
        return aliases[actorId] ?? actorId;
      }
    }
    return 'YOUR_PLAYER_ID';
  });

/** Point a `can_change=yes` government row at the catalog that proves it. */
export const governmentScopeLines = (
  items: ReadonlyArray<JsonValue>,
  state: V2ClientState | null
): Effect.Effect<ReadonlyArray<string>, PlayerError> => {
  const changeable = items.some(
    (item) => isJsonObject(item) && item['can_change'] === true
  );
  if (state === null || !changeable) return Effect.succeed([]);
  return Effect.map(playerScopeAlias(state), (alias) => [
    `government actions are player-scoped: just legal --actor_id ${alias} --all`,
  ]);
};
