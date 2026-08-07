/**
 * `just show` — the offline read of this seat's state mirror.
 *
 * Ports `play/client.py:11169-11435`: `V2_SHOW_FILES`, `V2_SHOW_ROW_FILES`,
 * `SHOW_NAME_RE`, `_show_option_files`, `_show_catalog`, `_show_present`,
 * `_show_empty`, `_show_sources`, `_show_staleness`, `_show_default`,
 * `_show_rows`, `_show_named`, `NESTED_QUANTIFIER_RE`,
 * `V2_SHOW_GREP_BUDGET_S` and `_show_grep`.
 *
 * Every byte this prints was already written by a command that ingested a page
 * for this seat, so a read can never widen what the seat knows past fog — and it
 * costs no server round trip.  The private `.v2-state` cache is deliberately
 * unreachable from here: it is a *sibling* of the mirror directory, not a member
 * of it, and `SHOW_NAME_RE` rejects the traversal that would reach it anyway.
 *
 * The staleness banner is computed at read time and never written back:
 * rewriting a projection to say it is old would change the very stamp being
 * judged.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { Effect, Either } from 'effect';
import { ENTITY_ALIAS_RE } from 'src/constants';
import { PY_IGNORECASE, compilePythonRegex } from 'src/render/show-regex';
import { casefold, pyIsPrintable } from 'src/render/show-unicode';
import { PlayerError, playerError } from 'src/errors';
import { PrivateFs } from 'src/services/private-fs';
import {
  OPTIONS_DIR,
  STATE_DIR,
  isBehind,
  mirrorDir,
  mirrorText,
  parseTable,
  splitLines,
  staleLine,
  strip,
  type MirrorRevision,
} from 'src/services/mirror';
import { SessionStore, type Session } from 'src/services/session-store';

// ---------------------------------------------------------------------------
// The catalog
// ---------------------------------------------------------------------------

/**
 * `V2_SHOW_FILES` — the fixed half of the catalog, in the order `show` lists it.
 *
 * Insertion order is byte-diff surface twice over: it is the order of the
 * `files:` line and the order `--grep` walks the mirror in.
 */
export const V2_SHOW_FILES: ReadonlyMap<string, ReadonlyArray<string>> = new Map([
  ['header', [STATE_DIR, 'header.txt']],
  // The machine-readable half of the header: whose turn it is, right now.
  ['phase', [STATE_DIR, 'phase.json']],
  ['overview', [STATE_DIR, 'overview.tsv']],
  ['units', [STATE_DIR, 'units.tsv']],
  ['cities', [STATE_DIR, 'cities.tsv']],
  ['map', [STATE_DIR, 'map.txt']],
  ['yields', [STATE_DIR, 'yields.tsv']],
  ['diplomacy', [STATE_DIR, 'diplomacy.tsv']],
  ['delta', [STATE_DIR, 'delta.md']],
  ['nations', ['cache', 'nations.tsv']],
  ['styles', ['cache', 'styles.tsv']],
  ['governments', ['cache', 'governments.tsv']],
]);

/**
 * `V2_SHOW_ROW_FILES` — the tables an entity alias is looked up in.  A relation
 * is addressed by alias exactly like a unit or a city, so `just show r1` reaches
 * the row that names the counterpart and its open meeting.
 */
export const V2_SHOW_ROW_FILES: ReadonlyArray<string> = ['units', 'cities', 'diplomacy'];

export const V2_SHOW_MAX_MATCHES = 200;

export const SHOW_NAME_RE = /^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/;

/** One catalog entry: the label `show` prints and the file it reads. */
export interface ShowCatalogEntry {
  readonly label: string;
  readonly parts: ReadonlyArray<string>;
}

/** A catalog entry that turned out to exist, with the text it holds. */
export interface ShowPresentFile extends ShowCatalogEntry {
  readonly text: string;
}

/**
 * The three row tables, derived from the one catalog rather than restated, so a
 * lookup is total without a fallback path nothing can reach.  `V2_SHOW_FILES`
 * already lists them in `V2_SHOW_ROW_FILES`' order.
 */
const ROW_SOURCES: ReadonlyArray<ShowCatalogEntry> = [...V2_SHOW_FILES]
  .filter(([label]) => V2_SHOW_ROW_FILES.includes(label))
  .map(([label, parts]) => ({ label, parts }));

// ---------------------------------------------------------------------------
// _show_option_files / _show_catalog / _show_present
// ---------------------------------------------------------------------------

