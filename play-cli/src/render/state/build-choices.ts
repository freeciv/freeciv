/**
 * The `city_build_choices` section and the change-production forfeit.
 *
 * Ports `_city_build_stock` (client.py:5186-5224), `_build_choice_note`
 * (5227-5250) and `_render_city_build_choices` (5253-5278).
 *
 * `city_change_production_penalty()` (common/city.c) halves the accumulated
 * shields when the new target is a different production class — unit, ordinary
 * improvement, wonder — and the native client has already run it: every build
 * choice carries `cost.shield_stock_after_change`, the stock this city would
 * have *after* switching to that exact target.  Subtracting it from the stock
 * the city holds now gives the forfeit as a fact, so no rule is re-derived here
 * and no warning is printed unless both numbers are in hand at one revision.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import {
  drift,
  isJsonObject,
  needText,
  scalar,
  table,
  type AliasMap,
  type JsonValue,
} from 'src/render/primitives';
import type { Revision } from 'src/schema/revision';
import { PrivateFs } from 'src/services/private-fs';
import { mirrorText, parseTable } from 'src/services/mirror';

/**
 * `V2_SHOW_FILES["cities"]` — the one mirror projection this renderer reads.
 *
 * The whole `V2_SHOW_FILES` map is U09's; only the cities row is needed here
 * and duplicating one two-element path beats importing a command surface that
 * has not landed.  See NOTES §U10.
 */
export const CITIES_MIRROR_FILE: ReadonlyArray<string> = ['state', 'cities.tsv'];

/** `MIRROR_CELL_RE` — the shape of a mirror cell that is a plain integer. */
const MIRROR_CELL_RE = /^-?[0-9]{1,9}$/;

const mirrorCell = (
  columns: ReadonlyArray<string>,
  row: ReadonlyArray<string>,
  name: string
): string => {
  const index = columns.indexOf(name);
  if (index < 0) return '';
  return row[index] ?? '';
};

/** Read the left half of a `3/3` mirror cell as an integer, or `null`. */
const mirrorNumber = (text: string): number | null => {
  const head = (text.split('/', 1)[0] ?? '').trim();
  return MIRROR_CELL_RE.test(head) ? Number(head) : null;
};

export interface BuildStockRequest {
  readonly sessionPath: string | null;
  readonly revision: Revision;
  readonly items: ReadonlyArray<JsonValue>;
  readonly aliases: AliasMap | null;
}

/**
 * Read this city's shield stock from the mirror, at this exact revision.
 *
 * The mirror is only ever as fresh as the last read, and a forfeit computed
 * against a stale stock would be a wrong number stated confidently.  So the
 * stock is used only when the cities table already stands at the revision this
 * page was served at; otherwise the rendering says nothing.  Reading the mirror
 * opens no socket.
 */
export const cityBuildStock = (
  request: BuildStockRequest
): Effect.Effect<number | null, never, PrivateFs> =>
  Effect.gen(function* () {
    const { sessionPath, revision, items, aliases } = request;
    if (sessionPath === null || aliases === null) return null;
    const head = items[0];
    const cityId = isJsonObject(head) ? (head['city_id'] ?? null) : null;
    if (typeof cityId !== 'string' || cityId === '') return null;
    if (items.some((item) => !isJsonObject(item) || item['city_id'] !== cityId)) return null;
    const alias = aliases[cityId];
    if (alias === undefined || alias === '') return null;
    const text = yield* mirrorText(sessionPath, CITIES_MIRROR_FILE);
    const parsed = parseTable(text);
    const stamp = parsed.revision;
    if (stamp === null || stamp.turn !== revision.turn || stamp.revision !== revision.revision) {
      return null;
    }
    for (const row of parsed.rows) {
      if (mirrorCell(parsed.columns, row, 'alias') === alias) {
        return mirrorNumber(mirrorCell(parsed.columns, row, 'shields'));
      }
    }
    return null;
  });

export const buildChoiceNote = (
  item: JsonValue,
  cost: JsonValue,
  stock: number | null
): string => {
  const fields = isJsonObject(item) ? item : {};
  const keep = isJsonObject(cost) ? (cost['shield_stock_after_change'] ?? null) : null;
  const integral = typeof keep === 'number' && Number.isInteger(keep);
  const parts: string[] = [];
  if (integral && stock !== null && stock > (keep)) {
    parts.push(`!forfeits ${stock - (keep)} of ${stock} shields`);
  } else if (integral && stock === null) {
    parts.push(`keep ${keep}`);
  }
  if (fields['can_build_now'] === false) parts.push('!worklist only');
  const upkeep = fields['upkeep'] ?? null;
  if (isJsonObject(upkeep)) {
    for (const [key, number] of Object.entries(upkeep)) {
      if (typeof number === 'number' && Number.isInteger(number) && number !== 0) {
        parts.push(`!upkeep ${key} ${scalar(number)}`);
      }
    }
  }
  const unhappy = fields['happy_cost'] ?? null;
  if (typeof unhappy === 'number' && Number.isInteger(unhappy) && unhappy !== 0) {
    parts.push(`!unhappy ${unhappy}`);
  }
  return parts.join(' ');
};

export const renderCityBuildChoices = (
  items: ReadonlyArray<JsonValue>,
  aliases: AliasMap | null = null,
  stock: number | null = null
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    void aliases;
    const lines: string[] = [];
    if (stock !== null) {
      lines.push(
        `stock ${stock} shields; a switch to another production class forfeits half of it`
      );
    }
    const rows: Array<ReadonlyArray<string>> = [
      ['#', 'name', 'kind', 'shields', 'turns', 'note'],
    ];
    for (const [index, item] of items.entries()) {
      const fields = isJsonObject(item) ? item : {};
      const cost = fields['cost'] ?? null;
      if (!isJsonObject(cost)) return yield* Effect.fail(drift('city build choice cost'));
      rows.push([
        String(index + 1),
        yield* needText(item, 'name', 'city build choice'),
        scalar(fields['kind'] ?? null),
        scalar(cost['shields'] ?? null),
        scalar(cost['turns_with_stock'] ?? null),
        buildChoiceNote(item, cost, stock),
      ]);
    }
    lines.push(...table(rows));
    return lines;
  });
