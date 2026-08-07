/**
 * The mirror's map and yields writers (U08).
 *
 * Ports `MapTests` from `play/tests/test_state_mirror.py` plus the write-side
 * half of `YieldOverlayTests` (the overlay renderer itself is U09's).  Every
 * golden below is the byte-exact text CPython wrote for the same input: the
 * fixtures were fed to `state_mirror.update_from_page` and the resulting files
 * captured verbatim, so a single space in a grid row is a failing assertion.
 *
 * Beyond CPython's own cases the suite pins the three shapes the brief calls
 * out because they have no CPython test and are pure byte surface: a partially
 * explored board (unproven squares inside the bounding box stay blank), a board
 * with negative coordinates (the `%5d` gutter and the `x -2..0` window), and a
 * wrap-around board whose grid spans the full width read back from
 * `state/overview.tsv`.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import { renderMap, terrainLegendLine } from 'src/render/mirror/map';
import type { PrivateFs } from 'src/services/private-fs';
import {
  MAP_FILE,
  OVERVIEW_FILE,
  YIELD_FILE,
  mirrorDir,
  parseMap,
  writeMirror,
  type MirrorRevision,
} from 'src/services/mirror';
import { updateMap } from 'src/services/mirror/update-map';
import { updateYields, yieldRows } from 'src/services/mirror/update-yields';
import { scratchWorkspace, type Scratch } from 'test/_fixtures';

const GAME_ID = 'game_12345678901234567890';
const CITY_A = `city_${'c'.repeat(32)}`;
const CITY_B = `city_${'e'.repeat(32)}`;
const TILE_A = `tile_${'1'.repeat(32)}`;
const TILE_B = `tile_${'2'.repeat(32)}`;
const TILE_C = `tile_${'3'.repeat(32)}`;

const rev = (revision: number, turn = 3): MirrorRevision => ({ turn, revision });

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

interface Mirror {
  readonly dir: string;
  readonly run: <A, E>(effect: Effect.Effect<A, E, PrivateFs>) => A;
  readonly attempt: <A, E>(
    effect: Effect.Effect<A, E, PrivateFs>
  ) => Either.Either<A, E>;
  readonly read: (relative: ReadonlyArray<string>) => string;
  readonly exists: (relative: ReadonlyArray<string>) => boolean;
  readonly relative: (absolute: ReadonlyArray<string>) => ReadonlyArray<string>;
}

const freshMirror = (): Mirror => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const sessionPath = path.join(scratch.workspace.stateRoot, GAME_ID, 'codex-test.json');
  const dir = Effect.runSync(mirrorDir(sessionPath));
  const attempt = <A, E>(effect: Effect.Effect<A, E, PrivateFs>): Either.Either<A, E> =>
    Effect.runSync(Effect.either(Effect.provide(effect, scratch.layer)));
  return {
    dir,
    attempt,
    run: <A, E>(effect: Effect.Effect<A, E, PrivateFs>): A => {
      const either = attempt(effect);
      if (Either.isLeft(either)) {
        const failure: unknown = either.left;
        throw new Error(
          `unexpected failure: ${
            failure !== null && typeof failure === 'object' && 'message' in failure
              ? String(failure.message)
              : String(failure)
          }`
        );
      }
      return either.right;
    },
    read: (relative) => fs.readFileSync(path.join(dir, ...relative), 'utf8'),
    exists: (relative) => fs.existsSync(path.join(dir, ...relative)),
    relative: (absolute) =>
      absolute.map((item) => path.relative(fs.realpathSync(dir), fs.realpathSync(item))),
  };
};

// ---------------------------------------------------------------------------
// Fixtures — the CPython test's own payloads
// ---------------------------------------------------------------------------

const tiles = (): ReadonlyArray<Record<string, unknown>> => [
  { id: 'tile_1', x: 30, y: 71, visibility: 'visible', terrain: 'Ocean' },
  { id: 'tile_2', x: 31, y: 71, visibility: 'remembered', terrain: 'Desert' },
  { id: 'tile_3', x: 32, y: 71, visibility: 'unknown' },
];

const citizens = (city: string = CITY_A): ReadonlyArray<Record<string, unknown>> => [
  {
    city_id: city,
    kind: 'tile',
    tile_id: TILE_A,
    worked: true,
    free_worked: true,
    can_work: true,
    yields: { food: 2, shields: 1, trade: 0, gold: 0, luxury: 0, science: 0 },
  },
  {
    city_id: city,
    kind: 'tile',
    tile_id: TILE_B,
    worked: false,
    free_worked: false,
    can_work: true,
    yields: { food: 1, shields: 0, trade: 3, gold: 0, luxury: 0, science: 0 },
  },
  {
    city_id: city,
    kind: 'specialist',
    id: 'specialist_1',
    name: 'Elvis',
    count: 0,
    counts_toward_population: true,
    can_use: true,
    is_default: true,
    yields: { food: 0, shields: 0, trade: 0, gold: 0, luxury: 2, science: 0 },
  },
];

const TILE_ALIASES = {
  [TILE_A]: 'T(30,71)',
  [TILE_B]: 'T(31,71)',
  [CITY_A]: 'c1',
};

/** The `state/overview.tsv` CPython wrote for an 80x50 wrapping board. */
const OVERVIEW_80x50 =
  '# rev 9 turn 3\n' +
  '# overview 1/1 complete\n' +
  'fact        \tvalue\n' +
  'client_state\trunning\n' +
  'turn        \t3\n' +
  'map         \t80x50\n' +
  'topology    \twrapx|iso\n' +
  'player      \t(none yet)\n' +
  'research    \t(none yet)\n';

