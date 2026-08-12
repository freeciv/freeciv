/**
 * FNV-1a-64 — the digest the observation stream carries.
 *
 * The vectors below were taken from CPython (the same arithmetic
 * `v2_control._known_techs_digest` runs), and the differential tests re-derive
 * them from a live python3 rather than trusting this file.  The last suite
 * rebuilds `_research_choices_digest` out of {@link fnv1a64Update} and checks
 * it against the real function, which is what proves the primitive composes
 * the way the protocol's framed records need it to.
 */
import { describe, expect, test } from 'bun:test';
import { Either, Option } from 'effect';
import {
  FNV1A64_OFFSET_BASIS,
  FNV1A64_PRIME,
  FNV1A64_TEXT_PATTERN,
  fnv1a64,
  fnv1a64Update,
  fnv1a64Utf8,
  formatFnv1a64,
  parseFnv1a64,
  U64_MASK,
} from 'src/index';
import { NATIVE_SCHEMA_CANONICAL } from 'test/native-schema-fixture';

const REPO_ROOT = `${import.meta.dir}/../../..`;

const decoder = new TextDecoder();
const encoder = new TextEncoder();

const pythonText = (source: string, input = ''): string => {
  const run = Bun.spawnSync(['python3', '-c', source], {
    cwd: REPO_ROOT,
    stdin: encoder.encode(input),
  });
  return run.success
    ? decoder.decode(run.stdout)
    : `<python3 failed: ${decoder.decode(run.stderr).trim()}>`;
};

const pythonAvailable = pythonText('print("ok")').trim() === 'ok';

const hexOf = (bytes: Uint8Array): string =>
  Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');

const digestOfText = (text: string): bigint =>
  Either.getOrElse(fnv1a64Utf8(text), () => -1n);

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value);