const codePointSort = (left: string, right: string): number =>
  left < right ? -1 : left > right ? 1 : 0;

/**
 * `_show_option_files` — list the per-actor option projections **without
 * following a symlink**.
 *
 * The listing walks in through `PrivateFs.openDirectory`, which refuses every
 * symlinked component, so a planted `state/options -> /etc` cannot turn a read
 * of this seat's mirror into a read of somebody else's files.  Any failure at
 * all is an empty list: an unreadable options directory must not stop `show`
 * from printing the files it can read perfectly well.
 */
export const showOptionFiles = (
  sessionPath: string
): Effect.Effect<ReadonlyArray<string>, never, PrivateFs> =>
  Effect.orElseSucceed(
    Effect.gen(function* () {
      const files = yield* PrivateFs;
      const dir = yield* mirrorDir(sessionPath);
      const { relative } = yield* files.resolve(path.join(dir, STATE_DIR, OPTIONS_DIR));
      const opened = yield* files.openDirectory(relative, { create: false });
      const names = yield* Effect.try({
        try: () => fs.readdirSync(opened),
        catch: () => playerError('the options directory is not readable'),
      });
      return names
        .filter((name) => name.endsWith('.txt') && !name.startsWith('.'))
        .sort(codePointSort);
    }),
    (): ReadonlyArray<string> => []
  );

/** `_show_catalog` — the fixed files plus one entry per option projection. */
export const showCatalog = (
  sessionPath: string
): Effect.Effect<ReadonlyArray<ShowCatalogEntry>, never, PrivateFs> =>
  Effect.map(showOptionFiles(sessionPath), (names) => [
    ...[...V2_SHOW_FILES].map(([label, parts]) => ({ label, parts })),
    ...names.map((name) => ({
      label: `options/${name.slice(0, -4)}`,
      parts: [STATE_DIR, OPTIONS_DIR, name],
    })),
  ]);

/** `_show_present` — the catalog entries this seat has actually written. */
export const showPresent = (
  sessionPath: string
): Effect.Effect<ReadonlyArray<ShowPresentFile>, never, PrivateFs> =>
  Effect.gen(function* () {
    const catalog = yield* showCatalog(sessionPath);
    const found: ShowPresentFile[] = [];
    for (const entry of catalog) {
      const text = yield* mirrorText(sessionPath, entry.parts);
      if (text !== null) found.push({ ...entry, text });
    }
    return found;
  });

// ---------------------------------------------------------------------------
// _show_empty
// ---------------------------------------------------------------------------

/**
 * `_show_empty` — the refusal an empty mirror earns, as a failing effect.
 *
 * The remedy has to run in the phase the agent is actually in.  A seat still in
 * the lobby has no units and no briefing to write, and `just turn` there returns
 * the lobby card without writing a file — so the command named *first* is the
 * one that projects a page in either phase.
 */
export const showEmpty = (sessionPath: string): Effect.Effect<never, PlayerError> =>
  Effect.flatMap(mirrorDir(sessionPath), (dir) =>
    Effect.fail(
      playerError(
        'this seat has no local state mirror yet; run ' +
          '`just state --section overview` (it works in the lobby too, and ' +
          '`just turn` writes the rest once the game is running); the files ' +
          `then appear under ${dir}`
      )
    )
  );

// ---------------------------------------------------------------------------
// _show_sources / _show_staleness
// ---------------------------------------------------------------------------

/** `_show_sources` — name the mirror files one `show` invocation rendered. */
export const showSources = (
  sessionPath: string,
  name: string,
  pattern: string
): Effect.Effect<ReadonlyArray<ReadonlyArray<string>>, never, PrivateFs> => {
  if (pattern !== '' || name === '') {
    return Effect.map(showPresent(sessionPath), (present) =>
      present.map((entry) => entry.parts)
    );
  }
  const parts = V2_SHOW_FILES.get(name);
  return Effect.succeed(
    parts !== undefined
      ? [parts]
      : [
          ...ROW_SOURCES.map((entry) => entry.parts),
          [STATE_DIR, OPTIONS_DIR, `${name}.txt`],
        ]
  );
};

/**
 * CPython's `order(stamp)`: `_mirror_table` reports `(turn, revision)` and
 * revision is the monotonic half, so the oldest projection is the one with the
 * smallest revision.
 */
const olderThan = (left: MirrorRevision, right: MirrorRevision): boolean =>
  left.revision === right.revision ? left.turn < right.turn : left.revision < right.revision;

