/**
 * The state mirror's foundation: layout, atomic private writes, value cells.
 *
 * Ports `play/state_mirror.py:130-208` (the module constants), 215-358
 * (`_client`, `_error`, `_write`, `_read`, `_dig`, `_cell`, `_pair`,
 * `_position`, `_handle`, `_alias_map`, `_file_name`, `_revision_pair`,
 * `_rev_line`, `_newer`, `_stale`) and 1215-1223 (`mirror_dir`), plus the
 * client-side bridge `_mirror_path` / `_mirror` (client.py:3002-3013),
 * `_mirror_text` and `_cached_phase_note` (10755-10799).
 *
 * Two guarantees live here and nowhere else:
 *
 * * **Atomicity.** Every mirror write goes through `PrivateFs.writeText`,
 *   which is temp file + `rename` at mode 0600.  A failure mid-write leaves
 *   the previous file untouched and no partial file behind — `AtomicityTests`
 *   and `LeakTests` in `play/tests/test_state_mirror.py` are the spec.
 * * **Sanitization.** `cell` strips control characters and caps length, so a
 *   server-supplied name can never forge a `# rev` header line or a table row.
 */
import * as path from 'node:path';
import { Console, Effect } from 'effect';
import { PlayerError, playerError } from 'src/errors';
import { isJsonObject } from 'src/schema/primitives';
import { PrivateFs } from 'src/services/private-fs';
import { rewriteProgMentions } from 'src/services/prog-prefix';

// ---------------------------------------------------------------------------
// Constants — the mirror's directory layout and cell limits
// ---------------------------------------------------------------------------

export const MAX_CELL = 64;
export const MAX_DELTA_LINES = 12;
export const MAX_ROWS = 4096;
export const MAX_LOG_LINE = 512;

/** `_CONTROL_RE` — what may never reach a projection file. */
export const CONTROL_RE = /[\x00-\x1f\x7f]/g;

/** `_HANDLE_WIDTHS` — the id-suffix widths a fallback handle tries in order. */
export const HANDLE_WIDTHS: ReadonlyArray<number> = [6, 10, 32];

export const STATE_DIR = 'state';
export const CACHE_DIR = 'cache';

export const HEADER_FILE: ReadonlyArray<string> = [STATE_DIR, 'header.txt'];
export const PHASE_FILE: ReadonlyArray<string> = [STATE_DIR, 'phase.json'];
export const MONITOR_LOG_FILE: ReadonlyArray<string> = [STATE_DIR, 'monitor.log'];
export const DELTA_FILE: ReadonlyArray<string> = [STATE_DIR, 'delta.md'];
export const OVERVIEW_FILE: ReadonlyArray<string> = [STATE_DIR, 'overview.tsv'];
export const MAP_FILE: ReadonlyArray<string> = [STATE_DIR, 'map.txt'];
export const YIELD_FILE: ReadonlyArray<string> = [STATE_DIR, 'yields.tsv'];
export const OPTIONS_DIR = 'options';

export const PHASE_SCHEMA_VERSION = 1;

export const YIELD_COLUMNS: ReadonlyArray<string> = [
  'tile',
  'food',
  'shields',
  'trade',
  'worked',
  'city',
];

/** `_SECTION_TARGETS` — which file a v2 page section projects into. */
export const SECTION_TARGETS: ReadonlyMap<string, ReadonlyArray<string>> = new Map([
  ['overview', [STATE_DIR, 'overview.tsv']],
  ['units', [STATE_DIR, 'units.tsv']],
  ['cities', [STATE_DIR, 'cities.tsv']],
  ['diplomacy', [STATE_DIR, 'diplomacy.tsv']],
  ['known_tiles', [STATE_DIR, 'map.txt']],
  ['map_tiles', [STATE_DIR, 'map.txt']],
  ['tile_window', [STATE_DIR, 'map.txt']],
  ['pregame_nations', [CACHE_DIR, 'nations.tsv']],
  ['pregame_styles', [CACHE_DIR, 'styles.tsv']],
  ['governments', [CACHE_DIR, 'governments.tsv']],
]);

/** `_SECTION_TITLES` — how a section names itself in the page note and digest. */
export const SECTION_TITLES: ReadonlyMap<string, string> = new Map([
  ['overview', 'overview'],
  ['units', 'units'],
  ['cities', 'cities'],
  ['diplomacy', 'diplomacy'],
  ['known_tiles', 'map'],
  ['map_tiles', 'map'],
  ['tile_window', 'map'],
  ['pregame_nations', 'nations'],
  ['pregame_styles', 'styles'],
  ['governments', 'governments'],
]);

