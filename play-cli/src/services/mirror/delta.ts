/**
 * `state/delta.md` — the short prose digest of what changed.
 *
 * Ports `play/state_mirror.py:1081-1208` — `_DELTA_SINCE_RE`, `_parse_delta`,
 * `_delta_text`, `_update_delta`, `_row_changes`.
 *
 * `just show` leads with this file, so it may never lag its own tables: an
 * empty change list still moves the stamp forward, and the digest never walks
 * backwards.  Both rules are asserted by `DeltaTests`.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import type { PrivateFs } from 'src/services/private-fs';
import {
  DELTA_FILE,
  MAX_DELTA_LINES,
  cell,
  isNewer,
  readMirror,
  revLine,
  sameRevision,
  splitLines,
  strip,
  writeMirror,
  type MirrorRevision,
} from 'src/services/mirror/store';
import { parseRevLine, tableByKey, type MirrorTable } from 'src/services/mirror/table';

export const DELTA_SINCE_RE = /^since rev (\d+) turn (\d+)/;

/** A parsed `delta.md`: its stamp, the revision it compares against, its body. */
export interface MirrorDelta {
  readonly revision: MirrorRevision | null;
  readonly since: MirrorRevision | null;
  /** Section title → its bullet lines, in the order the file holds them. */
  readonly sections: ReadonlyMap<string, ReadonlyArray<string>>;
}

/** `_parse_delta` — read the digest back. */
export const parseDelta = (text: string | null | undefined): MirrorDelta => {
  const sections = new Map<string, string[]>();
  if (text === null || text === undefined || text === '') {
    return { revision: null, since: null, sections };
  }
  const lines = splitLines(text);
  const revision = parseRevLine(lines[0]);
  const state = lines.slice(1).reduce<{ since: MirrorRevision | null; current: string | null }>(
    (accumulated, line) => {
      const found = DELTA_SINCE_RE.exec(line);
      if (found !== null) {
        return {
          since: { turn: Number(found[2]), revision: Number(found[1]) },
          current: accumulated.current,
        };
      }
      if (line.startsWith('## ')) {
        const title = strip(line.slice(3));
        if (!sections.has(title)) sections.set(title, []);
        return { since: accumulated.since, current: title };
      }
      if (line.startsWith('- ') && accumulated.current !== null) {
        sections.get(accumulated.current)?.push(line.slice(2));
      }
      return accumulated;
    },
    { since: null, current: null }
  );
  return { revision, since: state.since, sections };
};

/** `_delta_text` — render the digest. */
export const deltaText = (
  revision: MirrorRevision,
  since: MirrorRevision | null,
  command: string,
  sections: ReadonlyMap<string, ReadonlyArray<string>>
): string => {
  const head =
    since !== null && !sameRevision(since, revision)
      ? `since rev ${since.revision} turn ${since.turn} · last update: ${cell(command)}`
      : `no earlier mirror · last update: ${cell(command)}`;
  const body = [...sections].flatMap(([title, entries]) =>
    entries.length === 0
      ? []
      : [
          '',
          `## ${title}`,
          ...entries.slice(0, MAX_DELTA_LINES).map((entry) => `- ${entry}`),
          ...(entries.length > MAX_DELTA_LINES
            ? [`- … and ${entries.length - MAX_DELTA_LINES} more`]
            : []),
        ]
  );
  const lines =
    body.length === 0
      ? [revLine(revision), head, '', 'nothing changed.']
      : [revLine(revision), head, ...body];
  return `${lines.join('\n')}\n`;
};

/**
 * `_update_delta` — read-modify-write the digest.
 *
 * Returns the file written, or `null` when the digest is deliberately left
 * alone: nothing to say at an unchanged (or older) revision, and never a walk
 * backwards.
 */
export const updateDelta = (
  dir: string,
  revision: MirrorRevision,
  command: string,
  title: string,
  entries: ReadonlyArray<string>
): Effect.Effect<string | null, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const prior = parseDelta(yield* readMirror(dir, DELTA_FILE));
    if (entries.length === 0) {
      // An empty change list still has to move the stamp forward, or the
      // digest keeps claiming an older revision than the tables beside it.
      if (prior.revision === null || !isNewer(revision, prior.revision)) return null;
    } else if (prior.revision !== null && isNewer(prior.revision, revision)) {
      // Never walk the digest backwards: a mirror file only moves forward.
      return null;
    }
    const advancing = prior.revision === null || isNewer(revision, prior.revision);
    const since = advancing ? prior.revision : prior.since;
    const merged = new Map<string, ReadonlyArray<string>>(advancing ? [] : prior.sections);
    const existing = [...(merged.get(title) ?? [])];
    for (const entry of entries) {
      if (!existing.includes(entry)) existing.push(entry);
    }
    merged.set(title, existing);
    return yield* writeMirror(dir, DELTA_FILE, deltaText(revision, since, command, merged));
  });

const described = (row: ReadonlyArray<string>): string =>
  row
    .slice(1, 3)
    .filter((value) => value !== '-')
    .join(' ');

/** CPython's `str.rstrip(": ")` — strip any trailing `:` or space. */
const rstripColonSpace = (text: string): string => text.replace(/[: ]+$/, '');

/** `_row_changes` — describe added, removed and changed rows. */
export const rowChanges = (
  columns: ReadonlyArray<string>,
  prior: MirrorTable,
  rows: ReadonlyArray<ReadonlyArray<string>>
): ReadonlyArray<string> => {
  if (prior.columns.length === 0) {
    return rows.map(
      (row) => `${strip(`${row[0] ?? ''} ${row.slice(1, 3).join(' ')}`)} (first observation)`
    );
  }
  const old = tableByKey(prior);
  const next = new Map<string, ReadonlyArray<string>>();
  for (const row of rows) {
    const key = row[0];
    if (key !== undefined) next.set(key, row);
  }
  const changed = [...next].flatMap(([key, row]) => {
    const previous = old.get(key);
    if (previous === undefined) {
      return [rstripColonSpace(`${key} new: ${described(row)}`)];
    }
    const differences = columns.flatMap((name, index) => {
      const before = previous[index] ?? '-';
      const after = row[index] ?? '-';
      return index !== 0 && before !== after ? [`${name} ${before} -> ${after}`] : [];
    });
    return differences.length === 0 ? [] : [`${key}: ${differences.join('; ')}`];
  });
  const gone = [...old].flatMap(([key, previous]) =>
    next.has(key) ? [] : [`${key} gone (was ${described(previous)})`]
  );
  return [...changed, ...gone];
};