/**
 * `_show_staleness` — warn when what was just printed predates the revision this
 * seat knows.
 *
 * Every rendered file stamps the revision it was written at, and semantic alias
 * continuity means an `aN` read from an older file still resolves to the move it
 * named — but only because it is re-verified by meaning on use.  Saying so is
 * the difference between a projection and a claim about now.
 *
 * A banner is an annotation, never a gate: an unreadable private cache must not
 * stop `show` from printing files it can read perfectly well, so every failure
 * of the `.v2-state` read collapses to the empty string.
 */
export const showStaleness = (
  sessionPath: string,
  session: Session,
  sources: ReadonlyArray<ReadonlyArray<string>>
): Effect.Effect<string, never, SessionStore | PrivateFs> =>
  Effect.orElseSucceed(
    Effect.gen(function* () {
      const store = yield* SessionStore;
      const current = (yield* store.readState(sessionPath, session)).last_revision;
      if (current === null) return '';
      const now: MirrorRevision = { turn: current.turn, revision: current.revision };
      const stamps: MirrorRevision[] = [];
      for (const parts of sources) {
        const found = parseTable(yield* mirrorText(sessionPath, parts)).revision;
        if (found !== null) stamps.push(found);
      }
      const rendered = stamps.reduce<MirrorRevision | null>(
        (oldest, found) => (oldest === null || olderThan(found, oldest) ? found : oldest),
        null
      );
      return rendered === null || !isBehind(rendered, now) ? '' : staleLine(rendered, now);
    }),
    () => ''
  );

// ---------------------------------------------------------------------------
// _show_default
// ---------------------------------------------------------------------------

/** The two sections bare `show` prints in full before the catalog line. */
const INLINE_SECTIONS: ReadonlySet<string> = new Set(['header', 'delta']);

/** `_show_default` — the summary of what this seat has written. */
export const showDefault = (
  sessionPath: string
): Effect.Effect<ReadonlyArray<string>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const present = yield* showPresent(sessionPath);
    if (present.length === 0) return yield* showEmpty(sessionPath);
    const inline = present
      .filter((entry) => INLINE_SECTIONS.has(entry.label))
      .flatMap((entry) => [...splitLines(entry.text), '']);
    return [
      ...inline,
      `files: ${present.map((entry) => entry.label).join(' ')}`,
      'read one with `just show NAME`, search with `just show --grep PATTERN`',
    ];
  });

// ---------------------------------------------------------------------------
// _show_rows / _show_named
// ---------------------------------------------------------------------------

/**
 * `_show_rows` — the mirror rows an entity alias names, from the tables.
 *
 * The alias column is compared with `line.split("\t", 1)[0].strip()`, and
 * `strip` is U04's — CPython's whitespace class, which is neither a superset nor
 * a subset of `String.prototype.trim()`'s: `str.strip()` removes U+0085 and
 * U+001C–U+001F, which `trim` keeps, and keeps U+FEFF, which `trim` removes.
 * Both directions decide whether a row is printed, and neither changes the
 * printed bytes — the line goes out exactly as the projection holds it,
 * padding and all.
 */
export const showRows = (
  sessionPath: string,
  alias: string
): Effect.Effect<ReadonlyArray<string>, never, PrivateFs> =>
  Effect.gen(function* () {
    const lines: string[] = [];
    for (const entry of ROW_SOURCES) {
      const text = yield* mirrorText(sessionPath, entry.parts);
      if (text === null) continue;
      for (const line of splitLines(text)) {
        if (strip(line.split('\t')[0] ?? '') === alias) lines.push(`${entry.label}: ${line}`);
      }
    }
    return lines;
  });

const NAME_REFUSAL = playerError(
  'just show takes one mirror file name or one entity alias, ' +
    'for example `just show units` or `just show u1`'
);

const missingProjection = (name: string): PlayerError =>
  playerError(`this seat has no ${name} projection yet; run \`just turn\` to write one`);

/**
 * The refusal for a name the mirror does not hold.
 *
 * A remedy must be runnable: `just legal --actor_id research --all` cannot work,
 * so only an alias-shaped name is offered that repair.
 */