/** `_REPLACE_ONLY` — sections a page always replaces rather than merges into. */
export const REPLACE_ONLY: ReadonlySet<string> = new Set(['overview']);

/** `_DEFAULT_PROBABILITY` — the probability an options file never prints. */
export const DEFAULT_PROBABILITY: Readonly<Record<string, unknown>> = {
  kind: 'exact',
  minimum_percent: 100,
  maximum_percent: 100,
};

/** `_DEFAULT_COMMAND_CARD` — the header card when the caller supplies none. */
export const DEFAULT_COMMAND_CARD: ReadonlyArray<string> = [
  'just turn                              turn briefing',
  'just state --section SECTION           one state section',
  'just legal --actor_id ACTOR_ID         one actor\'s option catalog',
  'just batch --action_id ID [--arguments JSON]',
  'just wait                              block until the next phase',
];

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

/** `_error` — every failure this layer raises is prefixed `state mirror: `. */
export const mirrorError = (message: string): PlayerError =>
  playerError(`state mirror: ${message}`);

// ---------------------------------------------------------------------------
// The MISSING sentinel
// ---------------------------------------------------------------------------

/**
 * `_MISSING` — "the payload did not carry this key", distinct from a carried
 * `null`.  A renderer never substitutes a plausible default for either, but
 * `_pair` treats the two differently, so the distinction has to survive.
 */
export const MISSING: unique symbol = Symbol('state-mirror/missing');
export type Missing = typeof MISSING;

// ---------------------------------------------------------------------------
// Revisions
// ---------------------------------------------------------------------------

/** The `(turn, revision)` pair every mirror file is stamped with. */
export interface MirrorRevision {
  readonly turn: number;
  readonly revision: number;
}

/** `_revision_pair` — refuse a payload that carries no state revision counters. */
export const revisionPair = (revision: unknown): Effect.Effect<MirrorRevision, PlayerError> => {
  const turn = dig(revision, 'turn');
  const number = dig(revision, 'revision');
  return isExactInteger(turn) && isExactInteger(number)
    ? Effect.succeed({ turn, revision: number })
    : Effect.fail(mirrorError('payload carries no state revision counters'));
};

const isExactInteger = (value: unknown): value is number =>
  typeof value === 'number' && Number.isInteger(value);

/** `_rev_line` — the stamp every projection file opens with. */
export const revLine = (revision: MirrorRevision | null): string =>
  revision === null ? '# rev ? turn ?' : `# rev ${revision.revision} turn ${revision.turn}`;

/** `_newer` — CPython's `(revision, turn)` lexicographic comparison. */
export const isNewer = (current: MirrorRevision, prior: MirrorRevision | null): boolean =>
  prior === null ||
  (current.revision === prior.revision
    ? current.turn > prior.turn
    : current.revision > prior.revision);

/** Tuple equality, the `current != prior` of `_stale`. */
export const sameRevision = (
  left: MirrorRevision | null,
  right: MirrorRevision | null
): boolean =>
  left === null || right === null
    ? left === right
    : left.turn === right.turn && left.revision === right.revision;

/** `_stale` — true when the mirror already holds a strictly newer revision. */
export const isStale = (current: MirrorRevision, prior: MirrorRevision | null): boolean =>
  prior !== null && !sameRevision(current, prior) && !isNewer(current, prior);

/**
 * The re-verification line `show` prefixes a lagging section with — the whole
 * CPython sentence, `client.py:11289-11292`.  Choosing *which* mirror file's
 * stamp is `rendered` is `_show_staleness`' own scan and belongs to U09; the
 * comparison and the text live here because the stamp does.
 */
export const staleLine = (rendered: MirrorRevision, now: MirrorRevision): string =>
  `stale: rendered at rev${rendered.revision}, now rev${now.revision} — ` +
  'aliases will be re-verified by meaning on use';

/**
 * `_show_staleness`' predicate: the rendered stamp is behind the live revision.
 * CPython compares the `(revision, turn)` order, which is exactly `isNewer`.
 */
export const isBehind = (rendered: MirrorRevision, now: MirrorRevision): boolean =>
  isNewer(now, rendered);

// ---------------------------------------------------------------------------
// Value helpers
// ---------------------------------------------------------------------------

