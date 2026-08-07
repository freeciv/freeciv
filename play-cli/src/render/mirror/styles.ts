/**
 * `cache/styles.tsv` — the static city-style catalog.
 *
 * Ports `play/state_mirror.py:631-640` (`_render_styles`).
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject } from 'src/schema/primitives';
import { cell, dig, mirrorError, type MirrorAliases } from 'src/services/mirror';
import type { RenderedSection } from 'src/render/mirror/section';

export const STYLE_COLUMNS: ReadonlyArray<string> = ['id', 'style'];

/** `_render_styles` — project a `pregame_styles` page. */
export const renderStyles = (
  items: ReadonlyArray<unknown>,
  _aliases?: MirrorAliases | null
): Effect.Effect<RenderedSection, PlayerError> => {
  const rows: Array<ReadonlyArray<string>> = [];
  for (const item of items) {
    if (!isJsonObject(item)) {
      return Effect.fail(mirrorError('style item is not an object'));
    }
    rows.push([cell(dig(item, 'id')), cell(dig(item, 'name'))]);
  }
  return Effect.succeed({ columns: STYLE_COLUMNS, rows });
};
