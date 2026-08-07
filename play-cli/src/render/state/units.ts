/**
 * The `units` state section.
 *
 * Ports `_route_summary` (client.py:4249-4271), `_unit_row` (4274-4312) and
 * `_render_units` (4315-4326).
 *
 * With an alias cache in hand the alias column already addresses the unit, so
 * the opaque `unit_<32hex>` never enters agent context; `--json` still carries
 * it.  That is why `show_id` is `aliases === null` and not a flag of its own.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import {
  coordinates,
  isJsonObject,
  needText,
  rowAlias,
  scalar,
  table,
  type AliasMap,
  type JsonValue,
} from 'src/render/primitives';

/**
 * Summarize a unit's standing route from fields already ingested.
 *
 * `→(40,60) 3st` reads as "walking to 40,60, three path steps left".  The count
 * is what `unit.route` already carries — there is no turn estimate on the wire,
 * so none is invented.  A path the server could not reconstruct falls back to
 * the queued order count, which is still a real number of steps left, and
 * prints `?st` only when the payload names neither.
 */
export const routeSummary = (route: JsonValue): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    if (!isJsonObject(route)) return '';
    const destination = (yield* coordinates(route['destination'] ?? null)) ?? '?';
    const declared = route['path_step_count'] ?? null;
    const count =
      route['path_available'] === false || declared === null
        ? (route['order_count'] ?? null)
        : declared;
    const steps = count === null ? '?' : scalar(count);
    let text = `→(${destination.replace(/^@+/, '')}) ${steps}st`;
    const mode = route['mode'] ?? null;
    if (mode !== null && mode !== 'goto') text += ` ${scalar(mode)}`;
    if (route['vigilant'] === true) text += ' vigilant';
    return text;
  });

export interface UnitRowOptions {
  readonly showId?: boolean;
}

export const unitRow = (
  alias: string,
  item: JsonValue,
  options: UnitRowOptions = {}
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    const showId = options.showId ?? true;
    const detail: string[] = [];
    const place = yield* coordinates(item);
    if (place !== null) detail.push(place);
    const fields = isJsonObject(item) ? item : {};
    const stats = fields['type_stats'] ?? null;
    if ('moves' in fields) {
      const rate =
        isJsonObject(stats) && 'move_rate' in stats ? scalar(stats['move_rate'] ?? null) : '?';
      detail.push(`mv${scalar(fields['moves'] ?? null)}/${rate}`);
    }
    if ('hp' in fields) {
      const maximum =
        isJsonObject(stats) && 'max_hp' in stats ? scalar(stats['max_hp'] ?? null) : '?';
      detail.push(`hp${scalar(fields['hp'] ?? null)}/${maximum}`);
    }
    const activity = fields['activity'] ?? null;
    if (isJsonObject(activity) && 'name' in activity) {
      detail.push(scalar(activity['name'] ?? null));
    }
    const summary = yield* routeSummary(fields['route'] ?? null);
    if (summary !== '') detail.push(summary);
    const automation = fields['automation'] ?? null;
    if (isJsonObject(automation)) {
      const controller = automation['controller'] ?? null;
      if (controller !== null && controller !== 'player') {
        detail.push(`!controller=${scalar(controller)}`);
      }
    }
    const scope = fields['scope'] ?? null;
    if (typeof scope === 'string' && scope !== 'own') detail.push(`scope=${scope}`);
    const row = [alias, yield* needText(item, 'type', 'unit'), detail.join(' ')];
    if (showId) row.push(yield* needText(item, 'id', 'unit'));
    return row;
  });

export const renderUnits = (
  items: ReadonlyArray<JsonValue>,
  aliases: AliasMap | null = null
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.map(
    Effect.forEach(items, (item, index) =>
      Effect.flatMap(rowAlias(aliases, item, 'id', 'u', index + 1), (alias) =>
        unitRow(alias, item, { showId: aliases === null })
      )
    ),
    (rows) => table(rows)
  );