/** `_dig` — walk a payload, returning {@link MISSING} at the first absent key. */
export const dig = (value: unknown, ...keys: ReadonlyArray<string>): unknown => {
  const walk = (current: unknown, remaining: ReadonlyArray<string>): unknown => {
    const [key, ...rest] = remaining;
    if (key === undefined) return current;
    if (!isJsonObject(current) || !Object.hasOwn(current, key)) return MISSING;
    return walk(current[key], rest);
  };
  return walk(value, keys);
};

/**
 * CPython's `str(value)` for the scalars a validated payload carries.  JSON has
 * one number type in JavaScript, which matches `_cell`'s own
 * integral-float-to-int conversion; see NOTES.md §2 for the residue.
 */
const pyStr = (value: unknown): string => {
  if (typeof value === 'string') return value;
  if (typeof value === 'number') return String(value);
  if (typeof value === 'bigint') return value.toString();
  if (value === null) return 'None';
  if (value === undefined) return 'None';
  // Containers never reach `_cell` on any tested path; JSON is the closest
  // stable rendering, and it stays sanitized by the caller below.
  return JSON.stringify(value) ?? String(value);
};

/**
 * CPython counts text in code points, not UTF-16 code units.  Every cap, width
 * and slice in this file goes through these three helpers, because the
 * difference is not cosmetic: `"x".slice` on a code-unit boundary splits an
 * astral character into a lone surrogate, which `PrivateFs.writeText` then
 * encodes as U+FFFD — a projection file that differs from CPython's byte for
 * byte, on any server-supplied name that carries an emoji.
 */
export const codePoints = (text: string): ReadonlyArray<string> => Array.from(text);

/** CPython's `len(text)`. */
export const codeLength = (text: string): number => codePoints(text).length;

/** CPython's `text[start:end]`. */
export const codeSlice = (text: string, start: number, end?: number): string =>
  codePoints(text).slice(start, end).join('');

/** CPython's `text.ljust(width)` — padded by code points, never code units. */
export const ljust = (text: string, width: number): string =>
  text + ' '.repeat(Math.max(0, width - codeLength(text)));

/**
 * CPython's whitespace class, which is neither JavaScript's `\s` nor a subset
 * of it: Python strips `\x1c-\x1f` and `\x85` and does *not* strip `﻿`.
 *
 * Exported as a character-class *body* because it is also what `re`'s `\s` and
 * `\S` mean to Python, and this directory's parse regexes must use it rather
 * than `\s`/`\S`.  A server-supplied terrain name is written into
 * `state/map.txt` unsanitized, so a name carrying U+001F or U+FEFF decides
 * whether a line reads as a grid row — and the two engines disagree in both
 * directions.
 */
export const PY_SPACE_CLASS =
  '\\t\\n\\v\\f\\r\\x1c-\\x1f \\x85\\xa0\\u1680\\u2000-\\u200a\\u2028\\u2029\\u202f\\u205f\\u3000';

const PY_RSTRIP_RE = new RegExp(`[${PY_SPACE_CLASS}]+$`, 'u');
const PY_LSTRIP_RE = new RegExp(`^[${PY_SPACE_CLASS}]+`, 'u');
const PY_RUN_RE = new RegExp(`[${PY_SPACE_CLASS}]{2,}`, 'gu');

/**
 * CPython's `str.rstrip()`.  Exported (with {@link strip}) because every parser
 * in this directory reads text a server supplied: `.trim()` would strip U+FEFF
 * that CPython keeps and keep U+001C-U+001F / U+0085 that CPython strips, so a
 * name carrying either would round-trip to a *different* string than the one
 * `cell` wrote and every refresh would report a phantom change.
 */
const rstrip = (text: string): string => text.replace(PY_RSTRIP_RE, '');

/** CPython's `str.strip()`. */
const strip = (text: string): string => rstrip(text).replace(PY_LSTRIP_RE, '');

/** `_cell` — render one table cell: sanitized, capped, never empty. */
export const cell = (value: unknown): string => {
  if (value === MISSING || value === null || value === undefined) return '-';
  if (value === true) return 'yes';
  if (value === false) return 'no';
  const collapsed = strip(pyStr(value).replace(CONTROL_RE, ' ')).replace(PY_RUN_RE, ' ');
  const capped =
    codeLength(collapsed) > MAX_CELL
      ? `${codeSlice(collapsed, 0, MAX_CELL - 1)}~`
      : collapsed;
  return capped === '' ? '-' : capped;
};

