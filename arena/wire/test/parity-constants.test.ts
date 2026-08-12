/**
 * Every exported numeric bound, pinned to its literal — and, where the number
 * exists upstream, re-derived from the Python that owns it.
 *
 * ## Why this file exists
 *
 * The bounds had assertions, and the assertions were vacuous.  `test/agent/
 * primitives.test.ts` has a test literally titled *"the depth ceiling is
 * exactly 12 ancestors"* whose body is `accepts(nest(JSON_MAX_DEPTH))` and
 * `!accepts(nest(JSON_MAX_DEPTH + 1))` — both true for **any** value of the
 * symbol.  Mutating 15 of the 26 exported bounds by ±1 left the entire suite
 * green.  A transcription typo in the port was therefore invisible to CI and
 * would surface only as a live parity divergence against CPython.
 *
 * The rule this file establishes: **a bound is asserted against a literal or
 * against the Python, never against its own symbol.**
 *
 * ## Two layers
 *
 * 1. {@link LITERALS} pins every exported numeric constant to a written-out
 *    number.  Exhaustive by construction — the last test in the file fails if
 *    `src/` grows a constant this table does not name, so a new bound cannot be
 *    added without pinning it.
 * 2. {@link DERIVED} re-reads the Python and extracts the same number from the
 *    expression that enforces it.  This is the layer that catches a bound the
 *    port and its pin *both* got wrong, and the layer that goes red when the
 *    upstream moves.
 *
 * Layer 2 is ungated: a missing `play/client.py` fails here rather than
 * skipping, matching `test/canon.test.ts`.
 */
import { describe, expect, test } from 'bun:test';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { CANON_MAX_DEPTH } from 'src/canon';
import { COMMANDS_PER_BATCH } from 'src/agent/batch';
import { DESCRIPTOR_LABEL_MAX } from 'src/agent/descriptor';
import { ERROR_MESSAGE_MAX } from 'src/agent/error';
import { IMPROVEMENTS_MAX, SPECIALISTS_MAX } from 'src/agent/observation';
import { V2_PAGE_MAX_ITEMS } from 'src/agent/page';
import {
  JSON_MAX_DEPTH,
  JSON_MAX_ITEMS,
  JSON_MAX_KEY_LENGTH,
  JSON_MAX_KEYS,
  MAX_TURNS_CEILING,
} from 'src/agent/primitives';
import { ARCHIVE_SCHEMA_VERSION } from 'src/gateway/archive';
import { MAX_TECHNOLOGY_ID } from 'src/gateway/manifest';
import {
  GAME_EVENT_ACTOR_MAX_LENGTH,
  GAME_EVENT_ACTORS_MAX,
  GAME_EVENT_KIND_MAX_LENGTH,
  GAME_EVENT_MAX_WEIGHT,
  GAME_EVENT_MIN_WEIGHT,
  GAME_EVENT_SUMMARY_MAX_LENGTH,
  GAME_EVENT_WARNING_MESSAGE_MAX_LENGTH,
  GAME_EVENT_WARNINGS_MAX,
  REPLAY_DEFAULT_AFTER_TURN,
  REPLAY_DEFAULT_LIMIT,
  REPLAY_MAX_LIMIT,
  REPLAY_MIN_LIMIT,
} from 'src/gateway/replay';

// ---------------------------------------------------------------------------
// Layer 1 — the literal pin
// ---------------------------------------------------------------------------

/**
 * Every exported numeric constant in `src/`, and the number it must be.
 *
 * Write the number out.  Do not compute it, do not reference the symbol on the
 * right-hand side, and do not import a "expected" constant from anywhere —
 * the whole point is that this column is an independent second copy.
 */
