/**
 * `cache/nations.tsv` — the static nation catalog, fetched once at join.
 *
 * Ports `play/state_mirror.py:615-628` (`_render_nations`).
 *
 * `just start --nation …` matches its argument against this file, so the `id`
 * column is the wire id the pregame command sends, never a display name.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject } from 'src/schema/primitives';
import { cell, dig, mirrorError, type MirrorAliases } from 'src/services/mirror';
import type { RenderedSection } from 'src/render/mirror/section';

export const NATION_COLUMNS: ReadonlyArray<string> = ['id', 'nation', 'default_style_id'];

/** `_render_nations` — project a `pregame_nations` page. */
export const renderNations = (
  items: ReadonlyArray<unknown>,
  _aliases?: MirrorAliases | null
): Effect.Effect<RenderedSection, PlayerError> => {
  const rows: Array<ReadonlyArray<string>> = [];
  for (const item of items) {
    if (!isJsonObject(item)) {
      return Effect.fail(mirrorError('nation item is not an object'));
    }
    rows.push([
      cell(dig(item, 'id')),
      cell(dig(item, 'name')),
      cell(dig(item, 'default_style_id')),
    ]);
  }
  return Effect.succeed({ columns: NATION_COLUMNS, rows });
};
