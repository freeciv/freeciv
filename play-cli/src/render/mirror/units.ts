/**
 * `state/units.tsv` — one row per unit.
 *
 * Ports `play/state_mirror.py:529-574` (`_render_units`).
 *
 * Column 0 is the alias the CLI's own tables print, so `grep '^u1'` here and
 * `just legal --actor_id u1` name the same unit.  Without an alias map the row
 * falls back to a stable display handle (`u.3f9a2c`), which is deliberately not
 * a name any command accepts: it fails closed exactly like a stale alias.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject } from 'src/schema/primitives';
import {
  MISSING,
  aliasMap,
  cell,
  dig,
  mirrorError,
  pair,
  position,
  type MirrorAliases,
} from 'src/services/mirror';
import type { RenderedSection } from 'src/render/mirror/section';

export const UNIT_COLUMNS: ReadonlyArray<string> = [
  'alias',
  'unit',
  'who',
  'pos',
  'moves',
  'hp',
  'activity',
  'orders',
];

/**
 * The same route summary the CLI's unit tables print, so a row read here and a
 * row read there say the same thing: where the unit is walking and how many
 * path steps of it are left.
 */
const routeOrders = (route: unknown): string => {
  if (!isJsonObject(route)) return '-';
  const destination = dig(route, 'destination');
  const where = isJsonObject(destination) ? position(destination) : '?';
  const steps = dig(route, 'path_step_count');
  const count =
    dig(route, 'path_available') === false || steps === MISSING
      ? dig(route, 'order_count')
      : steps;
  const summary = `→(${where}) ${count === MISSING ? '?' : cell(count)}st`;
  const mode = dig(route, 'mode');
  return mode === MISSING || mode === null || mode === 'goto'
    ? summary
    : `${summary} ${cell(mode)}`;
};

/** `activity.name`, suffixed with its target when the payload names one. */
const activityText = (item: unknown): string => {
  const name = cell(dig(item, 'activity', 'name'));
  const target = dig(item, 'activity', 'target', 'name');
  return name !== '-' && target !== MISSING && target !== null
    ? `${name}:${cell(target)}`
    : name;
};

/** `_render_units` — project a units page. */
export const renderUnits = (
  items: ReadonlyArray<unknown>,
  aliases?: MirrorAliases | null
): Effect.Effect<RenderedSection, PlayerError> => {
  const names = aliasMap(
    'u',
    items.map((item) => dig(item, 'id')),
    aliases
  );
  const rows: Array<ReadonlyArray<string>> = [];
  for (const item of items) {
    if (!isJsonObject(item)) {
      return Effect.fail(mirrorError('unit item is not an object'));
    }
    const id = cell(dig(item, 'id'));
    rows.push([
      names.get(id) ?? id,
      cell(dig(item, 'type')),
      cell(dig(item, 'scope')),
      position(item),
      pair(dig(item, 'moves'), dig(item, 'type_stats', 'move_rate')),
      pair(dig(item, 'hp'), dig(item, 'type_stats', 'max_hp')),
      activityText(item),
      routeOrders(dig(item, 'route')),
    ]);
  }
  return Effect.succeed({ columns: UNIT_COLUMNS, rows });
};
