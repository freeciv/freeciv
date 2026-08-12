/**
 * The corpus guards itself.
 *
 * Decode-parity tests are only as trustworthy as the bytes they read, so this
 * suite proves the committed fixtures still match what `index.json` claims:
 * same size, same digest, nothing on disk that the index does not describe, and
 * nothing in the index that is missing from disk. A fixture edited by hand — or
 * added without a provenance entry — fails here, long before it can quietly
 * change the meaning of a schema test downstream.
 */
import { createHash } from 'node:crypto';
import { readFileSync, readdirSync } from 'node:fs';
import { join, relative } from 'node:path';
import { describe, expect, test } from 'bun:test';
import { Either, Schema } from 'effect';

const FIXTURES_DIR = join(import.meta.dir, 'fixtures');

const FixtureOrigin = Schema.Literal('run-dir', 'live-capture', 'synthetic-negative');

const FixtureEntry = Schema.Struct({
  path: Schema.NonEmptyString,
  origin: FixtureOrigin,
  source: Schema.NonEmptyString,
  bytes: Schema.Int.pipe(Schema.positive()),
  sha256: Schema.String.pipe(Schema.pattern(/^[0-9a-f]{64}$/)),
  note: Schema.NonEmptyString,
});

const FixtureIndex = Schema.Struct({
  schema_version: Schema.Literal(1),
  generated_for: Schema.NonEmptyString,
  fixtures: Schema.Array(FixtureEntry).pipe(Schema.minItems(1)),
});

type FixtureEntry = Schema.Schema.Type<typeof FixtureEntry>;

/** Files that describe the corpus rather than belonging to it. */
const METADATA_FILES: ReadonlySet<string> = new Set(['README.md', 'index.json']);

const walk = (dir: string): ReadonlyArray<string> =>
  readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const full = join(dir, entry.name);
    return entry.isDirectory() ? walk(full) : [relative(FIXTURES_DIR, full)];
  });

const decodedIndex = Schema.decodeUnknownEither(FixtureIndex)(
  JSON.parse(readFileSync(join(FIXTURES_DIR, 'index.json'), 'utf8')),
);

const entries: ReadonlyArray<FixtureEntry> = Either.match(decodedIndex, {
  onLeft: () => [],
  onRight: (index) => index.fixtures,
});

const parsesAsJson = (text: string): boolean =>
  Either.isRight(Either.try(() => JSON.parse(text) as unknown));

describe('@arena/wire fixture corpus', () => {
  test('index.json itself decodes', () => {
    expect(Either.isRight(decodedIndex)).toBe(true);
  });

  // NOTE: this checks the *index*, not decodability. It proves each family has
  // at least one entry with provenance; it says nothing about whether any
  // schema can read those bytes. `test/fixture-coverage.test.ts` is what
  // measures that, and it is where the sidecar family's missing schema is
  // tracked — this assertion used to be read as covering it, and did not.
  test('the index carries a provenance entry for every scenario family', () => {
    const origins = new Set(entries.map((entry) => entry.origin));
    expect(origins).toEqual(new Set(['run-dir', 'live-capture', 'synthetic-negative']));

    const families = ['runs/manifest/', 'runs/report/', 'runs/replay-catalog/', 'runs/sidecar/', 'live/', 'invalid/'];
    const missing = families.filter(
      (family) => !entries.some((entry) => entry.path.startsWith(family)),
    );
    expect(missing).toEqual([]);
  });

  test('every indexed fixture is on disk at its recorded size and digest', () => {
    const mismatches = entries.flatMap((entry) => {
      const bytes = readFileSync(join(FIXTURES_DIR, entry.path));
      const digest = createHash('sha256').update(bytes).digest('hex');
      return bytes.byteLength === entry.bytes && digest === entry.sha256
        ? []
        : [`${entry.path}: ${bytes.byteLength}b/${digest.slice(0, 12)} vs indexed ${entry.bytes}b/${entry.sha256.slice(0, 12)}`];
    });
    expect(mismatches).toEqual([]);
  });

  test('nothing sits in the corpus without a provenance entry', () => {
    const indexed = new Set(entries.map((entry) => entry.path));
    const undocumented = walk(FIXTURES_DIR)
      .filter((path) => !METADATA_FILES.has(path))
      .filter((path) => !indexed.has(path));
    expect(undocumented).toEqual([]);
  });

  test('captured fixtures are real JSON; only the negatives may not be', () => {
    const broken = entries
      .filter((entry) => entry.origin !== 'synthetic-negative')
      .filter((entry) => !parsesAsJson(readFileSync(join(FIXTURES_DIR, entry.path), 'utf8')));
    expect(broken.map((entry) => entry.path)).toEqual([]);
  });

  test('synthetic negatives are quarantined under invalid/', () => {
    const leaked = entries
      .filter((entry) => entry.origin === 'synthetic-negative')
      .filter((entry) => !entry.path.startsWith('invalid/'));
    expect(leaked.map((entry) => entry.path)).toEqual([]);

    const smuggled = entries
      .filter((entry) => entry.path.startsWith('invalid/'))
      .filter((entry) => entry.origin !== 'synthetic-negative');
    expect(smuggled.map((entry) => entry.path)).toEqual([]);
  });

  test('no fixture carries a string-valued credential field', () => {
    const credentialKey = /token|secret|bearer|password|api_?key|credential|authorization/i;
    const offenders = entries.flatMap((entry) => {
      const text = readFileSync(join(FIXTURES_DIR, entry.path), 'utf8');
      const matches = text.match(/"[A-Za-z0-9_]+"\s*:\s*"[^"]*"/g) ?? [];
      return matches
        .filter((pair) => credentialKey.test(pair.slice(0, pair.indexOf(':'))))
        .map((pair) => `${entry.path}: ${pair.slice(0, 60)}`);
    });
    expect(offenders).toEqual([]);
  });

  test('the run-dir fixtures never include a run auth.json', () => {
    const authLike = walk(FIXTURES_DIR).filter((path) => /(^|\/)auth[^/]*$/.test(path));
    expect(authLike).toEqual([]);
  });

  test('every fixture stays small enough to read in a review', () => {
    const oversized = entries.filter((entry) => entry.bytes > 128 * 1024);
    expect(oversized.map((entry) => entry.path)).toEqual([]);
  });
});