const unknownName = (name: string): PlayerError =>
  ENTITY_ALIAS_RE.test(name)
    ? playerError(
        `the mirror holds nothing named ${name}; run ` +
          `\`just legal --actor_id ${name} --all\` to write ` +
          `state/options/${name}.txt, or \`just show\` to list what is there`
      )
    : playerError(
        `${name} is not a mirror file or an entity alias; the file names are ` +
          `${[...V2_SHOW_FILES.keys()].sort(codePointSort).join(' ')}, an alias ` +
          'looks like u1 or c1, and `just show` lists what this seat has written'
      );

/** `_show_named` — one mirror file, or one alias's rows plus its option file. */
export const showNamed = (
  sessionPath: string,
  name: string
): Effect.Effect<ReadonlyArray<string>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    if (!SHOW_NAME_RE.test(name)) return yield* Effect.fail(NAME_REFUSAL);
    const parts = V2_SHOW_FILES.get(name);
    if (parts !== undefined) {
      const text = yield* mirrorText(sessionPath, parts);
      if (text === null) return yield* Effect.fail(missingProjection(name));
      return splitLines(text);
    }
    const options = yield* mirrorText(sessionPath, [STATE_DIR, OPTIONS_DIR, `${name}.txt`]);
    const rows = yield* showRows(sessionPath, name);
    const lines =
      options === null
        ? [...rows]
        : [...rows, ...(rows.length > 0 ? [''] : []), ...splitLines(options)];
    if (lines.length === 0) return yield* Effect.fail(unknownName(name));
    return lines;
  });

// ---------------------------------------------------------------------------
// _show_grep
// ---------------------------------------------------------------------------

/**
 * `NESTED_QUANTIFIER_RE` — one catastrophic-backtracking shape, refused by name.
 *
 * A quantified group that is itself quantified is one such shape, but it is not
 * the only one: `(a|aa)+$` overlaps by alternation instead and takes exponential
 * time in the line length with no nested quantifier anywhere.  Enumerating
 * shapes therefore cannot close the class, so the default search is a plain
 * case-insensitive substring — which has no backtracking at all — and the regex
 * engine is opt-in behind `--regex`, guarded by this pattern *and* by a
 * wall-clock budget.  A wedged `just show` costs the seat its turn deadline, and
 * the mirror files are the one surface the agent is told is free.
 */