describe('fnv1a64 / constants', () => {
  test('the basis and prime are the ones the schema id publishes', () => {
    const digest = isRecord(NATIVE_SCHEMA_CANONICAL)
      ? NATIVE_SCHEMA_CANONICAL['research_choices_digest']
      : undefined;
    expect(isRecord(digest) ? digest['algorithm'] : null).toBe('FNV-1a-64');
    expect(isRecord(digest) ? digest['offset_basis'] : null).toBe(FNV1A64_OFFSET_BASIS);
    expect(isRecord(digest) ? digest['prime'] : null).toBe(FNV1A64_PRIME);
    expect(isRecord(digest) ? digest['text_format'] : null).toBe('fnv1a64-%016x');
  });

  test('the constants are their documented hex values', () => {
    expect(FNV1A64_OFFSET_BASIS).toBe(0xcbf29ce484222325n);
    expect(FNV1A64_PRIME).toBe(0x100000001b3n);
    expect(U64_MASK).toBe(2n ** 64n - 1n);
  });

  test('the basis is beyond what a double can hold', () => {
    expect(Number.isSafeInteger(Number(FNV1A64_OFFSET_BASIS))).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// Vectors
// ---------------------------------------------------------------------------

/** `[utf-8 text, fnv1a64-%016x]`, each computed with CPython. */
const TEXT_VECTORS: ReadonlyArray<readonly [string, string]> = [
  ['', 'fnv1a64-cbf29ce484222325'],
  ['a', 'fnv1a64-af63dc4c8601ec8c'],
  ['foobar', 'fnv1a64-85944171f73967e8'],
  ['hello world', 'fnv1a64-779a65e7023cd2e7'],
  // `_known_techs_digest` spells an empty list as "-" and a list as "1,2,3".
  ['-', 'fnv1a64-af63a04c86018698'],
  ['1,2,3', 'fnv1a64-ff3635971fa88157'],
  // Multi-byte input: the digest is over UTF-8 bytes, not code units.
  ['ünïcødé \u{1f600}', 'fnv1a64-fbf9e13c1b0e076d'],
];

describe('fnv1a64 / vectors', () => {
  test.each(TEXT_VECTORS)('fnv1a64Utf8(%p) === %p', (text, expected) => {
    expect(formatFnv1a64(digestOfText(text))).toBe(expected);
  });

  test('the empty digest is the offset basis itself', () => {
    expect(fnv1a64(new Uint8Array())).toBe(FNV1A64_OFFSET_BASIS);
    expect(formatFnv1a64(FNV1A64_OFFSET_BASIS)).toBe('fnv1a64-cbf29ce484222325');
  });

  test('the state stays inside 64 bits over a long input', () => {
    const long = fnv1a64(new Uint8Array(4096).fill(0xff));
    expect(long).toBeLessThanOrEqual(U64_MASK);
    expect(long).toBeGreaterThanOrEqual(0n);
  });

  test('updating in chunks is updating in one go', () => {
    const bytes = encoder.encode('the quick brown fox jumps over the lazy dog');
    const split = [7, 19, 31].reduce(
      (accumulator, cut, index, cuts) =>
        fnv1a64Update(accumulator, bytes.subarray(index === 0 ? 0 : (cuts[index - 1] ?? 0), cut)),
      FNV1A64_OFFSET_BASIS,
    );
    expect(fnv1a64Update(split, bytes.subarray(31))).toBe(fnv1a64(bytes));
  });

  test('a one-bit change moves the digest', () => {
    expect(digestOfText('a')).not.toBe(digestOfText('b'));
    expect(digestOfText('ab')).not.toBe(digestOfText('ba'));
  });
});

// ---------------------------------------------------------------------------
// Text form
// ---------------------------------------------------------------------------

describe('fnv1a64 / text form', () => {
  test('formats as sixteen zero-padded lowercase hex digits', () => {
    expect(formatFnv1a64(0n)).toBe('fnv1a64-0000000000000000');
    expect(formatFnv1a64(1n)).toBe('fnv1a64-0000000000000001');
    expect(formatFnv1a64(U64_MASK)).toBe('fnv1a64-ffffffffffffffff');
    expect(FNV1A64_TEXT_PATTERN.test(formatFnv1a64(0n))).toBe(true);
  });

  test('round-trips through parse', () => {
    const digests = [0n, 1n, FNV1A64_OFFSET_BASIS, U64_MASK, digestOfText('freeciv')];
    expect(digests.map((digest) => Option.getOrNull(parseFnv1a64(formatFnv1a64(digest))))).toEqual(
      digests,
    );
  });

  test('rejects what the native peer would reject', () => {
    const bad = [
      'fnv1a64-CBF29CE484222325',
      'fnv1a64-cbf29ce48422232',
      'fnv1a64-cbf29ce4842223255',
      'cbf29ce484222325',
      'fnv1a64-',
      'fnv1a64-zzzzzzzzzzzzzzzz',
      ' fnv1a64-cbf29ce484222325',
    ];
    expect(bad.filter((text) => FNV1A64_TEXT_PATTERN.test(text))).toEqual([]);
    expect(bad.filter((text) => Option.isSome(parseFnv1a64(text)))).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// UTF-8 refusals
// ---------------------------------------------------------------------------

describe('fnv1a64 / utf-8 refusals are values', () => {
  test('a lone surrogate is refused rather than digested as U+FFFD', () => {
    expect(Either.isLeft(fnv1a64Utf8('\ud800'))).toBe(true);
    expect(Either.isLeft(fnv1a64Utf8('ok\udfffok'))).toBe(true);
    // The digest that the silent route would have produced, for contrast.
    expect(fnv1a64Utf8('\ud800')).not.toEqual(fnv1a64Utf8('�'));
  });

  test('a paired surrogate digests over its four UTF-8 bytes', () => {
    expect(Either.isRight(fnv1a64Utf8('\u{1f600}'))).toBe(true);
    expect(digestOfText('\u{1f600}')).toBe(fnv1a64(encoder.encode('\u{1f600}')));
  });
});

// ---------------------------------------------------------------------------
// Differential against a live CPython
// ---------------------------------------------------------------------------

const MASK64 = 0xffff_ffff_ffff_ffffn;

const splitmix64 = (seed: bigint): bigint => {
  const a = (seed + 0x9e3779b97f4a7c15n) & MASK64;
  const b = ((a ^ (a >> 30n)) * 0xbf58476d1ce4e5b9n) & MASK64;
  const c = ((b ^ (b >> 27n)) * 0x94d049bb133111ebn) & MASK64;
  return c ^ (c >> 31n);
};

/** Deterministic random byte strings of length 0..40. */
const RANDOM_INPUTS: ReadonlyArray<Uint8Array> = Array.from({ length: 200 }, (_, index) => {
  const length = Number(splitmix64(BigInt(index)) % 41n);
  return Uint8Array.from({ length }, (_unused, position) =>
    Number(splitmix64(BigInt(index * 64 + position + 1)) % 256n),
  );
});

describe('fnv1a64 / differential against python3', () => {
  test('python3 is present — ungated, so a missing oracle fails instead of skipping', () => {
    expect(pythonAvailable).toBe(true);
  });

  test('200 random byte strings digest identically', () => {
    const theirs = pythonText(
      [
        'import json,sys',
        'def digest(data):',
        '    d = 14695981039346656037',
        '    for b in data:',
        '        d ^= b',
        '        d = (d * 1099511628211) & 0xFFFFFFFFFFFFFFFF',
        '    return "fnv1a64-%016x" % d',
        'rows = json.loads(sys.stdin.read())',
        'sys.stdout.write("\\n".join(digest(bytes.fromhex(r)) for r in rows))',
      ].join('\n'),
      JSON.stringify(RANDOM_INPUTS.map(hexOf)),
    );
    expect(theirs.split('\n')).toEqual(
      RANDOM_INPUTS.map((bytes) => formatFnv1a64(fnv1a64(bytes))),
    );
  });

  test('_known_techs_digest is reproducible from the primitive', () => {
    const idLists: ReadonlyArray<readonly number[]> = [[], [1], [1, 2, 3], [0, 42, 999]];
    const theirs = pythonText(
      [
        'import json,sys',
        'from agent_eval.v2_control import _known_techs_digest',
        'rows = json.loads(sys.stdin.read())',
        'sys.stdout.write("\\n".join(_known_techs_digest(r) for r in rows))',
      ].join('\n'),
      JSON.stringify(idLists),
    );
    const mine = idLists.map((ids) =>
      formatFnv1a64(digestOfText(ids.length === 0 ? '-' : ids.join(','))),
    );
    expect(theirs.split('\n')).toEqual(mine);
  });
});

// ---------------------------------------------------------------------------
// The framed record digest, built from fnv1a64Update
// ---------------------------------------------------------------------------

interface Tech {
  readonly native_id: number;
  readonly name: string;
  readonly state: string;
  readonly can_target: boolean;
  readonly can_goal: boolean;
}

const u32be = (value: number): Uint8Array =>
  Uint8Array.of((value >>> 24) & 0xff, (value >>> 16) & 0xff, (value >>> 8) & 0xff, value & 0xff);

/**
 * `_research_choices_digest` (v2_control.py:2284-2304), rebuilt on top of the
 * primitive: `native-id-u32-be, name-utf8-length-u32-be, name-utf8,
 * state-ascii-length-u8, state-ascii, can-target-u8, can-goal-u8`, records in
 * ascending native id.
 */
const researchChoicesDigest = (techs: readonly Tech[]): string =>
  formatFnv1a64(
    techs
      .toSorted((left, right) => left.native_id - right.native_id)
      .reduce((digest, tech) => {
        const name = encoder.encode(tech.name);
        const state = encoder.encode(tech.state);
        return [
          u32be(tech.native_id),
          u32be(name.byteLength),
          name,
          Uint8Array.of(state.byteLength),
          state,
          Uint8Array.of(Number(tech.can_target), Number(tech.can_goal)),
        ].reduce(fnv1a64Update, digest);
      }, FNV1A64_OFFSET_BASIS),
  );

const TECHS: readonly Tech[] = [
  { native_id: 7, name: 'Bronze Working', state: 'reachable', can_target: true, can_goal: true },
  { native_id: 2, name: 'Alphabet', state: 'known', can_target: false, can_goal: false },
  { native_id: 88, name: 'Écriture', state: 'unreachable', can_target: false, can_goal: true },
];

describe('fnv1a64 / the framed research digest composes', () => {
  test('matches the real _research_choices_digest', () => {
    const theirs = pythonText(
      [
        'import json,sys',
        'from agent_eval.v2_control import _research_choices_digest',
        'techs = json.loads(sys.stdin.read())',
        'sys.stdout.write(_research_choices_digest(techs))',
      ].join('\n'),
      JSON.stringify(TECHS),
    );
    expect(researchChoicesDigest(TECHS)).toBe(theirs);
  });

  test('record order is by native id, not by call order', () => {
    expect(researchChoicesDigest([...TECHS].toReversed())).toBe(researchChoicesDigest(TECHS));
  });

  test('the empty choice set is the bare basis', () => {
    expect(researchChoicesDigest([])).toBe(formatFnv1a64(FNV1A64_OFFSET_BASIS));
  });
});
