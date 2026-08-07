/**
 * `state/diplomacy.tsv` — one row per relation, with any open meeting spelled
 * out.
 *
 * Ports `play/state_mirror.py:643-684` (`_render_diplomacy`).
 *
 * The meeting columns are separate and literal rather than one prose cell
 * because the decisions projection reads them back: `open` plus a clause count
 * plus who has accepted is exactly what decides whether this relation is still
 * waiting on the seat.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject } from 'src/schema/primitives';
import {
  aliasMap,
  cell,
  dig,
  mirrorError,
  type MirrorAliases,
} from 'src/services/mirror';
import type { RenderedSection } from 'src/render/mirror/section';

export const DIPLOMACY_COLUMNS: ReadonlyArray<string> = [
  'alias',
  'player',
  'nation',
  'state',
  'embassy',
  'meeting',
  'clauses',
  'accepted',
];

/** Which sides have accepted the open meeting, in CPython's order. */
const acceptedText = (meeting: unknown): string => {
  const sides = (
    [
      ['you', 'self_accepted'],
      ['them', 'other_accepted'],
    ] as const
  ).flatMap(([side, key]) => (dig(meeting, key) === true ? [side] : []));
  return sides.length === 0 ? 'neither' : sides.join('+');
};

/** `_render_diplomacy` — project a diplomacy page. */
export const renderDiplomacy = (
  items: ReadonlyArray<unknown>,
  aliases?: MirrorAliases | null
): Effect.Effect<RenderedSection, PlayerError> => {
  const names = aliasMap(
    'r',
    items.map((item) => dig(item, 'relation_id')),
    aliases
  );
  const rows: Array<ReadonlyArray<string>> = [];
  for (const item of items) {
    if (!isJsonObject(item)) {
      return Effect.fail(mirrorError('diplomacy item is not an object'));
    }
    const meeting = dig(item, 'meeting');
    const open = isJsonObject(meeting);
    const id = cell(dig(item, 'relation_id'));
    rows.push([
      names.get(id) ?? id,
      cell(dig(item, 'player_name')),
      cell(dig(item, 'nation')),
      cell(dig(item, 'state')),
      cell(dig(item, 'has_embassy')),
      open ? 'open' : '-',
      open ? cell(dig(meeting, 'clause_count')) : '-',
      open ? acceptedText(meeting) : '-',
    ]);
  }
  return Effect.succeed({ columns: DIPLOMACY_COLUMNS, rows });
};
