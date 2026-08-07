/**
 * The `diplomacy` section.
 *
 * Ports `_meeting_summary` (client.py:4958-4975) and `_render_diplomacy`
 * (4978-5021).
 *
 * A relation whose meeting is open is the one row that carries a decision, so
 * it says so on the row rather than in a `meeting.clauses_token` column the
 * generic flattener would print beside four opaque IDs.  The clause list is the
 * only thing this page does not carry, so it is named as the command.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import {
  isJsonObject,
  needText,
  rowAlias,
  scalar,
  table,
  type AliasMap,
  type JsonValue,
} from 'src/render/primitives';

/** Say what an open meeting is waiting on, in the words the seat needs. */
export const meetingSummary = (meeting: JsonValue): string => {
  if (!isJsonObject(meeting)) return '';
  let text = '!meeting open';
  const count = meeting['clause_count'] ?? null;
  if (typeof count === 'number' && Number.isInteger(count)) text += ` ${count} clauses`;
  const accepted = (
    [
      ['you', 'self_accepted'],
      ['them', 'other_accepted'],
    ] as const
  )
    .filter(([, key]) => meeting[key] === true)
    .map(([side]) => side);
  if (accepted.length > 0) text += ` accepted by ${accepted.join('+')}`;
  if (meeting['self_accepted'] !== true) text += ', awaiting you';
  return text;
};

export const renderDiplomacy = (
  items: ReadonlyArray<JsonValue>,
  aliases: AliasMap | null = null
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    const rows: Array<ReadonlyArray<string>> = [];
    const drilldowns: string[] = [];
    for (const [index, item] of items.entries()) {
      const fields = isJsonObject(item) ? item : {};
      const alias = yield* rowAlias(aliases, item, 'relation_id', 'r', index + 1);
      const detail: string[] = [];
      const nation = fields['nation'] ?? null;
      if (typeof nation === 'string' && nation !== '') detail.push(`(${nation})`);
      detail.push(scalar(fields['state'] ?? null));
      if (fields['has_embassy'] === true) detail.push('embassy');
      const turns = fields['treaty_turns_left'] ?? null;
      if (typeof turns === 'number' && Number.isInteger(turns)) detail.push(`${turns}t left`);
      if (fields['alive'] === false) detail.push('!dead');
      const summary = meetingSummary(fields['meeting'] ?? null);
      if (summary !== '') detail.push(`· ${summary}`);
      const row = [alias, yield* needText(item, 'player_name', 'relation'), detail.join(' ')];
      if (aliases === null) row.push(yield* needText(item, 'relation_id', 'relation'));
      rows.push(row);
      if (summary !== '') {
        const handle = aliases !== null ? alias : scalar(fields['relation_id'] ?? null);
        drilldowns.push(
          `clauses: just state --section diplomacy_clauses --relation_id ${handle}`
        );
      }
    }
    return [...table(rows), ...drilldowns];
  });
