/**
 * The line-number citations in `src/` must still point at what they name.
 *
 * ## Why this file exists
 *
 * `src/` cites the Python it was transcribed from ~350 times, and several
 * modules asserted that the citations were *guarded* — `src/ids.ts` claimed "a
 * stale citation cannot go unnoticed even after the lines move", and three
 * gateway test files repeated the claim.  No test read `play/client.py` at
 * all, so the entire agent half had no citation guard whatsoever, and ten
 * separate drifts had accumulated silently.  The worst of them pointed a
 * reader at a *different, wider* constant: `src/agent/descriptor.ts` cited
 * `ACTION_KIND_RE` as `client.py:105`, which is the body of
 * `ACTION_KIND_SELECTOR_RE` — follow that pointer and you would "fix" the port
 * to accept `unit.order/move`, silently widening a validated alphabet.
 *
 * ## What is checked, and what is not
 *
 * A citation is **anchored** when a backticked Python identifier appears just
 * before it, and that identifier is defined in the file being cited:
 *
 * ```
 * `V2_ERROR_CODES` — `play/client.py:50-55`
 *  ^ anchor          ^ citation
 * ```
 *
 * For every anchored citation, the cited region must either **mention** the
 * identifier or lie **inside its definition**.  Both are legitimate ways to
 * cite: one names where a thing is defined, the other names a statement within
 * it.  Anything else means the pointer has rotted.
 *
 * This deliberately does **not** check:
 *
 * - bare continuation citations (`` `:2018` ``), which name no identifier;
 * - citations whose anchor is not defined in that file (a `play-cli` symbol,
 *   an OpenAPI schema name, a Python local);
 * - whether the cited region is the *most relevant* one — only that it is
 *   about the thing the sentence says it is about.
 *
 * So a green run means no anchored citation is wrong, not that every citation
 * is right.  {@link anchoredCount} is asserted below so the coverage cannot
 * quietly collapse to zero.
 */
import { describe, expect, test } from 'bun:test';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join } from 'node:path';

const WIRE = join(import.meta.dir, '..');
const REPO = join(WIRE, '../..');

const tsFiles = (dir: string): ReadonlyArray<string> =>
  readdirSync(dir).flatMap((entry) => {
    const full = join(dir, entry);
    return statSync(full).isDirectory() ? tsFiles(full) : full.endsWith('.ts') ? [full] : [];
  });

/** The Python files `src/` is allowed to cite, and how they may be spelled. */
const PY_FILES: Readonly<Record<string, string>> = {
  'play/client.py': 'play/client.py',
  'client.py': 'play/client.py',
  'agent_eval/replay_gateway.py': 'agent_eval/replay_gateway.py',
  'replay_gateway.py': 'agent_eval/replay_gateway.py',
  'agent_eval/supervisor.py': 'agent_eval/supervisor.py',
  'supervisor.py': 'agent_eval/supervisor.py',
  'agent_eval/full_control_v2.py': 'agent_eval/full_control_v2.py',
  'full_control_v2.py': 'agent_eval/full_control_v2.py',
  'agent_eval/save_replay.py': 'agent_eval/save_replay.py',
  'save_replay.py': 'agent_eval/save_replay.py',
};

const CANONICAL_PY = [...new Set(Object.values(PY_FILES))];

/** Ungated: a missing authority throws at module load rather than skipping. */
const linesOf = (rel: string): ReadonlyArray<string> =>
  readFileSync(join(REPO, rel), 'utf8').split('\n');

const PY_LINES = new Map(CANONICAL_PY.map((rel) => [rel, linesOf(rel)] as const));

const indentOf = (line: string): number => (/^\s*/.exec(line) ?? [''])[0].length;

