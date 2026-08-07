/**
 * The `research` state section.
 *
 * Ports `_render_research` (client.py:4376-4406).
 *
 * A technology never receives an entity alias, so no positional column is
 * printed: a bare `3` in column 0 would resolve nowhere, and the name is
 * exactly what `research set_target NAME` accepts.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import {
  isJsonObject,
  needText,
  scalar,
  table,
  type AliasMap,
  type JsonValue,
} from 'src/render/primitives';

export const renderResearch = (
  items: ReadonlyArray<JsonValue>,
  aliases: AliasMap | null = null
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.map(
    Effect.forEach(items, (item) =>
      Effect.gen(function* () {
        const fields = isJsonObject(item) ? item : {};
        const detail: string[] = [];
        const cost = fields['path_cost'] ?? null;
        if (cost !== null) detail.push(`path${scalar(cost)}`);
        if (fields['can_target'] === true) detail.push('targetable');
        if (fields['can_goal'] === true) detail.push('goalable');
        const unknown = fields['unknown_prerequisite_count'] ?? null;
        if (typeof unknown === 'number' && Number.isInteger(unknown) && unknown !== 0) {
          detail.push(`needs${unknown}`);
        }
        const row = [
          yield* needText(item, 'name', 'research'),
          yield* needText(item, 'state', 'research'),
          detail.join(' '),
        ];
        if (aliases === null) row.push(yield* needText(item, 'id', 'research'));
        return row;
      })
    ),
    (rows) => table(rows)
  );
