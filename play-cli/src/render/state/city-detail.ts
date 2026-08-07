/**
 * The `city_detail` section.
 *
 * Ports `_city_granary_text` (client.py:4764-4792), `_city_citizens_text`
 * (4795-4825), `_city_production_lines` (4828-4866), `_city_management_lines`
 * (4869-4896), `_city_drilldown_lines` (4899-4917) and `_render_city_detail`
 * (4920-4955).
 *
 * `city_detail` used to fall through to the generic flattener, which renders a
 * nested object as `…`: a whole 596-turn game was played against
 * `outputs.food=… outputs.shields=…`, so deciding whether a tile swap paid for
 * itself cost several more reads and some experimental citizen assignments.
 * Every one of those numbers is already in the page.  This renders them.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { V2_CITY_DRILLDOWNS } from 'src/constants';
import {
  coordinates,
  drift,
  isJsonObject,
  named,
  needInt,
  needText,
  packedLines,
  rowAlias,
  scalar,
  table,
  type AliasMap,
  type JsonValue,
} from 'src/render/primitives';
import { surplusText } from 'src/render/state/cities';
import { cityOutputRows } from 'src/render/state/city-outputs';

const isInteger = (value: JsonValue): value is number =>
  typeof value === 'number' && Number.isInteger(value);

/**
 * Say where the food is going, in the words the growth counter means.
 *
 * `growth_turns` is `city_turns_to_grow()`: positive turns to the next citizen,
 * `0` when the granary is full but the city may not grow, negative turns until
 * famine, and absent when the food surplus is exactly zero.
 */
export const cityGranaryText = (
  item: JsonValue,
  surplus: JsonValue
): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const fields = isJsonObject(item) ? item : {};
    const storage = fields['food_storage'] ?? null;
    if (!isJsonObject(storage)) return '';
    const parts = [
      `granary ${scalar(storage['stock'] ?? null)}/${scalar(storage['granary_size'] ?? null)}`,
    ];
    const food = isJsonObject(surplus) ? (surplus['food'] ?? null) : null;
    if (isInteger(food)) parts.push(`food ${food < 0 ? food : `+${food}`}/turn`);
    const turns = storage['growth_turns'] ?? null;
    if (turns === null) parts.push('no growth');
    else if (!isInteger(turns)) return yield* Effect.fail(drift('city growth turns'));
    else if (turns > 0) parts.push(`grows in ${turns}t`);
    else if (turns === 0) parts.push('!full, growth blocked');
    else parts.push(`!starving, famine in ${-turns}t`);
    return parts.join(' ');
  });

const MOODS = ['happy', 'content', 'unhappy', 'angry'] as const;

/**
 * Split the citizens by mood and name disorder when the counts prove it.
 *
 * `city_unhappy()` (common/city.c) is exactly `happy < unhappy + 2 * angry`
 * over these same final-feeling counters, so the verdict is the server's own.
 */
export const cityCitizensText = (item: JsonValue): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const fields = isJsonObject(item) ? item : {};
    const citizens = fields['citizens'] ?? null;
    if (!isJsonObject(citizens)) return '';
    const moods = MOODS.map((name) => citizens[name] ?? null);
    if (moods.some((value) => !isInteger(value))) {
      return yield* Effect.fail(drift('city citizens'));
    }
    const counted = moods as ReadonlyArray<number>;
    const parts = [`citizens ${scalar(fields['size'] ?? null)}:`];
    const listed = MOODS.map((name, index) => [name, counted[index] as number] as const)
      .filter(([, value]) => value !== 0)
      .map(([name, value]) => `${value} ${name}`);
    parts.push(listed.length > 0 ? listed.join(', ') : 'none placed');
    const specialists = citizens['specialists'] ?? null;
    if (isInteger(specialists) && specialists !== 0) {
      parts.push(`+ ${specialists} specialist`);
    }
    const [happy, , unhappy, angry] = counted as [number, number, number, number];
    if (happy < unhappy + 2 * angry) parts.push('!disorder');
    if (fields['citizen_counts_consistent'] === false) {
      parts.push('!counts self-healed by the server, not a partition');
    }
    return parts.join(' ');
  });

/**
 * Price the current build in both currencies: shields and gold.
 *
 * The buy cost is on the city page and nowhere on the `city.buy_production`
 * action, so this is the one surface that can quote it before the emergency.
 * `can_change` is `city_can_change_build()` — `!did_buy || stock <= 0` — so a
 * locked build is proof this city already bought this turn.
 */