const LITERALS: ReadonlyArray<readonly [name: string, actual: number, expected: number]> = [
  // agent — _json_value's denial-of-service bounds (client.py:1279-1293)
  ['JSON_MAX_DEPTH', JSON_MAX_DEPTH, 12],
  ['JSON_MAX_ITEMS', JSON_MAX_ITEMS, 8192],
  ['JSON_MAX_KEYS', JSON_MAX_KEYS, 2048],
  ['JSON_MAX_KEY_LENGTH', JSON_MAX_KEY_LENGTH, 128],
  // agent — payload bounds
  ['MAX_TURNS_CEILING', MAX_TURNS_CEILING, 5000],
  ['ERROR_MESSAGE_MAX', ERROR_MESSAGE_MAX, 500],
  ['DESCRIPTOR_LABEL_MAX', DESCRIPTOR_LABEL_MAX, 240],
  ['IMPROVEMENTS_MAX', IMPROVEMENTS_MAX, 1024],
  ['SPECIALISTS_MAX', SPECIALISTS_MAX, 256],
  ['V2_PAGE_MAX_ITEMS', V2_PAGE_MAX_ITEMS, 16],
  ['COMMANDS_PER_BATCH', COMMANDS_PER_BATCH, 1],
  // gateway — replay pagination (replay_gateway.py:1566, :1572)
  ['REPLAY_DEFAULT_AFTER_TURN', REPLAY_DEFAULT_AFTER_TURN, 0],
  ['REPLAY_DEFAULT_LIMIT', REPLAY_DEFAULT_LIMIT, 250],
  ['REPLAY_MIN_LIMIT', REPLAY_MIN_LIMIT, 1],
  ['REPLAY_MAX_LIMIT', REPLAY_MAX_LIMIT, 250],
  // gateway — game events (replay_gateway.py:371-394, :435-439)
  ['GAME_EVENT_KIND_MAX_LENGTH', GAME_EVENT_KIND_MAX_LENGTH, 40],
  ['GAME_EVENT_SUMMARY_MAX_LENGTH', GAME_EVENT_SUMMARY_MAX_LENGTH, 240],
  ['GAME_EVENT_ACTOR_MAX_LENGTH', GAME_EVENT_ACTOR_MAX_LENGTH, 80],
  ['GAME_EVENT_ACTORS_MAX', GAME_EVENT_ACTORS_MAX, 8],
  ['GAME_EVENT_MIN_WEIGHT', GAME_EVENT_MIN_WEIGHT, 1],
  ['GAME_EVENT_MAX_WEIGHT', GAME_EVENT_MAX_WEIGHT, 100],
  ['GAME_EVENT_WARNING_MESSAGE_MAX_LENGTH', GAME_EVENT_WARNING_MESSAGE_MAX_LENGTH, 200],
  ['GAME_EVENT_WARNINGS_MAX', GAME_EVENT_WARNINGS_MAX, 100],
  // gateway — misc
  ['ARCHIVE_SCHEMA_VERSION', ARCHIVE_SCHEMA_VERSION, 1],
  ['MAX_TECHNOLOGY_ID', MAX_TECHNOLOGY_ID, 511],
  // core
  ['CANON_MAX_DEPTH', CANON_MAX_DEPTH, 1000],
];

describe('numeric bounds / pinned to literals', () => {
  test.each(LITERALS.map(([name, actual, expected]) => [name, actual, expected] as const))(
    '%s is exactly %p',
    (_name, actual, expected) => {
      expect(actual).toBe(expected);
    },
  );
});

// ---------------------------------------------------------------------------
// Exhaustiveness — a new bound cannot be added without pinning it
// ---------------------------------------------------------------------------

const SRC = join(import.meta.dir, '../src');

const tsFiles = (dir: string): ReadonlyArray<string> =>
  readdirSync(dir).flatMap((entry) => {
    const full = join(dir, entry);
    return statSync(full).isDirectory() ? tsFiles(full) : full.endsWith('.ts') ? [full] : [];
  });

/** `export const NAME = <integer>;` — the shape a bound is written in. */
const EXPORTED_INT_RE = /^export const ([A-Z_][A-Z0-9_]*) = (\d+);$/gm;