// ---------------------------------------------------------------------------
// map.txt
// ---------------------------------------------------------------------------

describe('state/map.txt', () => {
  test('fog renders as a question mark and the legend names what it shows', () => {
    const mirror = freshMirror();
    const written = mirror.run(updateMap(mirror.dir, 'state', rev(9), tiles()));
    expect(mirror.read(MAP_FILE)).toBe(
      '# rev 9 turn 3\n' +
        '# map size unknown · 2 of 3 tiles known\n' +
        '# window x 30..32 y 71..71\n' +
        "# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n" +
        '# terrain D=Desert · O=Ocean\n' +
        '   71 |Od?\n'
    );
    expect(mirror.relative(written)).toEqual(['state/map.txt', 'state/delta.md']);
  });

  test('an unknown tile never reveals a volunteered terrain', () => {
    const mirror = freshMirror();
    const items = tiles().map((item, index) =>
      index === 2 ? { ...item, terrain: 'Grassland' } : item
    );
    mirror.run(updateMap(mirror.dir, 'state', rev(9), items));
    const text = mirror.read(MAP_FILE);
    expect(text.split('\n').filter((line) => line.startsWith('   71'))[0]?.split('|')[1]).toBe(
      'Od?'
    );
    expect(text).not.toContain('Grassland');
  });

  test('later tiles merge into the grid and report progress', () => {
    const mirror = freshMirror();
    mirror.run(updateMap(mirror.dir, 'state', rev(9), tiles()));
    mirror.run(
      updateMap(mirror.dir, 'state', rev(10), [
        { id: 'tile_3', x: 32, y: 71, visibility: 'visible', terrain: 'Forest' },
      ])
    );
    expect(mirror.read(MAP_FILE)).toBe(
      '# rev 10 turn 3\n' +
        '# map size unknown · 3 of 3 tiles known\n' +
        '# window x 30..32 y 71..71\n' +
        "# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n" +
        '# terrain D=Desert · F=Forest · O=Ocean\n' +
        '   71 |OdF\n'
    );
    expect(mirror.read(['state', 'delta.md'])).toContain('terrain known: 2 -> 3 tiles');
  });

  test('terrain codes never change meaning inside one grid', () => {
    const mirror = freshMirror();
    mirror.run(
      updateMap(mirror.dir, 'state', rev(9), [
        { id: 'tile_1', x: 30, y: 71, visibility: 'visible', terrain: 'Wasteland' },
      ])
    );
    expect(mirror.read(MAP_FILE)).toContain('W=Wasteland');
    mirror.run(
      updateMap(mirror.dir, 'state', rev(10), [
        { id: 'tile_2', x: 31, y: 71, visibility: 'visible', terrain: 'Wetland' },
      ])
    );
    // The legend keeps `W` for the terrain that claimed it; the newcomer takes
    // the first free fallback letter, and the row still opens with `W`.
    expect(mirror.read(MAP_FILE)).toBe(
      '# rev 10 turn 3\n' +
        '# map size unknown · 2 of 2 tiles known\n' +
        '# window x 30..31 y 71..71\n' +
        "# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n" +
        '# terrain W=Wasteland · E=Wetland\n' +
        '   71 |WE\n'
    );
  });

  test('an empty tiles page says so instead of drawing a grid', () => {
    const mirror = freshMirror();
    const written = mirror.run(updateMap(mirror.dir, 'state', rev(9), []));
    expect(mirror.read(MAP_FILE)).toBe('# rev 9 turn 3\n# no tiles known yet\n');
    // Nothing changed, so the digest is left alone entirely.
    expect(mirror.relative(written)).toEqual(['state/map.txt']);
    expect(mirror.exists(['state', 'delta.md'])).toBe(false);
  });

  test('a page older than the mirror is ignored', () => {
    const mirror = freshMirror();
    mirror.run(updateMap(mirror.dir, 'state', rev(9), tiles()));
    const before = mirror.read(MAP_FILE);
    const written = mirror.run(
      updateMap(mirror.dir, 'state', rev(8), [
        { id: 'tile_9', x: 40, y: 71, visibility: 'visible', terrain: 'Forest' },
      ])
    );
    expect(written).toEqual([]);
    expect(mirror.read(MAP_FILE)).toBe(before);
  });

  test('a partially explored board leaves unproven squares blank, not fogged', () => {
    const mirror = freshMirror();
    mirror.run(
      updateMap(mirror.dir, 'state', rev(4, 2), [
        { id: 't1', x: 10, y: 4, visibility: 'visible', terrain: 'Grassland' },
        { id: 't2', x: 13, y: 4, visibility: 'remembered', terrain: 'Hills' },
        { id: 't3', x: 11, y: 6, visibility: 'unknown', terrain: 'Mountains' },
        { id: 't4', x: 13, y: 6, visibility: 'visible', terrain: 'Deep Ocean' },
      ])
    );
    // Row 5 was never covered at all, so it is four spaces — and the line is
    // not right-stripped, which is what keeps column x=13 under column x=13.
    expect(mirror.read(MAP_FILE)).toBe(
      '# rev 4 turn 2\n' +
        '# map size unknown · 3 of 4 tiles known\n' +
        '# window x 10..13 y 4..6\n' +
        "# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n" +
        '# terrain B=Deep Ocean · G=Grassland · H=Hills\n' +
        '    4 |G  h\n' +
        '    5 |    \n' +
        '    6 | ? B\n'
    );
  });

  test('negative coordinates keep their sign in the gutter and the window', () => {
    const mirror = freshMirror();
    mirror.run(
      updateMap(mirror.dir, 'state', rev(12, 7), [
        { id: 't1', x: -2, y: -1, visibility: 'visible', terrain: 'Tundra' },
        { id: 't2', x: -1, y: -1, visibility: 'remembered', terrain: 'Jungle' },
        { id: 't3', x: 0, y: 0, visibility: 'visible', terrain: 'Lake' },
        { id: 't4', x: -2, y: 0, visibility: 'unknown' },
      ])
    );
    expect(mirror.read(MAP_FILE)).toBe(
      '# rev 12 turn 7\n' +
        '# map size unknown · 3 of 4 tiles known\n' +
        '# window x -2..0 y -1..0\n' +
        "# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n" +
        '# terrain J=Jungle · L=Lake · T=Tundra\n' +
        '   -1 |Tj \n' +
        '    0 |? L\n'
    );
  });

  test('a wrap-around board reads its size back out of the overview', () => {
    const mirror = freshMirror();
    mirror.run(writeMirror(mirror.dir, OVERVIEW_FILE, OVERVIEW_80x50));
    mirror.run(
      updateMap(mirror.dir, 'state', rev(9), [
        { id: 't1', x: 79, y: 25, visibility: 'visible', terrain: 'Plains' },
        { id: 't2', x: 0, y: 25, visibility: 'visible', terrain: 'Swamp' },
      ])
    );
    // Two tiles either side of the seam bound an 80-wide window: the grid is a
    // bounding box in raw coordinates, it does not know the board wraps.
    expect(mirror.read(MAP_FILE)).toBe(
      '# rev 9 turn 3\n' +
        '# map 80x50 · 2 of 2 tiles known\n' +
        '# window x 0..79 y 25..25\n' +
        "# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n" +
        '# terrain P=Plains · S=Swamp\n' +
        `   25 |S${' '.repeat(78)}P\n`
    );
  });

  test('a coordinate wider than the gutter pushes the grid right', () => {
    const mirror = freshMirror();
    mirror.run(
      updateMap(mirror.dir, 'state', rev(9), [
        { id: 'a', x: 100000, y: 123456, visibility: 'visible', terrain: 'Ocean' },
        { id: 'b', x: 100001, y: 123457, visibility: 'visible', terrain: 'Ocean' },
      ])
    );
    // CPython's `{y:>5}` pads, it never truncates.
    expect(mirror.read(MAP_FILE)).toBe(
      '# rev 9 turn 3\n' +
        '# map size unknown · 2 of 2 tiles known\n' +
        '# window x 100000..100001 y 123456..123457\n' +
        "# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n" +
        '# terrain O=Ocean\n' +
        '123456 |O \n' +
        '123457 | O\n'
    );
  });

  test('an overview that carries no map row still says size unknown', () => {
    const mirror = freshMirror();
    mirror.run(
      writeMirror(
        mirror.dir,
        OVERVIEW_FILE,
        '# rev 9 turn 3\n# overview 1/1 complete\nfact        \tvalue\nclient_state\trunning\n'
      )
    );
    mirror.run(
      updateMap(mirror.dir, 'state', rev(9), [
        { id: 'a', x: 1, y: 1, visibility: 'visible', terrain: 'Ocean' },
      ])
    );
    expect(mirror.read(MAP_FILE)).toContain('# map size unknown · 1 of 1 tiles known');
  });

  test('a ruleset-invented terrain draws from the fallback alphabet in name order', () => {
    const mirror = freshMirror();
    mirror.run(
      updateMap(mirror.dir, 'state', rev(9), [
        { id: 'a', x: 0, y: 0, visibility: 'visible', terrain: 'deep water' },
        { id: 'b', x: 1, y: 0, visibility: 'visible', terrain: 'Deep Ocean' },
        { id: 'c', x: 2, y: 0, visibility: 'remembered', terrain: 'desert' },
        { id: 'd', x: 3, y: 0, visibility: 'visible', terrain: '123' },
        { id: 'e', x: 4, y: 0, visibility: 'visible', terrain: 'Ocean' },
      ])
    );
    // Names sort by code point — digits, then uppercase, then lowercase — and
    // `desert` cannot take `D` because `deep water` claimed it first.
    expect(mirror.read(MAP_FILE)).toBe(
      '# rev 9 turn 3\n' +
        '# map size unknown · 5 of 5 tiles known\n' +
        '# window x 0..4 y 0..0\n' +
        "# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n" +
        '# terrain E=123 · B=Deep Ocean · O=Ocean · D=deep water · N=desert\n' +
        '    0 |DBnEO\n'
    );
  });

  test('the written grid parses back to the grid that was written', () => {
    const mirror = freshMirror();
    mirror.run(updateMap(mirror.dir, 'state', rev(12, 7), [
      { id: 't1', x: -2, y: -1, visibility: 'visible', terrain: 'Tundra' },
      { id: 't2', x: 0, y: 0, visibility: 'visible', terrain: 'Lake' },
    ]));
    const parsed = parseMap(mirror.read(MAP_FILE));
    expect(parsed.revision).toEqual({ turn: 7, revision: 12 });
    expect([...parsed.grid.entries()].sort()).toEqual([
      ['-2,-1', 'T'],
      ['0,0', 'L'],
    ]);
    expect([...parsed.legend.entries()].sort()).toEqual([
      ['Lake', 'L'],
      ['Tundra', 'T'],
    ]);
  });

  test('a tile item without coordinates is a refusal, not a blank square', () => {
    const mirror = freshMirror();
    const either = mirror.attempt(
      updateMap(mirror.dir, 'state', rev(9), [
        { id: 'tile_1', visibility: 'visible', terrain: 'Ocean' },
      ])
    );
    expect(Either.isLeft(either) ? either.left.message : '').toBe(
      'state mirror: tile item carries no coordinates'
    );
    expect(mirror.exists(MAP_FILE)).toBe(false);
  });
});