export const cityProductionLines = (
  item: JsonValue,
  surplus: JsonValue
): ReadonlyArray<string> => {
  const fields = isJsonObject(item) ? item : {};
  const production = fields['production'] ?? null;
  if (!isJsonObject(production)) return [];
  const head = ['build', named(production), scalar(production['kind'] ?? null)];
  const stock = production['shield_stock'] ?? null;
  const cost = production['shield_cost'] ?? null;
  if (isInteger(stock) && isInteger(cost)) {
    head.push(`${stock}/${cost} shields`);
    const shields = isJsonObject(surplus) ? (surplus['shields'] ?? null) : null;
    if (stock >= cost) head.push('done next turn');
    else if (isInteger(shields) && shields > 0) {
      const turns = -Math.floor((stock - cost) / shields);
      head.push(`+${shields}/turn done in ${turns}t`);
    } else head.push('!no shield surplus, never completes');
  }
  const parts = [head.join(' ')];
  parts.push(
    production['can_buy'] === true
      ? `· buy ${scalar(production['buy_cost'] ?? null)} gold`
      : '· !cannot buy this turn'
  );
  if (production['can_change'] === false) {
    parts.push('!locked: this city already bought this turn');
  }
  return packedLines(parts);
};

/** Print only what is set away from its default, each marked `!`. */
export const cityManagementLines = (item: JsonValue): ReadonlyArray<string> => {
  const fields = isJsonObject(item) ? item : {};
  const management = fields['management'] ?? null;
  if (!isJsonObject(management)) return [];
  const parts: string[] = [];
  if (management['did_sell'] === true) {
    parts.push('!sold here this turn: no second sale until next turn');
  }
  const governor = management['governor'] ?? null;
  if (isJsonObject(governor) && governor['enabled'] === true) {
    parts.push('!governor on: tiles are assigned for you');
  }
  const rally = management['rally'] ?? null;
  if (isJsonObject(rally) && rally['active'] === true) {
    let text = `!rally ${scalar(rally['order_count'] ?? null)} orders`;
    for (const flag of ['persistent', 'vigilant'] as const) {
      if (rally[flag] === true) text += ` ${flag}`;
    }
    parts.push(text);
  }
  const options = management['options'] ?? null;
  if (isJsonObject(options)) {
    if (options['allow_disband'] === true) parts.push('!allow_disband');
    const citizens = options['new_citizens'] ?? null;
    if (typeof citizens === 'string' && citizens !== '' && citizens !== 'default') {
      parts.push(`!new_citizens=${citizens}`);
    }
    if (options['conflict'] === true) parts.push('!options_conflict');
  }
  return packedLines(parts);
};

/**
 * Name the exact command that holds each number this page does not.
 *
 * A collection that lives in a child section is named once, as the command that
 * prints it — never as an ellipsis the agent has to guess a way past.
 */
export const cityDrilldownLines = (item: JsonValue, handle: string): ReadonlyArray<string> => {
  const fields = isJsonObject(item) ? item : {};
  const counts = fields['counts'] ?? null;
  if (!isJsonObject(counts) || handle === '') return [];
  const lines: string[] = [];
  for (const [key, label, section] of V2_CITY_DRILLDOWNS) {
    const total = counts[key] ?? null;
    if (!isInteger(total) || total <= 0) continue;
    lines.push(`${total} ${label}: just state --section ${section} --actor_id ${handle}`);
  }
  return lines;
};

export const renderCityDetail = (
  items: ReadonlyArray<JsonValue>,
  aliases: AliasMap | null = null
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    const lines: string[] = [];
    for (const [index, item] of items.entries()) {
      const fields = isJsonObject(item) ? item : {};
      const alias = yield* rowAlias(aliases, item, 'id', 'c', index + 1);
      // Only a resolvable handle may be printed inside a command: without an
      // alias cache the positional `c1` names nothing, so the ID is typed.
      const handle = aliases !== null ? alias : scalar(fields['id'] ?? null);
      const surplus = fields['surplus'] ?? null;
      const head = [alias, yield* needText(item, 'name', 'city')];
      const place = yield* coordinates(item);
      if (place !== null) head.push(place);
      head.push(`sz${yield* needInt(item, 'size', 'city')}`);
      if (isJsonObject(surplus)) head.push(surplusText(surplus));
      const pollution = fields['pollution'] ?? null;
      if (isInteger(pollution) && pollution !== 0) head.push(`!pollution ${pollution}`);
      lines.push(head.join(' '));
      const body = [
        yield* cityGranaryText(item, surplus),
        yield* cityCitizensText(item),
        ...cityProductionLines(item, surplus),
        ...cityManagementLines(item),
      ];
      lines.push(...body.filter((text) => text !== '').map((text) => `  ${text}`));
      const rows = yield* cityOutputRows(fields['outputs'] ?? null);
      lines.push(...table(rows).map((line) => `  ${line}`));
      lines.push(...cityDrilldownLines(item, handle).map((line) => `  ${line}`));
    }
    return lines;
  });
