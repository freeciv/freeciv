/**
 * `update_from_page` — project one validated v2 page into the mirror.
 *
 * Ports `play/state_mirror.py:1224-1297`.
 *
 * A page is a *window*, so page 2 of a unit catalog must not erase page 1: a
 * page carrying the mirror's own revision merges into the file (rows keyed by
 * column 0), while a newer revision replaces it and an older one is ignored
 * outright.  The `# <section> N/T complete|partial` note says how much of the
 * section the file holds.
 *
 * The entry point is a read-modify-write of the files it touches, so call it
 * from inside the session's request lock, as every v2 command already holds.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject, type JsonObject } from 'src/schema/primitives';
import type { PrivateFs } from 'src/services/private-fs';
import {
  MAX_ROWS,
  REPLACE_ONLY,
  SECTION_TARGETS,
  SECTION_TITLES,
  dig,
  isStale,
  mirrorDir,
  mirrorError,
  mirrorGuard,
  readMirror,
  revisionPair,
  sameRevision,
  writeMirror,
  type MirrorAliases,
  type MirrorRevision,
} from 'src/services/mirror/store';
import { pageNote, parseTable, tableByKey, tableText } from 'src/services/mirror/table';
import { rowChanges, updateDelta } from 'src/services/mirror/delta';
import { updateMap } from 'src/services/mirror/update-map';
import { updateYields } from 'src/services/mirror/update-yields';
import type { RenderedSection, SectionRenderer } from 'src/render/mirror/section';
import { renderCities } from 'src/render/mirror/cities';
import { renderDiplomacy } from 'src/render/mirror/diplomacy';
import { renderGovernments } from 'src/render/mirror/governments';
import { renderNations } from 'src/render/mirror/nations';
import { renderOverview } from 'src/render/mirror/overview';
import { renderStyles } from 'src/render/mirror/styles';
import { renderUnits } from 'src/render/mirror/units';
import { updateOptions } from 'src/render/mirror/options';

/** `_RENDERERS` — which renderer owns which page section. */
export const RENDERERS: ReadonlyMap<string, SectionRenderer> = new Map<string, SectionRenderer>([
  ['overview', renderOverview],
  ['units', renderUnits],
  ['cities', renderCities],
  ['diplomacy', renderDiplomacy],
  ['pregame_nations', renderNations],
  ['pregame_styles', renderStyles],
  ['governments', renderGovernments],
]);

/** The sections `_update_map` owns. */
const MAP_SECTIONS: ReadonlySet<string> = new Set(['known_tiles', 'map_tiles', 'tile_window']);

export interface UpdatePageOptions {
  /** Opaque id → CLI alias, so the mirror and the CLI agree on every name. */
  readonly aliases?: MirrorAliases | null;
}

interface PageParts {
  readonly revision: MirrorRevision;
  readonly inner: JsonObject;
  readonly section: string;
  readonly items: ReadonlyArray<unknown>;
}

/** The shape checks CPython runs before it touches a file. */
const pageParts = (page: unknown): Effect.Effect<PageParts, PlayerError> =>
  Effect.gen(function* () {
    const revision = yield* revisionPair(dig(page, 'state_revision'));
    const inner = dig(page, 'page');
    if (!isJsonObject(inner)) {
      return yield* Effect.fail(mirrorError('page payload carries no page envelope'));
    }
    const section = inner['section'];
    const items = inner['items'];
    if (typeof section !== 'string' || !Array.isArray(items)) {
      return yield* Effect.fail(mirrorError('page envelope carries no section items'));
    }
    if (items.length > MAX_ROWS) {
      return yield* Effect.fail(mirrorError('page envelope carries too many items'));
    }
    return { revision, inner, section, items };
  });

/** `isinstance(total_items, int)` — a JSON integer, never a bool. */
const isCount = (value: unknown): value is number =>
  typeof value === 'number' && Number.isInteger(value);

const columnsEqual = (
  left: ReadonlyArray<string>,
  right: ReadonlyArray<string>
): boolean => left.length === right.length && left.every((name, index) => name === right[index]);