export const NESTED_QUANTIFIER_RE = /\([^()]*[*+}][^()]*\)\s*[*+{]/;

/**
 * `V2_SHOW_GREP_BUDGET_S` — the whole-command budget for `--regex`.
 *
 * One pathological pattern cannot be interrupted mid-match, so the budget is
 * checked between lines: it bounds the damage to roughly one line's worth of
 * backtracking rather than the whole mirror's, and the refusal names the
 * flagless form that cannot backtrack.
 */
export const V2_SHOW_GREP_BUDGET_S = 2.0;

/**
 * CPython's `repr()` for the pattern echoed back by `no mirror line matches …`.
 *
 * Python prefers single quotes and switches to double quotes only when the text
 * holds a single quote and no double quote; backslash, the chosen quote and the
 * three C escapes `repr` uses are escaped by name.
 *
 * Everything else turns on `str.isprintable()`, **not** on "is it a control
 * character": a no-break space is escaped `\xa0`, a byte order mark
 * `\ufeff` and a tag character `\U000e0001`, while printable non-ASCII
 * (`naïve`, `·`, `Große`) is left exactly as it is.  A pattern reaching this
 * line has already been through `strip`, which removes U+0085 and U+00A0 from
 * the ends but from nowhere else, so the interior cases are all reachable.  The
 * escape width is CPython's too: `\xNN` below U+0100, `\uXXXX` below U+10000,
 * `\UXXXXXXXX` above it.  Compared against `repr()` on every code point and on
 * 20,000 generated strings: zero mismatches (NOTES.md §19.11).
 */
const pyEscape = (code: number): string => {
  if (code < 0x100) return `\\x${code.toString(16).padStart(2, '0')}`;
  if (code < 0x10000) return `\\u${code.toString(16).padStart(4, '0')}`;
  return `\\U${code.toString(16).padStart(8, '0')}`;
};

export const pyRepr = (text: string): string => {
  const quote = text.includes("'") && !text.includes('"') ? '"' : "'";
  const escaped = [...text]
    .map((char) => {
      if (char === '\\') return '\\\\';
      if (char === quote) return `\\${quote}`;
      if (char === '\n') return '\\n';
      if (char === '\r') return '\\r';
      if (char === '\t') return '\\t';
      const code = char.codePointAt(0) ?? 0;
      return pyIsPrintable(code) ? char : pyEscape(code);
    })
    .join('');
  return `${quote}${escaped}${quote}`;
};

export interface ShowGrepOptions {
  readonly regex: boolean;
  /** Monotonic seconds, injected so the budget refusal is testable. */
  readonly clock?: () => number;
}

const monotonicSeconds = (): number => performance.now() / 1000;

const TOO_LONG = playerError('just show --grep takes a pattern of at most 200 characters');

const nestedQuantifier = playerError(
  'just show --grep --regex refuses a quantifier applied to an ' +
    'already quantified group; drop --regex to search for the ' +
    'literal text, for example `just show --grep found_city`'
);

const budgetSpent = (pattern: string): PlayerError =>
  playerError(
    'just show --grep --regex took too long; narrow the pattern, or ' +
      `drop --regex to search for the literal text \`${pattern}\``
  );

/**
 * `re.compile(pattern, re.IGNORECASE)`, refusal and all.
 *
 * The dialect is `re`'s, not JavaScript's — `show-regex.ts` says why and what
 * it costs — so this only has to wrap the failure in CPython's sentence.  The
 * message it wraps is `str(exc)` verbatim, including the `at position N` tail,
 * for every pattern `re` itself rejects.
 */
const compileGrep = (pattern: string): Effect.Effect<RegExp, PlayerError> => {
  if (NESTED_QUANTIFIER_RE.test(pattern)) return Effect.fail(nestedQuantifier);
  const compiled = compilePythonRegex(pattern, PY_IGNORECASE);
  return Either.isRight(compiled)
    ? Effect.succeed(compiled.right)
    : Effect.fail(
        playerError(
          `just show --grep pattern is invalid: ${compiled.left.message}; drop ` +
            `--regex to search for the literal text \`${pattern}\``
        )
      );
};

/**
 * Walk the present projections, collecting at most `V2_SHOW_MAX_MATCHES + 1`
 * matches.  The extra one is how truncation is detected — CPython stops on the
 * match it cannot append, so an exactly-200-match search is *not* truncated.
 *
 * `guard` is the wall-clock budget; it is `() => null` on the literal path,
 * because CPython checks the clock only in the regex branch.
 */
const scanPresent = (
  present: ReadonlyArray<ShowPresentFile>,
  matches: (line: string) => boolean,
  guard: () => PlayerError | null
): ReadonlyArray<string> | PlayerError => {
  const collected: string[] = [];
  for (const entry of present) {
    for (const [index, line] of splitLines(entry.text).entries()) {
      const spent = guard();
      if (spent !== null) return spent;
      if (!matches(line)) continue;
      collected.push(`${entry.label}:${index + 1}: ${line}`);
      if (collected.length > V2_SHOW_MAX_MATCHES) return collected;
    }
  }
  return collected;
};

/** `_show_grep` — search every present projection, literally or by regex. */
export const showGrep = (
  sessionPath: string,
  pattern: string,
  options: ShowGrepOptions
): Effect.Effect<ReadonlyArray<string>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    // `len(pattern)` counts code points, so an astral character is one.
    if ([...pattern].length > 200) return yield* Effect.fail(TOO_LONG);
    const clock = options.clock ?? monotonicSeconds;
    const expression = options.regex ? yield* compileGrep(pattern) : null;
    // `casefold`, not `toLowerCase`: CPython folds `Große` onto `grosse` and
    // `ſ` onto `s`, and a search that lowercased instead would miss the row.
    const needle = casefold(pattern);
    const present = yield* showPresent(sessionPath);
    if (present.length === 0) return yield* showEmpty(sessionPath);
    const deadline = clock() + V2_SHOW_GREP_BUDGET_S;
    const outcome = scanPresent(
      present,
      expression === null
        ? (line) => casefold(line).includes(needle)
        : // No `g` flag, so `lastIndex` is never consulted between lines.
          (line) => expression.test(line),
      expression === null ? () => null : () => (clock() > deadline ? budgetSpent(pattern) : null)
    );
    if (outcome instanceof PlayerError) return yield* Effect.fail(outcome);
    if (outcome.length === 0) return [`no mirror line matches ${pyRepr(pattern)}`];
    return outcome.length > V2_SHOW_MAX_MATCHES
      ? [
          ...outcome.slice(0, V2_SHOW_MAX_MATCHES),
          `(stopped at ${V2_SHOW_MAX_MATCHES} matches; narrow the pattern)`,
        ]
      : outcome;
  });
