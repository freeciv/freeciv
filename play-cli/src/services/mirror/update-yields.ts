/**
 * Project a `city_citizens` page into `state/yields.tsv`.
 *
 * Ports `play/state_mirror.py:1324-1385` (`_update_yields`).
 *
 * This is the only page that prices a tile in food/shields/trade, so it is what
 * `show map --yields` overlays.  A tile is keyed by the coordinate alias the CLI
 * already assigned it; a tile this seat has never located by coordinate keeps
 * its opaque handle, because inventing a coordinate for it would put a tile on
 * the map the seat has not seen.
 *
 * Two shapes are deliberately silent rather than fatal: a specialist row (it
 * prices no tile) and a tile row whose `yields` or `tile_id` is the wrong type.
 * Only a non-object item is an error, and a page that priced nothing writes
 * nothing at all — not even an empty table.
 *
 * CPython takes the page envelope as a fifth argument and never reads it; the
 * port drops it (PORT_MAP §7).
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject } from 'src/schema/primitives';
import type { PrivateFs } from 'src/services/private-fs';
import {
  YIELD_COLUMNS,
  YIELD_FILE,
  cell,
  dig,
  isStale,
  mirrorError,
  pageNote,
  parseTable,
  readMirror,
  sameRevision,
  tableByKey,
  tableText,
  updateDelta,
  writeMirror,
  type MirrorAliases,
  type MirrorRevision,
  type MirrorTable,
} from 'src/services/mirror';

/** The digest title `_update_yields` files its one entry under. */
export const YIELD_DELTA_TITLE = 'yields';

/**
 * `(aliases or {}).get(key, key)`.
 *
 * CPython looks the id up in a dict, which sees **own keys only**; a plain JS
 * property read walks `Object.prototype`, so a server-chosen `tile_id`/`city_id`
 * of `toString`/`constructor`/`valueOf`/`__proto__` would resolve to an
 * inherited value and `?? key` would never fire — the overlay would carry
 * `function toString() { [native code] }` where CPython writes `toString`, at a
 * different column width and under a merge key `render_map_yields` can no
 * longer match.  `Object.hasOwn` is the dict.
 */
const aliased = (aliases: MirrorAliases | null | undefined, key: string): string => {
  if (aliases === null || aliases === undefined) return key;
  return Object.hasOwn(aliases, key) ? (aliases[key] ?? key) : key;
};

/**
 * The rows one `city_citizens` page proves, in payload order.
 *
 * Exported because it is the whole projection decision — which citizen rows
 * price a tile and how each cell is spelled — and a test that pins it does not
 * need a filesystem.
 */
export const yieldRows = (
  items: ReadonlyArray<unknown>,
  aliases?: MirrorAliases | null
): Effect.Effect<ReadonlyArray<ReadonlyArray<string>>, PlayerError> =>
  Effect.suspend(() => {
    const rows: Array<ReadonlyArray<string>> = [];
    for (const item of items) {
      if (!isJsonObject(item)) {
        return Effect.fail(mirrorError('city citizen item is not an object'));
      }
      if (dig(item, 'kind') !== 'tile') continue;
      const yields = dig(item, 'yields');
      const tileId = dig(item, 'tile_id');
      if (!isJsonObject(yields) || typeof tileId !== 'string') continue;
      const cityId = dig(item, 'city_id');
      rows.push([
        cell(aliased(aliases, tileId)),
        cell(dig(yields, 'food')),
        cell(dig(yields, 'shields')),
        cell(dig(yields, 'trade')),
        dig(item, 'worked') === true ? 'yes' : 'no',
        cell(typeof cityId === 'string' ? aliased(aliases, cityId) : cityId),
      ]);
    }
    return Effect.succeed(rows);
  });

/** `_Table.by_key` merged under this page's rows, insertion order preserved. */
const mergeRows = (
  prior: MirrorTable,
  rows: ReadonlyArray<ReadonlyArray<string>>
): ReadonlyArray<ReadonlyArray<string>> => {
  const merged = new Map(tableByKey(prior));
  for (const row of rows) {
    const key = row[0];
    if (key !== undefined) merged.set(key, row);
  }
  return [...merged.values()];
};

/**
 * `_update_yields` — returns the files written, in CPython's order
 * (`state/yields.tsv` first, `state/delta.md` when the digest moved), or
 * nothing at all when the page priced no tile or is older than the mirror.
 */
export const updateYields = (
  dir: string,
  command: string,
  revision: MirrorRevision,
  items: ReadonlyArray<unknown>,
  aliases?: MirrorAliases | null
): Effect.Effect<ReadonlyArray<string>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const priced = yield* yieldRows(items, aliases);
    if (priced.length === 0) return [];
    const prior = parseTable(yield* readMirror(dir, YIELD_FILE));
    if (isStale(revision, prior.revision)) return [];
    // One page prices one city's tiles; a second city's page must add to the
    // overlay rather than replace it.
    const merge =
      sameRevision(prior.revision, revision) &&
      prior.columns.length === YIELD_COLUMNS.length &&
      prior.columns.every((name, index) => name === YIELD_COLUMNS[index]);
    const rows = merge ? mergeRows(prior, priced) : priced;
    const note = pageNote(YIELD_DELTA_TITLE, rows.length, rows.length, true);
    const written = [
      yield* writeMirror(
        dir,
        YIELD_FILE,
        tableText(revision, [note], YIELD_COLUMNS, rows)
      ),
    ];
    const delta = yield* updateDelta(dir, revision, command, YIELD_DELTA_TITLE, [
      `tile yields known for ${rows.length} tiles`,
    ]);
    return delta === null ? written : [...written, delta];
  });