/** `_pair` — two cells joined, or `-` when either side is absent. */
export const pair = (first: unknown, second: unknown, separator = '/'): string =>
  first === MISSING || second === MISSING ? '-' : `${cell(first)}${separator}${cell(second)}`;

/** `_position` — the `x,y` cell. */
export const position = (item: unknown): string => pair(dig(item, 'x'), dig(item, 'y'), ',');

/** `_handle` — the fallback display key (`u.3f9a2c`) for an unaliased entity. */
export const handle = (kind: string, identifier: unknown, width: number): string => {
  const text = cell(identifier);
  const digits = text.replace(/^[a-z_]+_/, '');
  return digits === '' ? text : `${kind}.${codeSlice(digits, 0, width)}`;
};

/** A CLI alias table: opaque id → short alias. */
export type MirrorAliases = Readonly<Record<string, string>>;

/**
 * `_alias_map` — map opaque ids to short names, preferring the CLI's alias
 * layer.  The mirror never *mints* an `aN` name; a fallback handle is a display
 * key only, so using one fails closed exactly like a stale alias.
 */
export const aliasMap = (
  kind: string,
  identifiers: ReadonlyArray<unknown>,
  aliases: MirrorAliases | null | undefined
): ReadonlyMap<string, string> => {
  const resolved = new Map<string, string>();
  const unresolved: string[] = [];
  for (const identifier of identifiers) {
    const text = cell(identifier);
    // CPython's `text in aliases` is own-keys-only.  A bare `aliases[text]`
    // would resolve a server-supplied id spelled `constructor` to
    // `Object.prototype.constructor` and render the host runtime's function
    // source into the alias column of `state/units.tsv`.
    const aliased =
      aliases !== null && aliases !== undefined && Object.hasOwn(aliases, text);
    if (aliased) {
      resolved.set(text, cell(aliases[text]));
    } else if (!resolved.has(text)) {
      unresolved.push(text);
    }
  }
  for (const width of HANDLE_WIDTHS) {
    const candidates = new Map(unresolved.map((text) => [text, handle(kind, text, width)]));
    if (new Set(candidates.values()).size === candidates.size) {
      for (const [text, name] of candidates) resolved.set(text, name);
      return resolved;
    }
  }
  // Identical ids cannot collide, so this is unreachable in practice.
  for (const text of unresolved) resolved.set(text, text);
  return resolved;
};

/** `_file_name` — reduce an alias to one safe path component. */
export const fileName = (alias: string): Effect.Effect<string, PlayerError> => {
  // The `u` flag makes the class match code points, so one astral character
  // becomes one `_` exactly as CPython's `re.sub` does — not two.
  const name = alias.replace(/[^A-Za-z0-9._-]/gu, '_').replace(/^[._-]+|[._-]+$/g, '');
  return name === ''
    ? Effect.fail(mirrorError('an actor alias has no usable file name'))
    : Effect.succeed(name.slice(0, 64));
};

// ---------------------------------------------------------------------------
// The mirror directory
// ---------------------------------------------------------------------------

/**
 * `pathlib.PurePosixPath._parse_path` — root plus the non-`.` components.
 *
 * `node:path` is not a substitute here.  `path.basename('/w/x/.')` is `'.'`
 * where pathlib's `name` is `'x'`, and pathlib keeps *exactly two* leading
 * slashes as the root (POSIX implementation-defined `//host`) while collapsing
 * one or three-or-more.  `mirror_dir` is the root of the whole mirror layout,
 * so a component-level disagreement puts every projection file in a directory
 * CPython would never read.
 */
interface PyPath {
  readonly root: string;
  readonly tail: ReadonlyArray<string>;
}

const pyParsePath = (text: string): PyPath => {
  if (text === '') return { root: '', tail: [] };
  // `posixpath.splitroot`: '//x' keeps both slashes, '/x' and '///x' do not.
  const root = !text.startsWith('/')
    ? ''
    : text.startsWith('//') && !text.startsWith('///')
      ? '//'
      : '/';
  const tail = text
    .slice(root.length)
    .split('/')
    .filter((part) => part !== '' && part !== '.');
  return { root, tail };
};

/** `_format_parsed_parts` + `PurePath.__str__` — never the empty string. */
const pyFormatPath = (parsed: PyPath): string => {
  const joined = `${parsed.root}${parsed.tail.join('/')}`;
  return joined === '' ? '.' : joined;
};