/**
 * `update_from_page` — project one validated page (state or legal-actions).
 *
 * Returns the files written, in CPython's order: the section file, then the
 * delta digest when the digest moved.  A page older than the mirror writes
 * nothing and returns an empty list.
 */
export const updateFromPage = (
  dir: string,
  command: string,
  page: unknown,
  options: UpdatePageOptions = {}
): Effect.Effect<ReadonlyArray<string>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const aliases = options.aliases ?? null;
    const { revision, inner, section, items } = yield* pageParts(page);
    if (section === 'legal_actions') {
      return yield* updateOptions(dir, command, revision, inner, items, aliases);
    }
    if (MAP_SECTIONS.has(section)) {
      return yield* updateMap(dir, command, revision, items);
    }
    if (section === 'city_citizens') {
      // `_update_yields` never reads the page envelope; U08's port drops it
      // (PORT_MAP §7), so the dispatch hands it only what it uses.
      return yield* updateYields(dir, command, revision, items, aliases);
    }
    const target = SECTION_TARGETS.get(section);
    const renderer = RENDERERS.get(section);
    const title = SECTION_TITLES.get(section);
    if (target === undefined || renderer === undefined || title === undefined) return [];
    const prior = parseTable(yield* readMirror(dir, target));
    if (isStale(revision, prior.revision)) return [];
    const rendered = yield* renderer(items, aliases);
    const terminal = (inner['next_cursor'] ?? null) === null;
    const total = inner['total_items'];
    const rows = mergedRows({
      prior: prior.rows,
      priorColumns: prior.columns,
      priorRevision: prior.revision,
      revision,
      rendered,
      section,
      whole: terminal && total === items.length,
    });
    // `overview` projects one item into many fact rows; every other section is
    // one row per item, so the row count is the honest "how much do I hold".
    const shown = REPLACE_ONLY.has(section) ? items.length : rows.length;
    const complete = terminal && (!isCount(total) || shown >= total);
    const note = pageNote(title, shown, total, complete);
    const written = yield* writeMirror(
      dir,
      target,
      tableText(revision, [note], rendered.columns, rows)
    );
    const delta = yield* updateDelta(
      dir,
      revision,
      command,
      title,
      rowChanges(rendered.columns, prior, rows)
    );
    return delta === null ? [written] : [written, delta];
  });

interface MergeInput {
  readonly prior: ReadonlyArray<ReadonlyArray<string>>;
  readonly priorColumns: ReadonlyArray<string>;
  readonly priorRevision: MirrorRevision | null;
  readonly revision: MirrorRevision;
  readonly rendered: RenderedSection;
  readonly section: string;
  /** True when this page is the section's whole answer at this revision. */
  readonly whole: boolean;
}

/**
 * A window of a section the mirror already holds at this exact revision:
 * merge, so page 2 of a unit catalog cannot erase page 1.
 */
const mergedRows = (input: MergeInput): ReadonlyArray<ReadonlyArray<string>> => {
  const mergeable =
    sameRevision(input.priorRevision, input.revision) &&
    !input.whole &&
    !REPLACE_ONLY.has(input.section) &&
    columnsEqual(input.priorColumns, input.rendered.columns);
  if (!mergeable) return input.rendered.rows;
  const keyed = new Map(tableByKey({ revision: null, columns: [], rows: input.prior }));
  for (const row of input.rendered.rows) {
    const key = row[0];
    if (key !== undefined) keyed.set(key, row);
  }
  return [...keyed.values()];
};

/**
 * `_mirror_page` (client.py:3015-3027) — the client-side bridge: project one
 * page for a session, and never fail the command that produced it.
 */
export const mirrorPage = (
  sessionPath: string,
  page: unknown,
  command: string,
  options: UpdatePageOptions = {}
): Effect.Effect<void, never, PrivateFs> =>
  mirrorGuard(
    Effect.flatMap(mirrorDir(sessionPath), (dir) => updateFromPage(dir, command, page, options))
  );
