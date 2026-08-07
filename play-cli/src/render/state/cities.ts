/**
 * The `cities` state section.
 *
 * Ports `_city_row` (client.py:4329-4361) and `_render_cities` (4364-4373).
 *
 * The rush-buy price rides the city row because it rides nothing else: the
 * `city.buy_production` action carries no gold cost, so a seat that has not
 * read this row is quoting from memory when the emergency starts.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import {
  coordinates,
  isJsonObject,
  named,
  needInt,
  needText,
  rowAlias,
  scalar,
  signed,
  table,
  type AliasMap,
  type JsonValue,
} from 'src/render/primitives';

export interface CityRowOptions {
  readonly showId?: boolean;
}

/** `{f}{+d}` for every output the surplus block names, in its own order. */
export const surplusText = (surplus: JsonValue): string => {
  if (!isJsonObject(surplus)) return '';
  return Object.entries(surplus)
    .map(([key, value]) => `${key.slice(0, 1)}${signed(value ?? null)}`)
    .join(' ');
};

export const cityRow = (
  alias: string,
  item: JsonValue,
  options: CityRowOptions = {}
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    const showId = options.showId ?? true;
    const detail: string[] = [];
    const place = yield* coordinates(item);
    if (place !== null) detail.push(place);
    detail.push(`sz${yield* needInt(item, 'size', 'city')}`);
    const fields = isJsonObject(item) ? item : {};
    const production = fields['production'] ?? null;
    if (isJsonObject(production)) {
      let text = named(production);
      if ('shield_stock' in production && 'shield_cost' in production) {
        text += ` ${scalar(production['shield_stock'] ?? null)}/${scalar(
          production['shield_cost'] ?? null
        )}`;
      }
      detail.push(text);
    }
    const surplus = fields['surplus'] ?? null;
    if (isJsonObject(surplus)) detail.push(surplusText(surplus));
    if (isJsonObject(production) && production['can_buy'] === true) {
      detail.push(`buy=${scalar(production['buy_cost'] ?? null)}`);
    }
    const row = [alias, yield* needText(item, 'name', 'city'), detail.join(' ')];
    if (showId) row.push(yield* needText(item, 'id', 'city'));
    return row;
  });

export const renderCities = (
  items: ReadonlyArray<JsonValue>,
  aliases: AliasMap | null = null
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.map(
    Effect.forEach(items, (item, index) =>
      Effect.flatMap(rowAlias(aliases, item, 'id', 'c', index + 1), (alias) =>
        cityRow(alias, item, { showId: aliases === null })
      )
    ),
    (rows) => table(rows)
  );