/** `PurePath.name` — the last component, or `''` for a rootless-tail path. */
const pyName = (parsed: PyPath): string => parsed.tail[parsed.tail.length - 1] ?? '';

/** CPython's `str.lstrip('.')`. */
const lstripDots = (text: string): string => text.replace(/^\.+/, '');

/**
 * `PurePath.suffix` as CPython 3.14 defines it — the rule changed in 3.14 and
 * this repo runs 3.14.6.  Leading dots are dropped *before* the search, so
 * `'..hidden'` and `'...'` have no suffix at all, while a trailing dot now is
 * one: `'a.'` -> `'.'`.
 */
const pySuffix = (name: string): string => {
  const stripped = lstripDots(name);
  const index = stripped.lastIndexOf('.');
  return index === -1 ? '' : stripped.slice(index);
};

/**
 * `PurePath.stem` (3.14): everything before the last dot, but only when what
 * remains still holds a non-dot character — so `'x..'` stems to `'x.'` and
 * `'...'` stems to itself.
 */
const pyStem = (name: string): string => {
  const index = name.lastIndexOf('.');
  if (index === -1) return name;
  const stem = name.slice(0, index);
  return lstripDots(stem) === '' ? name : stem;
};

/** `mirror_dir` — the projection directory that belongs to one session file. */
export const mirrorDir = (sessionPath: string): Effect.Effect<string, PlayerError> =>
  Effect.suspend(() => {
    const parsed = pyParsePath(sessionPath);
    const base = pyName(parsed);
    const name = pySuffix(base) !== '' ? pyStem(base) : `${base}-mirror`;
    // CPython's own guard.  Under 3.14's stem rule it is unreachable — a
    // non-empty suffix implies a stem with a non-dot character — but it is the
    // line the source carries, and it fails closed if the rule moves again.
    if (name === '' || name === '.' || name === '..' || name.includes('/')) {
      return Effect.fail(mirrorError('the session file name has no mirror directory'));
    }
    // `Path.with_name` on a path with no components (`'/'`, `'.'`, `''`).
    // CPython raises `ValueError` there rather than the mirror's own error;
    // see NOTES.md §12.  `_session_path` cannot produce such a path.
    return parsed.tail.length === 0
      ? Effect.fail(mirrorError('the session file name has no mirror directory'))
      : Effect.succeed(
          pyFormatPath({ root: parsed.root, tail: [...parsed.tail.slice(0, -1), name] })
        );
  });

// ---------------------------------------------------------------------------
// Private, atomic file access
// ---------------------------------------------------------------------------

const mirrorFile = (dir: string, relative: ReadonlyArray<string>): string =>
  path.join(dir, ...relative);

/**
 * `_write` — one atomic, private, newline-terminated projection write.
 *
 * Returns the absolute path written, so an entry point can report the files it
 * touched (`ApiTests.test_update_from_page_returns_the_files_it_wrote`).
 */
export const writeMirror = (
  dir: string,
  relative: ReadonlyArray<string>,
  text: string
): Effect.Effect<string, PlayerError, PrivateFs> =>
  Effect.flatMap(PrivateFs, (files) => {
    // Mirror files are agent-facing guidance, so command mentions follow
    // PLAY_PROG like stdout does (src/services/prog-prefix.ts).  Canonical
    // bodies and persisted receipts go through PrivateFs directly and are
    // never rewritten.
    const spelled = rewriteProgMentions(text);
    return files.writeText(mirrorFile(dir, relative), spelled.endsWith('\n') ? spelled : `${spelled}\n`);
  });

/** `_read` — a projection's text, or `null` when absent or unreadable. */
export const readMirror = (
  dir: string,
  relative: ReadonlyArray<string>
): Effect.Effect<string | null, never, PrivateFs> =>
  Effect.flatMap(PrivateFs, (files) =>
    Effect.orElseSucceed(
      files.readText(mirrorFile(dir, relative), 'state mirror file'),
      (): string | null => null
    )
  );

/** `_append_private_text` against a projection file. */
export const appendMirror = (
  dir: string,
  relative: ReadonlyArray<string>,
  text: string
): Effect.Effect<string, PlayerError, PrivateFs> =>
  Effect.flatMap(PrivateFs, (files) => files.appendText(mirrorFile(dir, relative), text));