describe('renderMap', () => {
  test('the legend is ordered by terrain name and filtered to what shows', () => {
    const legend = new Map([
      ['Ocean', 'O'],
      ['Desert', 'D'],
      ['Arctic', 'A'],
    ]);
    // `d` is Desert remembered through fog, so the filter compares uppercase.
    expect(terrainLegendLine(legend, new Map([['0,0', 'd'], ['1,0', 'O']]))).toBe(
      'D=Desert · O=Ocean'
    );
    expect(terrainLegendLine(legend, new Map([['0,0', '?']]))).toBe('');
  });

  test('an empty grid renders the note and nothing else', () => {
    const mirror = freshMirror();
    const text = mirror.run(
      renderMap(mirror.dir, rev(9), { revision: null, legend: new Map(), grid: new Map() }, [])
    );
    expect(text).toBe('# rev 9 turn 3\n# no tiles known yet\n');
  });
});

// ---------------------------------------------------------------------------
// yields.tsv
// ---------------------------------------------------------------------------

describe('state/yields.tsv', () => {
  test('citizen pages price tiles and specialists are skipped', () => {
    const mirror = freshMirror();
    const written = mirror.run(
      updateYields(mirror.dir, 'state', rev(9), citizens(), TILE_ALIASES)
    );
    expect(mirror.relative(written)).toEqual(['state/yields.tsv', 'state/delta.md']);
    const text = mirror.read(YIELD_FILE);
    expect(text).toBe(
      '# rev 9 turn 3\n' +
        '# yields 2/2 complete\n' +
        'tile    \tfood\tshields\ttrade\tworked\tcity\n' +
        'T(30,71)\t2   \t1      \t0    \tyes   \tc1\n' +
        'T(31,71)\t1   \t0      \t3    \tno    \tc1\n'
    );
    expect(text).not.toContain('Elvis');
    expect(mirror.read(['state', 'delta.md'])).toContain('tile yields known for 2 tiles');
  });

  test('a tile with no coordinate alias keeps its opaque handle', () => {
    const mirror = freshMirror();
    mirror.run(updateYields(mirror.dir, 'state', rev(9), citizens(), { [CITY_A]: 'c1' }));
    expect(mirror.read(YIELD_FILE)).toBe(
      '# rev 9 turn 3\n' +
        '# yields 2/2 complete\n' +
        'tile                                 \tfood\tshields\ttrade\tworked\tcity\n' +
        `${TILE_A}\t2   \t1      \t0    \tyes   \tc1\n` +
        `${TILE_B}\t1   \t0      \t3    \tno    \tc1\n`
    );
  });

  test("a second city's page adds to the overlay rather than replacing it", () => {
    const mirror = freshMirror();
    mirror.run(updateYields(mirror.dir, 'state', rev(9), citizens(), TILE_ALIASES));
    const second = citizens(CITY_B).map((item, index) =>
      index === 0
        ? { ...item, tile_id: TILE_C }
        : index === 1
          ? { ...item, tile_id: TILE_B, worked: true }
          : item
    );
    mirror.run(
      updateYields(mirror.dir, 'state', rev(9), second, {
        [TILE_C]: 'T(40,10)',
        [TILE_B]: 'T(31,71)',
        [CITY_B]: 'c2',
      })
    );
    // T(30,71) survives from page one, T(31,71) is re-priced in place by page
    // two, and T(40,10) is appended — insertion order, not sorted order.
    expect(mirror.read(YIELD_FILE)).toBe(
      '# rev 9 turn 3\n' +
        '# yields 3/3 complete\n' +
        'tile    \tfood\tshields\ttrade\tworked\tcity\n' +
        'T(30,71)\t2   \t1      \t0    \tyes   \tc1\n' +
        'T(31,71)\t1   \t0      \t3    \tyes   \tc2\n' +
        'T(40,10)\t2   \t1      \t0    \tyes   \tc2\n'
    );
  });

  test('a page that prices no tile writes nothing at all', () => {
    const mirror = freshMirror();
    const written = mirror.run(
      updateYields(mirror.dir, 'state', rev(9), [
        {
          city_id: CITY_A,
          kind: 'specialist',
          id: 'specialist_1',
          name: 'Elvis',
          count: 0,
          yields: { food: 0, shields: 0, trade: 0 },
        },
      ])
    );
    expect(written).toEqual([]);
    expect(mirror.exists(YIELD_FILE)).toBe(false);
    expect(mirror.exists(['state', 'delta.md'])).toBe(false);
  });

  test('a missing yield is a dash and only `worked: true` is `yes`', () => {
    const mirror = freshMirror();
    mirror.run(
      updateYields(
        mirror.dir,
        'state',
        rev(5, 2),
        [
          { city_id: CITY_A, kind: 'tile', tile_id: TILE_A, worked: true, yields: { food: 2 } },
          {
            city_id: null,
            kind: 'tile',
            tile_id: TILE_B,
            // A truthy string is not `True`: CPython compares by identity.
            worked: 'yes',
            yields: { food: 0, shields: 0, trade: 0 },
          },
          // Neither of these prices a tile: a non-string id and a non-object
          // `yields` are skipped in silence, not refused.
          { city_id: CITY_A, kind: 'tile', tile_id: 17, yields: { food: 1 } },
          { city_id: CITY_A, kind: 'tile', tile_id: 'tile_x', yields: 'nope' },
        ],
        { [TILE_A]: 'T(30,71)' }
      )
    );
    expect(mirror.read(YIELD_FILE)).toBe(
      '# rev 5 turn 2\n' +
        '# yields 2/2 complete\n' +
        'tile                                 \tfood\tshields\ttrade\tworked\tcity\n' +
        `T(30,71)                             \t2   \t-      \t-    \tyes   \t${CITY_A}\n` +
        `${TILE_B}\t0   \t0      \t0    \tno    \t-\n`
    );
  });

  test('a page older than the overlay is ignored', () => {
    const mirror = freshMirror();
    mirror.run(updateYields(mirror.dir, 'state', rev(9), citizens(), TILE_ALIASES));
    const before = mirror.read(YIELD_FILE);
    const stale = citizens().map((item, index) =>
      index === 0 ? { ...item, yields: { food: 99, shields: 1, trade: 0 } } : item
    );
    expect(mirror.run(updateYields(mirror.dir, 'state', rev(8), stale, TILE_ALIASES))).toEqual([]);
    expect(mirror.read(YIELD_FILE)).toBe(before);
  });

  test('a citizen item that is not an object is a refusal', () => {
    const mirror = freshMirror();
    const either = mirror.attempt(updateYields(mirror.dir, 'state', rev(9), ['oops']));
    expect(Either.isLeft(either) ? either.left.message : '').toBe(
      'state mirror: city citizen item is not an object'
    );
  });

  test('yieldRows is the projection decision on its own', () => {
    expect(Effect.runSync(yieldRows(citizens(), TILE_ALIASES))).toEqual([
      ['T(30,71)', '2', '1', '0', 'yes', 'c1'],
      ['T(31,71)', '1', '0', '3', 'no', 'c1'],
    ]);
  });

  test('an id naming an Object.prototype member is not an alias', () => {
    // `tile_id`/`city_id` are unvalidated wire strings: CPython only asks
    // `isinstance(str)`, then looks the id up in a dict, which sees own keys
    // only.  A plain JS property read would walk the prototype chain and spell
    // `toString` as `function toString() { [native code] }` — different bytes,
    // a different column width, and a merge key `show map --yields` can no
    // longer match.
    const hostile = ['toString', 'constructor', '__proto__', 'valueOf', 'hasOwnProperty'];
    const items = hostile.map((id) => ({
      city_id: id,
      kind: 'tile',
      tile_id: id,
      worked: true,
      yields: { food: 1, shields: 1, trade: 1 },
    }));
    expect(Effect.runSync(yieldRows(items, TILE_ALIASES))).toEqual(
      hostile.map((id) => [id, '1', '1', '1', 'yes', id])
    );
    // No aliases at all is the same answer, and an *own* key still resolves.
    expect(Effect.runSync(yieldRows(items, null))).toEqual(
      hostile.map((id) => [id, '1', '1', '1', 'yes', id])
    );
    expect(Effect.runSync(yieldRows(items, { toString: 'T(1,2)' }))).toEqual(
      hostile.map((id) =>
        id === 'toString'
          ? ['T(1,2)', '1', '1', '1', 'yes', 'T(1,2)']
          : [id, '1', '1', '1', 'yes', id]
      )
    );
  });

  test('a prototype-named tile survives the round trip into state/yields.tsv', () => {
    const mirror = freshMirror();
    mirror.run(
      updateYields(
        mirror.dir,
        'state',
        rev(9),
        [
          {
            city_id: 'constructor',
            kind: 'tile',
            tile_id: 'toString',
            worked: true,
            yields: { food: 2, shields: 1, trade: 0 },
          },
        ],
        TILE_ALIASES
      )
    );
    expect(mirror.read(YIELD_FILE)).toBe(
      '# rev 9 turn 3\n' +
        '# yields 1/1 complete\n' +
        'tile    \tfood\tshields\ttrade\tworked\tcity\n' +
        'toString\t2   \t1      \t0    \tyes   \tconstructor\n'
    );
  });
});

// ---------------------------------------------------------------------------
// Both writers against one mirror
// ---------------------------------------------------------------------------

describe('map and yields together', () => {
  test('each writer files its own digest section at one revision', () => {
    const mirror = freshMirror();
    mirror.run(updateMap(mirror.dir, 'state', rev(9), tiles()));
    mirror.run(updateYields(mirror.dir, 'state', rev(9), citizens(), TILE_ALIASES));
    expect(mirror.read(['state', 'delta.md'])).toBe(
      '# rev 9 turn 3\n' +
        'no earlier mirror · last update: state\n' +
        '\n' +
        '## map\n' +
        '- terrain known: 0 -> 2 tiles\n' +
        '\n' +
        '## yields\n' +
        '- tile yields known for 2 tiles\n'
    );
  });
});