/** Top-level and nested definitions: `NAME = ...`, `def NAME(`, `class NAME`. */
const definitionsOf = (rel: string): ReadonlyMap<string, number> => {
  const found = new Map<string, number>();
  (PY_LINES.get(rel) ?? []).forEach((line, index) => {
    const assignment = /^([A-Za-z_][A-Za-z0-9_]*)\s*(?::[^=]+)?=/.exec(line);
    const definition = /^\s*(?:async\s+)?def\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(/.exec(line);
    const klass = /^\s*class\s+([A-Za-z_][A-Za-z0-9_]*)/.exec(line);
    const name = assignment?.[1] ?? definition?.[1] ?? klass?.[1];
    if (name !== undefined && !found.has(name)) found.set(name, index + 1);
  });
  return found;
};

const PY_DEFS = new Map(CANONICAL_PY.map((rel) => [rel, definitionsOf(rel)] as const));

/** Inclusive `[start, end]` line span of the definition beginning at `start`. */
const spanOf = (rel: string, start: number): readonly [number, number] => {
  const lines = PY_LINES.get(rel) ?? [];
  const head = lines[start - 1] ?? '';

  /** Line on which the bracketed head starting at `from` closes. */
  const closesAt = (from: number): number => {
    const scan = lines.slice(from - 1).reduce<{ depth: number; end: number }>(
      (state, line, offset) => {
        if (state.end > 0) return state;
        const depth = [...line].reduce(
          (level, ch) => level + ('([{'.includes(ch) ? 1 : ')]}'.includes(ch) ? -1 : 0),
          state.depth,
        );
        return depth <= 0 ? { depth, end: from + offset } : { depth, end: 0 };
      },
      { depth: 0, end: 0 },
    );
    return scan.end === 0 ? from : scan.end;
  };

  if (!/^\s*(?:async\s+)?(?:def|class)\s/.test(head)) return [start, closesAt(start)];

  const base = indentOf(head);
  const headEnd = closesAt(start);
  const body = lines.slice(headEnd).findIndex(
    (line) => line.trim() !== '' && indentOf(line) <= base,
  );
  return [start, body === -1 ? lines.length : headEnd + body];
};

/**
 * A backticked identifier, or a backticked `<pyfile>:<lo>[-<hi>]` citation.
 * Scanned in document order so each citation can look back at what precedes it.
 */
const TOKEN_RE = new RegExp(
  '`(?:' +
    `(?<file>${Object.keys(PY_FILES).map((name) => name.replace(/[./]/g, '\\$&')).join('|')})` +
    ':(?<lo>\\d+)(?:-(?<hi>\\d+))?' +
    '|(?<ident>[A-Za-z_][A-Za-z0-9_]*)' +
    ')`',
  'g',
);

interface Problem {
  readonly where: string;
  readonly detail: string;
}

/** How far back an anchor may sit from the citation it explains. */
const ANCHOR_REACH = 200;

const scan = (): { problems: ReadonlyArray<Problem>; anchored: number; total: number } =>
  tsFiles(join(WIRE, 'src')).reduce<{
    problems: Problem[];
    anchored: number;
    total: number;
  }>(
    (acc, file) => {
      const text = readFileSync(file, 'utf8');
      const rel = file.slice(WIRE.length + 1);
      const tokens = [...text.matchAll(TOKEN_RE)];

      tokens.forEach((token, index) => {
        const groups = token.groups ?? {};
        const spelling = groups.file;
        if (spelling === undefined) return;
        acc.total += 1;

        const pyRel = PY_FILES[spelling] ?? spelling;
        const lines = PY_LINES.get(pyRel) ?? [];
        const lo = Number(groups.lo);
        const hi = groups.hi === undefined ? lo : Number(groups.hi);
        const line = text.slice(0, token.index).split('\n').length;
        const where = `${rel}:${String(line)} -> ${pyRel}:${groups.lo ?? ''}`;

        if (lo < 1 || hi < lo || hi > lines.length) {
          acc.problems.push({ where, detail: `range is outside the file (1..${String(lines.length)})` });
          return;
        }

        const previous = tokens[index - 1];
        const anchor = previous?.groups?.ident;
        if (
          previous === undefined ||
          anchor === undefined ||
          token.index - (previous.index + previous[0].length) > ANCHOR_REACH
        ) {
          return; // not anchored — nothing to check
        }

        const defLine = PY_DEFS.get(pyRel)?.get(anchor);
        if (defLine === undefined) return; // anchor is not a symbol of that file

        acc.anchored += 1;
        const region = lines.slice(lo - 1, hi).join('\n');
        if (new RegExp(`\\b${anchor}\\b`).test(region)) return;
        const [spanLo, spanHi] = spanOf(pyRel, defLine);
        if (lo >= spanLo && hi <= spanHi) return;

        acc.problems.push({
          where,
          detail:
            `cites \`${anchor}\`, but ${pyRel}:${String(lo)}` +
            `${hi === lo ? '' : `-${String(hi)}`} neither mentions it nor lies inside ` +
            `its definition (${String(spanLo)}-${String(spanHi)})`,
        });
      });
      return acc;
    },
    { problems: [], anchored: 0, total: 0 },
  );

const { problems, anchored: anchoredCount, total } = scan();

describe('citations / the Python line numbers in src/ still point at what they name', () => {
  test('the Python sources are present — ungated, so a missing authority fails instead of skipping', () => {
    for (const rel of CANONICAL_PY) expect((PY_LINES.get(rel) ?? []).length).toBeGreaterThan(10);
  });

  test('the scan found the citations it is supposed to find', () => {
    // Without this, deleting the token regex would make the suite greener.
    expect(total).toBeGreaterThan(300);
    expect(anchoredCount).toBeGreaterThan(100);
  });

  test('every anchored citation resolves', () => {
    expect(problems.map((problem) => `${problem.where}  ${problem.detail}`)).toEqual([]);
  });

  test('every cited line number is inside the file it names', () => {
    // Subsumed by the check above for anchored citations; this states the
    // weaker property for all ~350, including the unanchored ones.
    const outOfRange = problems.filter((problem) => problem.detail.startsWith('range is'));
    expect(outOfRange).toEqual([]);
  });
});

describe('citations / the guard itself', () => {
  test('it would notice a wrong pointer', () => {
    // The drift that motivated this file: ACTION_KIND_RE is client.py:99, and
    // :105 is the body of the strictly wider ACTION_KIND_SELECTOR_RE.
    const lines = PY_LINES.get('play/client.py') ?? [];
    expect(lines[98] ?? '').toContain('ACTION_KIND_RE = re.compile(');
    expect(lines[104] ?? '').not.toContain('ACTION_KIND_RE');
  });

  test('it accepts a citation that names a statement inside a function', () => {
    // `_validate_error` is defined at :1332 and cited at :1350 elsewhere; the
    // span rule is what keeps that legitimate citation from being a false alarm.
    const [lo, hi] = spanOf('play/client.py', 1332);
    expect(lo).toBe(1332);
    expect(hi).toBeGreaterThan(1350);
  });
});