// ---------------------------------------------------------------------------
// The client.py bridge
// ---------------------------------------------------------------------------

/** `_mirror_path` — the mirror directory of one session file. */
export const mirrorPath = mirrorDir;

/**
 * `_mirror` — a projection never fails a command.
 *
 * CPython swallows every exception and warns on stderr.  The port swallows the
 * typed error channel and any defect, so a mirror failure can never change an
 * exit code.
 */
export const mirrorGuard = <A, E extends { readonly message: string }, R>(
  projection: Effect.Effect<A, E, R>
): Effect.Effect<void, never, R> =>
  Effect.catchAllDefect(
    Effect.catchAll(Effect.asVoid(projection), (error) => warnMirror(error.message)),
    (defect) => warnMirror(defect instanceof Error ? defect.message : String(defect))
  );

const warnMirror = (message: string): Effect.Effect<void> =>
  Console.error(`warning: the local state mirror was not updated: ${message}`);

/** `_mirror_text` — read one projection, or `null` when this seat has none. */
export const mirrorText = (
  sessionPath: string,
  parts: ReadonlyArray<string>
): Effect.Effect<string | null, never, PrivateFs> =>
  Effect.flatMap(
    Effect.orElseSucceed(mirrorDir(sessionPath), (): string | null => null),
    (dir) => (dir === null ? Effect.succeed(null) : readMirror(dir, parts))
  );

/**
 * `MIRROR_PHASE_RE` — how `state/header.txt` spells the phase line.  The mirror
 * renders booleans through `cell`, so `active` is `yes`/`no`, never
 * `True`/`False`.
 */
export const MIRROR_PHASE_RE =
  /^phase\s+(\S+) · turn \S+ phase \S+ · active (\S+)\s*$/m;

/** `V2_MIRROR_INACTIVE` — how the header spells "not this seat's phase". */
export const V2_MIRROR_INACTIVE: ReadonlySet<string> = new Set(['no', 'false']);

/**
 * `V2_PHASE_STALLED` — phase states in which an order cannot land because this
 * seat's turn is over or has not begun.  `synchronizing` is deliberately
 * absent: that is the lobby, where the remedy is `just start`, not `just wait`.
 */
export const V2_PHASE_STALLED: ReadonlySet<string> = new Set([
  'inactive_done',
  'ending',
  'ambiguous_ending',
  'native_phase',
  'phase_not_ready',
  'terminalizing',
]);

/** `_cached_phase_note` — say, from the mirror alone, that the phase is dead. */
export const cachedPhaseNote = (
  sessionPath: string
): Effect.Effect<string, never, PrivateFs> =>
  Effect.map(mirrorText(sessionPath, HEADER_FILE), (header) => {
    const match = MIRROR_PHASE_RE.exec(header ?? '');
    if (match === null) return '';
    const state = match[1] ?? '';
    const active = match[2] ?? '';
    return V2_PHASE_STALLED.has(state) ||
      (V2_MIRROR_INACTIVE.has(active.toLowerCase()) && state === 'awaiting_agent')
      ? `your phase is not active (state ${state}) — just wait`
      : '';
  });

// ---------------------------------------------------------------------------
// Small shared text helpers the sibling modules need
// ---------------------------------------------------------------------------

/**
 * CPython's `str.splitlines()`.  `"a\n".splitlines()` is `["a"]`, which
 * `String.split` is not.
 *
 * The boundary set is CPython's complete one, including the three information
 * separators U+001C-U+001E \u2014 a mirror file written by an older client (or by
 * hand) can carry them, and treating one as ordinary text merges two rows of
 * `state/units.tsv` into a single row on read, which then rewrites the file.
 */
export const splitLines = (text: string): ReadonlyArray<string> => {
  const parts = text.split(/\r\n|[\n\r\v\f\x1c\x1d\x1e\u0085\u2028\u2029]/u);
  return parts.length > 0 && parts[parts.length - 1] === '' ? parts.slice(0, -1) : parts;
};

export { rstrip, strip };

/** `_number` — a real number, never a bool, else nothing. */
export const numberValue = (value: unknown): number | null =>
  typeof value === 'number' && Number.isFinite(value) ? value : null;

/** `_text` — a sanitized string, or nothing when it sanitizes to `-`. */
export const textValue = (value: unknown): string | null => {
  if (typeof value !== 'string') return null;
  const rendered = cell(value);
  return rendered === '-' ? null : rendered;
};
