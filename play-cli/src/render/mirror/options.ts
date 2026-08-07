/**
 * The `state/options/<alias>.txt` writer.
 *
 * Ports `play/state_mirror.py:1388-1468` (`_update_options`).
 *
 * Two merge rules carry the whole safety argument of this file:
 *
 * * **A complete, actor-wide catalog replaces.** The client re-projects the
 *   *promoted accumulation*, not just its final page, so the rows staged before
 *   the aliases existed must disappear instead of surviving with `-` in the
 *   alias column.  Replacing also keeps two capabilities that render identically
 *   (same kind, target and label, differing only in a subject key this table
 *   does not print) as two addressable rows instead of collapsing them.
 * * **Anything narrower merges.** `--target_id` asks a narrower question, so a
 *   complete answer to it is not a complete menu; letting it replace would drop
 *   capabilities the seat still holds and still has aliases for.
 *
 * When every alias column is `-` the file is readable but not executable, and it
 * says so — naming the command that re-issues the names rather than inventing
 * `aN` handles the CLI never assigned.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject, type JsonObject } from 'src/schema/primitives';
import type { PrivateFs } from 'src/services/private-fs';
import {
  OPTIONS_DIR,
  STATE_DIR,
  aliasMap,
  cell,
  fileName,
  isStale,
  pageNote,
  parseTable,
  readMirror,
  sameRevision,
  tableText,
  updateDelta,
  writeMirror,
  type MirrorAliases,
  type MirrorRevision,
} from 'src/services/mirror';
import { renderActions } from 'src/render/mirror/actions';

/** How an unscoped catalog names itself, both as a file and in its heading. */
const UNSCOPED_NAME = 'unscoped';
const UNSCOPED_HEADING = 'unscoped catalog (union of the kinds fetched at this rev)';

interface CatalogScope {
  /** The actor this catalog is scoped to, or `null` for the unscoped union. */
  readonly actor: string | null;
  /** The file stem (`u1`, `unscoped`) and the alias the remedy note names. */
  readonly name: string;
  /** The `# actor unit u1` heading line. */
  readonly heading: string;
}

const catalogScope = (
  scope: unknown,
  aliases: MirrorAliases | null | undefined
): CatalogScope => {
  const actorId = isJsonObject(scope) ? scope['actor_id'] : undefined;
  const actor = typeof actorId === 'string' ? actorId : null;
  if (actor === null) {
    return { actor, name: UNSCOPED_NAME, heading: UNSCOPED_HEADING };
  }
  const actorType = isJsonObject(scope) ? cell(scope['actor_type']) : 'actor';
  const letter = actorType === '' ? 'a' : (actorType[0] ?? 'a');
  const key = cell(actor);
  const name = aliasMap(letter, [actor], aliases).get(key) ?? key;
  return { actor, name, heading: `actor ${actorType} ${name}` };
};

/** `isinstance(total_items, int)` — a JSON integer, never a bool. */
const isCount = (value: unknown): value is number =>
  typeof value === 'number' && Number.isInteger(value);

/** The merge key of a catalog row: everything but its alias column. */
const rowKey = (row: ReadonlyArray<string>): string => JSON.stringify(row.slice(1));

const columnsEqual = (
  left: ReadonlyArray<string>,
  right: ReadonlyArray<string>
): boolean => left.length === right.length && left.every((name, index) => name === right[index]);

/**
 * `_update_options` — project one `legal_actions` page into this actor's
 * catalog file.  Returns the files written, in CPython's order.
 */
export const updateOptions = (
  dir: string,
  command: string,
  revision: MirrorRevision,
  inner: JsonObject,
  items: ReadonlyArray<unknown>,
  aliases: MirrorAliases | null | undefined
): Effect.Effect<ReadonlyArray<string>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const scope = inner['scope'];
    const catalog = catalogScope(scope, aliases);
    const target = [STATE_DIR, OPTIONS_DIR, `${yield* fileName(catalog.name)}.txt`];
    const prior = parseTable(yield* readMirror(dir, target));
    if (isStale(revision, prior.revision)) return [];
    const rendered = yield* renderActions(items, aliases, catalog.actor);
    const total = inner['total_items'];
    const terminal = Object.hasOwn(inner, 'catalog_complete')
      ? Boolean(inner['catalog_complete'])
      : (inner['next_cursor'] ?? null) === null;
    const whole =
      catalog.actor !== null &&
      isJsonObject(scope) &&
      !Object.hasOwn(scope, 'target_id') &&
      terminal &&
      (!isCount(total) || items.length >= total);
    const merged =
      !whole && sameRevision(prior.revision, revision) && columnsEqual(prior.columns, rendered.columns)
        ? mergeRows(prior.rows, rendered.rows)
        : rendered.rows;
    const complete = terminal && (!isCount(total) || merged.length >= total);
    const note = pageNote('actions', merged.length, total, complete);
    const remedy =
      merged.length > 0 && merged.every((row) => row[0] === '-')
        ? [
            'no action alias resolves at this revision; re-enumerate with ' +
              (catalog.actor !== null
                ? `\`just legal --actor_id ${catalog.name} --all\``
                : '`just legal --kind KIND --all`') +
              ' before acting',
          ]
        : [];
    const written = yield* writeMirror(
      dir,
      target,
      tableText(revision, [catalog.heading, note, ...remedy], rendered.columns, merged)
    );
    const delta = yield* updateDelta(dir, revision, command, 'options', [
      `options/${catalog.name}.txt refreshed: ${note}`,
    ]);
    return delta === null ? [written] : [written, delta];
  });

/** A window at the mirror's own revision adds to the file rather than replacing it. */
const mergeRows = (
  prior: ReadonlyArray<ReadonlyArray<string>>,
  rows: ReadonlyArray<ReadonlyArray<string>>
): ReadonlyArray<ReadonlyArray<string>> => {
  const keyed = new Map<string, ReadonlyArray<string>>();
  for (const row of prior) keyed.set(rowKey(row), row);
  for (const row of rows) keyed.set(rowKey(row), row);
  return [...keyed.values()];
};
