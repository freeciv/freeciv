/**
 * The `city_citizens` section.
 *
 * Ports `_render_city_citizens` (client.py:5024-5065).
 *
 * The per-tile table was already legible; what it never said is what the
 * citizens currently on those tiles add up to — the one number a tile swap is
 * judged against, and the number `city_detail` reports as `base`.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { drift, isJsonObject, scalar, type AliasMap, type JsonValue } from 'src/render/primitives';
import { renderGenericItems } from 'src/render/state/generic';

export const renderCityCitizens = (
  items: ReadonlyArray<JsonValue>,
  aliases: AliasMap | null = null
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    void aliases;
    const worked = items.filter(
      (item) => isJsonObject(item) && item['kind'] === 'tile' && item['worked'] === true
    );
    const totals = new Map<string, number>();
    for (const item of worked) {
      const yields = isJsonObject(item) ? (item['yields'] ?? null) : null;
      if (!isJsonObject(yields)) return yield* Effect.fail(drift('city citizen yields'));
      for (const [key, value] of Object.entries(yields)) {
        if (typeof value !== 'number' || !Number.isInteger(value)) {
          return yield* Effect.fail(drift('city citizen yields'));
        }
        totals.set(key, (totals.get(key) ?? 0) + value);
      }
    }
    const lines: string[] = [];
    if (worked.length > 0) {
      const named = [...totals.entries()]
        .filter(([, value]) => value !== 0)
        .map(([key, value]) => `${key.slice(0, 1)}${value}`)
        .join(' ');
      lines.push(
        `worked ${worked.length} of ${items.length} rows on this page: ` +
          (named === '' ? 'nothing' : named)
      );
    }
    const placed = items.filter((item) => {
      if (!isJsonObject(item) || item['kind'] !== 'specialist') return false;
      const count = item['count'] ?? null;
      return typeof count === 'number' && Number.isInteger(count) && count > 0;
    });
    if (placed.length > 0) {
      lines.push(
        'specialists: ' +
          placed
            .map((item) => {
              const fields = isJsonObject(item) ? item : {};
              return `${scalar(fields['count'] ?? null)} ${scalar(fields['name'] ?? null)}`;
            })
            .join(', ')
      );
    }
    return [...lines, ...renderGenericItems(items)];
  });
