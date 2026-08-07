/**
 * `cache/governments.tsv` — the government catalog and where this seat stands
 * in it.
 *
 * Ports `play/state_mirror.py:687-702` (`_render_governments`).
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject } from 'src/schema/primitives';
import { cell, dig, mirrorError, type MirrorAliases } from 'src/services/mirror';
import type { RenderedSection } from 'src/render/mirror/section';

export const GOVERNMENT_COLUMNS: ReadonlyArray<string> = [
  'id',
  'government',
  'current',
  'target',
  'can_change',
];

/** `_render_governments` — project a `governments` page. */
export const renderGovernments = (
  items: ReadonlyArray<unknown>,
  _aliases?: MirrorAliases | null
): Effect.Effect<RenderedSection, PlayerError> => {
  const rows: Array<ReadonlyArray<string>> = [];
  for (const item of items) {
    if (!isJsonObject(item)) {
      return Effect.fail(mirrorError('government item is not an object'));
    }
    rows.push([
      cell(dig(item, 'id')),
      cell(dig(item, 'name')),
      cell(dig(item, 'current')),
      cell(dig(item, 'target')),
      cell(dig(item, 'can_change')),
    ]);
  }
  return Effect.succeed({ columns: GOVERNMENT_COLUMNS, rows });
};
