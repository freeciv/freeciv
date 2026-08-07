/**
 * `state/cities.tsv` — one row per city.
 *
 * Ports `play/state_mirror.py:577-612` (`_render_cities`).
 *
 * `buy` is a price only when the payload says the purchase is legal: a
 * `buy_cost` the server carries alongside `can_buy: false` is a quote for an
 * order that would be refused, so the cell stays `-`.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject } from 'src/schema/primitives';
import {
  aliasMap,
  cell,
  dig,
  mirrorError,
  pair,
  position,
  type MirrorAliases,
} from 'src/services/mirror';
import type { RenderedSection } from 'src/render/mirror/section';

export const CITY_COLUMNS: ReadonlyArray<string> = [
  'alias',
  'city',
  'pos',
  'size',
  'building',
  'shields',
  'surplus',
  'buy',
];

const surplusText = (surplus: unknown): string =>
  isJsonObject(surplus)
    ? `f${cell(dig(surplus, 'food'))} s${cell(dig(surplus, 'shields'))} t${cell(
        dig(surplus, 'trade')
      )}`
    : '-';

/** `_render_cities` — project a cities page. */
export const renderCities = (
  items: ReadonlyArray<unknown>,
  aliases?: MirrorAliases | null
): Effect.Effect<RenderedSection, PlayerError> => {
  const names = aliasMap(
    'c',
    items.map((item) => dig(item, 'id')),
    aliases
  );
  const rows: Array<ReadonlyArray<string>> = [];
  for (const item of items) {
    if (!isJsonObject(item)) {
      return Effect.fail(mirrorError('city item is not an object'));
    }
    const id = cell(dig(item, 'id'));
    rows.push([
      names.get(id) ?? id,
      cell(dig(item, 'name')),
      position(item),
      cell(dig(item, 'size')),
      cell(dig(item, 'production', 'name')),
      pair(dig(item, 'production', 'shield_stock'), dig(item, 'production', 'shield_cost')),
      surplusText(dig(item, 'surplus')),
      dig(item, 'production', 'can_buy') === true
        ? cell(dig(item, 'production', 'buy_cost'))
        : '-',
    ]);
  }
  return Effect.succeed({ columns: CITY_COLUMNS, rows });
};
