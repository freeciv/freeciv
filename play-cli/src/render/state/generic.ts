/**
 * The fallback renderer every section without a bespoke one falls through to.
 *
 * Ports `_flatten_item` (client.py:5087-5101) and `_render_generic_items`
 * (5104-5156).
 *
 * A nested object rendered as `json.dumps` costs more than the JSON page it was
 * supposed to compact, so one level is spread into its own columns and anything
 * deeper is named rather than dumped.
 */
import { flat, isJsonObject, scalar, table, type JsonValue } from 'src/render/primitives';

/** How many columns still fit a table before the rows go long-form. */
const MAX_TABLE_COLUMNS = 14;

/** Values that mean "this column carries no decision" on every row. */
const EMPTY_CELLS: ReadonlySet<string> = new Set(['-', '0', 'no', '']);

/** Flatten one payload item one level into `parent.child` cells. */
export const flattenItem = (item: JsonValue): ReadonlyMap<string, string> => {
  const flattened = new Map<string, string>();
  if (!isJsonObject(item)) return flattened;
  for (const [key, value] of Object.entries(item)) {
    if (isJsonObject(value) && Object.keys(value).length > 0) {
      for (const [inner, nested] of Object.entries(value)) {
        flattened.set(`${key}.${inner}`, flat(nested ?? null));
      }
      continue;
    }
    flattened.set(key, flat(value ?? null));
  }
  return flattened;
};

export const renderGenericItems = (
  items: ReadonlyArray<JsonValue>
): ReadonlyArray<string> => {
  if (!items.every((item) => isJsonObject(item))) {
    return items.map((item, index) => `${index + 1}  ${scalar(item)}`);
  }
  const flattened = items.map((item) => flattenItem(item));
  const columns: string[] = [];
  for (const item of flattened) {
    for (const key of item.keys()) if (!columns.includes(key)) columns.push(key);
  }
  const first = flattened[0];
  // A column identical on every row is a page constant, not per-row data: an
  // actor-scoped page repeats its own 37-char actor ID on every row.
  const constants = new Map<string, string>();
  if (first !== undefined && flattened.length > 1) {
    for (const key of columns) {
      if (!flattened.every((item) => item.has(key))) continue;
      const value = first.get(key) as string;
      if (flattened.every((item) => item.get(key) === value)) constants.set(key, value);
    }
  }
  // A column that is empty, zero or false everywhere carries no decision.
  const empty = new Set(
    columns.filter(
      (key) =>
        !constants.has(key) &&
        flattened.every((item) => EMPTY_CELLS.has(item.get(key) ?? '-'))
    )
  );
  const shown = columns.filter((key) => !constants.has(key) && !empty.has(key));
  const lines: string[] = [];
  if (constants.size > 0) {
    lines.push(
      `constants: ${[...constants.entries()].map(([key, value]) => `${key}=${value}`).join(' ')}`
    );
  }
  if (shown.length === 0) {
    lines.push(`${flattened.length} item(s), all fields constant`);
    return lines;
  }
  if (shown.length > MAX_TABLE_COLUMNS) {
    lines.push(
      ...flattened.map(
        (item, index) =>
          `${index + 1}  ` +
          shown
            .filter((key) => item.has(key))
            .map((key) => `${key}=${item.get(key) as string}`)
            .join(' ')
      )
    );
    return lines;
  }
  lines.push(
    ...table([
      ['#', ...shown],
      ...flattened.map((item, index) => [
        String(index + 1),
        ...shown.map((key) => item.get(key) ?? 'n/a'),
      ]),
    ])
  );
  return lines;
};