const declaredBounds: ReadonlyArray<string> = tsFiles(SRC)
  .flatMap((file) => [...readFileSync(file, 'utf8').matchAll(EXPORTED_INT_RE)])
  .map((match) => match[1] ?? '')
  .toSorted();

describe('numeric bounds / the pin table is exhaustive', () => {
  test('the scan found the constants it is supposed to find', () => {
    // Guards the assertion below from passing because the regex matched nothing.
    expect(declaredBounds.length).toBeGreaterThan(20);
    expect(declaredBounds).toContain('JSON_MAX_DEPTH');
  });

  test('every exported integer constant in src/ is pinned above', () => {
    const pinned = new Set(LITERALS.map(([name]) => name));
    expect(declaredBounds.filter((name) => !pinned.has(name))).toEqual([]);
  });

  test('the table pins nothing that no longer exists', () => {
    const declared = new Set(declaredBounds);
    expect(LITERALS.map(([name]) => name).filter((name) => !declared.has(name))).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// Layer 2 — re-derived from the Python that owns the number
// ---------------------------------------------------------------------------

const REPO = join(import.meta.dir, '../../..');

/**
 * Read a Python source the assertions below are written against.
 *
 * Ungated: a missing authority throws at module load rather than letting these
 * tests vanish, which is the standard `test/canon.test.ts` sets for python3.
 */
const readPythonSource = (rel: string): string => readFileSync(join(REPO, rel), 'utf8');

const CLIENT = readPythonSource('play/client.py');
const GATEWAY = readPythonSource('agent_eval/replay_gateway.py');

/** The single integer captured by `pattern`, or `NaN` when it is not unique. */
const pythonNumber = (source: string, pattern: RegExp): number => {
  const flags = pattern.flags.includes('g') ? pattern.flags : `${pattern.flags}g`;
  const matches = [...source.matchAll(new RegExp(pattern.source, flags))];
  const values = new Set(matches.map((match) => Number(match[1])));
  return values.size === 1 ? [...values][0] ?? Number.NaN : Number.NaN;
};

/**
 * Each row: the port's constant, and the Python expression that enforces the
 * same number.  The regex must capture the number and must match uniquely.
 */
const DERIVED: ReadonlyArray<
  readonly [name: string, actual: number, source: string, pattern: RegExp]
> = [
  ['JSON_MAX_DEPTH', JSON_MAX_DEPTH, CLIENT, /if depth > (\d+):/],
  ['JSON_MAX_ITEMS', JSON_MAX_ITEMS, CLIENT, /if len\(value\) > (\d+):\n\s+raise PlayerError/],
  ['JSON_MAX_KEYS', JSON_MAX_KEYS, CLIENT, /if len\(value\) > (\d+) or any\(/],
  ['JSON_MAX_KEY_LENGTH', JSON_MAX_KEY_LENGTH, CLIENT, /len\(key\) > (\d+)/],
  ['MAX_TURNS_CEILING', MAX_TURNS_CEILING, CLIENT, /not 1 <= max_turns <= (\d+)/],
  ['ERROR_MESSAGE_MAX', ERROR_MESSAGE_MAX, CLIENT, /len\(error\["message"\]\) > (\d+)/],
  ['DESCRIPTOR_LABEL_MAX', DESCRIPTOR_LABEL_MAX, CLIENT, /len\(raw\["label"\]\) > (\d+)/],
  ['IMPROVEMENTS_MAX', IMPROVEMENTS_MAX, CLIENT, /len\(improvements\) > (\d+)/],
  ['SPECIALISTS_MAX', SPECIALISTS_MAX, CLIENT, /len\(specialists\) > (\d+)/],
  ['V2_PAGE_MAX_ITEMS', V2_PAGE_MAX_ITEMS, CLIENT, /^V2_TURN_PAGE_LIMIT = (\d+)$/m],
  ['REPLAY_DEFAULT_LIMIT', REPLAY_DEFAULT_LIMIT, GATEWAY, /values\.get\("limit", \["(\d+)"\]\)/],
  ['REPLAY_MAX_LIMIT', REPLAY_MAX_LIMIT, GATEWAY, /not 1 <= limit <= (\d+)/],
  ['GAME_EVENT_KIND_MAX_LENGTH', GAME_EVENT_KIND_MAX_LENGTH, GATEWAY, /_public_text\(kind, "event", (\d+)\)/],
  ['GAME_EVENT_SUMMARY_MAX_LENGTH', GAME_EVENT_SUMMARY_MAX_LENGTH, GATEWAY, /_public_text\(summary, "", (\d+)\)/],
  ['GAME_EVENT_ACTOR_MAX_LENGTH', GAME_EVENT_ACTOR_MAX_LENGTH, GATEWAY, /_public_text\(actor, "", (\d+)\)/],
  ['GAME_EVENT_MAX_WEIGHT', GAME_EVENT_MAX_WEIGHT, GATEWAY, /not 1 <= weight <= (\d+)/],
  ['GAME_EVENT_WARNING_MESSAGE_MAX_LENGTH', GAME_EVENT_WARNING_MESSAGE_MAX_LENGTH, GATEWAY, /"message": _public_text\(row\.get\("message"\), "", (\d+)\)/],
];

describe('numeric bounds / re-derived from the Python', () => {
  test('the Python sources are present — ungated, so a missing authority fails instead of skipping', () => {
    expect(CLIENT.length).toBeGreaterThan(0);
    expect(GATEWAY.length).toBeGreaterThan(0);
  });

  test.each(DERIVED.map((row) => [row[0], row] as const))(
    '%s matches the number the Python enforces',
    (_name, [, actual, source, pattern]) => {
      expect(pythonNumber(source, pattern)).toBe(actual);
    },
  );

  test('GAME_EVENT_ACTORS_MAX is the actors slice width', () => {
    // `][:8],` closes the actor list comprehension at replay_gateway.py:387.
    expect(GATEWAY).toContain(`][:${String(GAME_EVENT_ACTORS_MAX)}],`);
  });

  test('GAME_EVENT_WARNINGS_MAX is the warnings slice width', () => {
    expect(GATEWAY).toContain(`][:${String(GAME_EVENT_WARNINGS_MAX)}],`);
  });

  test('the minimum weight and the minimum limit are both the literal 1', () => {
    expect(GATEWAY).toContain(`not ${String(GAME_EVENT_MIN_WEIGHT)} <= weight <=`);
    expect(GATEWAY).toContain(`not ${String(REPLAY_MIN_LIMIT)} <= limit <=`);
  });
});

// ---------------------------------------------------------------------------
// CANON_MAX_DEPTH — the one rail with no upstream number
// ---------------------------------------------------------------------------

describe('CANON_MAX_DEPTH / the rail is fenced on both sides', () => {
  /**
   * `canon.ts`'s writer is recursive on the JS stack, so the rail has to sit
   * *above* everything CPython can practically encode and *below* where Bun
   * blows the stack.  Only the upper side was fenced (by the literal `20000`
   * in `canon-divergences.test.ts`); a drift down to 100 would have silently
   * started refusing documents CPython encodes, with the whole suite green.
   */
  test('it is at least CPython\'s default recursion reach', () => {
    // sys.setrecursionlimit defaults to 1000, and json.dumps recurses per
    // level, so a document CPython can encode cannot be deeper than this.
    expect(CANON_MAX_DEPTH).toBeGreaterThanOrEqual(1000);
  });

  test('it is below the depth at which Bun gives out', () => {
    // Measured around 8000 under Bun 1.4; keep a wide margin.
    expect(CANON_MAX_DEPTH).toBeLessThanOrEqual(4000);
  });
});
