/**
 * `state/delta.md`.
 *
 * Ports `DeltaTests` from `play/tests/test_state_mirror.py:941-1071` at the
 * primitive level: the section dispatch that feeds `rowChanges` is U07's, so
 * what is asserted here is the digest machinery those renderers call —
 * `parseDelta`, `deltaText`, `updateDelta` and `rowChanges`.  Every golden
 * string came from running the CPython original on the same input.
 */
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import type { PrivateFs } from 'src/services/private-fs';
import {
  DELTA_FILE,
  deltaText,
  mirrorDir,
  parseDelta,
  parseTable,
  readMirror,
  rowChanges,
  updateDelta,
  type MirrorTable,
} from 'src/services/mirror';
import { scratchWorkspace, type Scratch } from 'test/_fixtures';

const GAME_ID = 'game_12345678901234567890';
const REV9 = { turn: 3, revision: 9 } as const;
const REV10 = { turn: 3, revision: 10 } as const;
const REV7 = { turn: 3, revision: 7 } as const;

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

interface Mirror {
  readonly dir: string;
  readonly run: <A, E>(effect: Effect.Effect<A, E, PrivateFs>) => Either.Either<A, E>;
  readonly delta: () => string;
}

const freshMirror = (): Mirror => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const dir = Effect.runSync(
    mirrorDir(path.join(scratch.workspace.stateRoot, GAME_ID, 'codex-test.json'))
  );
  const run = <A, E>(effect: Effect.Effect<A, E, PrivateFs>): Either.Either<A, E> =>
    Effect.runSync(Effect.either(Effect.provide(effect, scratch.layer)));
  return {
    dir,
    run,
    delta: () => {
      const either = run(readMirror(dir, DELTA_FILE));
      return Either.isRight(either) ? (either.right ?? '') : '';
    },
  };
};

const ok = <A, E>(either: Either.Either<A, E>): A => {
  expect(Either.isLeft(either) ? either.left : 'ok').toBe('ok');
  if (Either.isLeft(either)) throw new Error('unreachable');
  return either.right;
};

const table = (
  columns: ReadonlyArray<string>,
  rows: ReadonlyArray<ReadonlyArray<string>>
): MirrorTable => ({ revision: null, columns, rows });

// ---------------------------------------------------------------------------

describe('deltaText', () => {
  test('the first digest says there is no earlier mirror', () => {
    expect(deltaText(REV9, null, 'state', new Map([['units', ['a', 'b']]]))).toBe(
      ['# rev 9 turn 3', 'no earlier mirror · last update: state', '', '## units', '- a', '- b', ''].join(
        '\n'
      )
    );
  });

  test('a later digest names the revision it compares against and caps entries', () => {
    const entries = Array.from({ length: 20 }, (_, index) => `e${index}`);
    expect(
      deltaText(REV10, REV9, 'state', new Map([['units', entries], ['cities', []]]))
    ).toBe(
      [
        '# rev 10 turn 3',
        'since rev 9 turn 3 · last update: state',
        '',
        '## units',
        ...entries.slice(0, 12).map((entry) => `- ${entry}`),
        '- … and 8 more',
        '',
      ].join('\n')
    );
  });

  test('an all-empty digest says nothing changed', () => {
    expect(deltaText(REV10, REV9, 'state', new Map([['units', []]]))).toBe(
      ['# rev 10 turn 3', 'since rev 9 turn 3 · last update: state', '', 'nothing changed.', ''].join(
        '\n'
      )
    );
  });

  test('a since equal to the revision is no earlier mirror', () => {
    expect(deltaText(REV9, REV9, 'state', new Map()).split('\n')[1]).toBe(
      'no earlier mirror · last update: state'
    );
  });
});

describe('parseDelta', () => {
  test('the round trip recovers the stamp, the since line and every section', () => {
    const entries = Array.from({ length: 20 }, (_, index) => `e${index}`);
    const parsed = parseDelta(
      deltaText(REV10, REV9, 'state', new Map([['units', entries], ['cities', []]]))
    );
    expect(parsed.revision).toEqual(REV10);
    expect(parsed.since).toEqual(REV9);
    expect([...parsed.sections]).toEqual([
      ['units', [...entries.slice(0, 12), '… and 8 more']],
    ]);
  });

  test('an absent digest parses empty', () => {
    expect(parseDelta(null)).toEqual({ revision: null, since: null, sections: new Map() });
  });

  test('a bullet before any section header is dropped', () => {
    expect([...parseDelta('# rev 9 turn 3\n- orphan\n').sections]).toEqual([]);
  });

  /**
   * Regression: the section title is `line[3:].strip()` in CPython, not
   * `.trim()`.  A title that round-trips to a different key opens a *second*
   * section for the same renderer, so `updateDelta`'s de-duplication stops
   * working and every refresh re-appends the same bullets.
   */
  test('the section title is stripped with CPython’s whitespace class', () => {
    expect([...parseDelta('# rev 9 turn 3\n## ﻿units\x1f\n- moved\n').sections]).toEqual([
      ['﻿units', ['moved']],
    ]);
  });
});

describe('updateDelta', () => {
  test('sections accumulate inside one revision', () => {
    const { dir, run, delta } = freshMirror();
    ok(run(updateDelta(dir, REV9, 'state', 'units', ['u1 new: Settlers own'])));
    ok(run(updateDelta(dir, REV9, 'state', 'cities', ['c1 new: London'])));
    const text = delta();
    expect(text).toContain('## units');
    expect(text).toContain('## cities');
    expect(text.startsWith('# rev 9 turn 3')).toBe(true);
    expect(text).toContain('no earlier mirror');
  });

  test('a duplicate entry inside one revision is not repeated', () => {
    const { dir, run, delta } = freshMirror();
    ok(run(updateDelta(dir, REV9, 'state', 'units', ['u1 moved'])));
    ok(run(updateDelta(dir, REV9, 'state', 'units', ['u1 moved', 'u2 moved'])));
    expect(delta().split('\n').filter((line) => line === '- u1 moved').length).toBe(1);
  });

  test('a newer revision resets the sections and records the prior stamp', () => {
    const { dir, run, delta } = freshMirror();
    ok(run(updateDelta(dir, REV7, 'state', 'units', ['first'])));
    ok(run(updateDelta(dir, REV9, 'state', 'units', ['second'])));
    const text = delta();
    expect(text.startsWith('# rev 9 turn 3')).toBe(true);
    expect(text).toContain('since rev 7 turn 3');
    expect(text).toContain('- second');
    expect(text).not.toContain('- first');
  });

  test('the stamp advances even when nothing changed', () => {
    const { dir, run, delta } = freshMirror();
    ok(run(updateDelta(dir, REV9, 'state', 'map', ['terrain known: 0 -> 1 tiles'])));
    expect(delta().startsWith('# rev 9 turn 3')).toBe(true);
    const written = ok(run(updateDelta(dir, REV10, 'state', 'map', [])));
    expect(written === null ? '' : path.relative(dir, written)).toBe(
      path.join('state', 'delta.md')
    );
    const text = delta();
    expect(text.startsWith('# rev 10 turn 3')).toBe(true);
    expect(text).toContain('nothing changed.');
    expect(text).toContain('since rev 9 turn 3');
  });

  test('nothing is written at an unchanged revision with no changes', () => {
    const { dir, run, delta } = freshMirror();
    ok(run(updateDelta(dir, REV9, 'state', 'map', ['terrain known: 0 -> 1 tiles'])));
    const before = delta();
    expect(ok(run(updateDelta(dir, REV9, 'state', 'map', [])))).toBe(null);
    expect(delta()).toBe(before);
  });

  test('an empty first digest is never written at all', () => {
    const { dir, run, delta } = freshMirror();
    expect(ok(run(updateDelta(dir, REV9, 'state', 'map', [])))).toBe(null);
    expect(delta()).toBe('');
  });

  test('the digest never walks backwards', () => {
    const { dir, run, delta } = freshMirror();
    ok(run(updateDelta(dir, REV9, 'batch', 'receipts', ['applied batch b at rev 9'])));
    expect(ok(run(updateDelta(dir, REV7, 'batch', 'receipts', ['applied batch b at rev 7'])))).toBe(
      null
    );
    expect(delta().startsWith('# rev 9 turn 3')).toBe(true);
  });

  test('the digest caps its entry count the way CPython does', () => {
    const { dir, run, delta } = freshMirror();
    const entries = Array.from({ length: 20 }, (_, index) => `Type${index} (first observation)`);
    ok(run(updateDelta(dir, REV9, 'state', 'units', entries)));
    const bullets = delta().split('\n').filter((line) => line.startsWith('- '));
    expect(bullets.length).toBe(13);
    expect(bullets[12]).toBe('- … and 8 more');
  });

  test('the digest stamp is readable by the table parser show leads with', () => {
    const { dir, run, delta } = freshMirror();
    ok(run(updateDelta(dir, REV9, 'state', 'units', ['u1 new: Settlers own'])));
    expect(parseTable(delta()).revision).toEqual(REV9);
  });
});

describe('rowChanges', () => {
  test('a first observation names every row', () => {
    expect(
      rowChanges(['alias', 'unit', 'who'], table([], []), [
        ['u1', 'Settlers', 'own'],
        ['u2', '-', '-'],
      ])
    ).toEqual(['u1 Settlers own (first observation)', 'u2 - - (first observation)']);
  });

  test('added, changed and gone rows all get a sentence', () => {
    expect(
      rowChanges(
        ['alias', 'unit', 'who', 'pos'],
        table(
          ['alias', 'unit', 'who', 'pos'],
          [
            ['u1', 'Settlers', 'own', '31,72'],
            ['u2', 'Settlers', 'own', '31,72'],
          ]
        ),
        [
          ['u1', 'Settlers', 'own', '32,72'],
          ['u3', 'Warriors', 'own', '30,70'],
        ]
      )
    ).toEqual([
      'u1: pos 31,72 -> 32,72',
      'u3 new: Warriors own',
      'u2 gone (was Settlers own)',
    ]);
  });

  test('several changed columns are joined in column order', () => {
    expect(
      rowChanges(
        ['alias', 'pos', 'moves'],
        table(['alias', 'pos', 'moves'], [['u1', '31,72', '3/3']]),
        [['u1', '32,72', '1/3']]
      )
    ).toEqual(['u1: pos 31,72 -> 32,72; moves 3/3 -> 1/3']);
  });

  /**
   * The reviewer's exact case.  `cell` writes a server-supplied `'﻿Artillery'`
   * verbatim, `parseTable` reads it back, and the two must be the same string —
   * otherwise the very next refresh appends `u1: kind Artillery -> ﻿Artillery`
   * to `state/delta.md`, spends the MAX_DELTA_LINES budget on a lie, and leaves
   * a merged `state/units.tsv` whose widths no longer match CPython's.
   */
  test('a BOM-carrying cell is not a change against the file that wrote it', () => {
    const written = '# rev 9 turn 3\nalias\tkind     \tat\nu1   \t﻿Artillery\t31,72\n';
    expect(parseTable(written).rows).toEqual([['u1', '﻿Artillery', '31,72']]);
    expect(
      rowChanges(['alias', 'kind', 'at'], parseTable(written), [
        ['u1', '﻿Artillery', '31,72'],
      ])
    ).toEqual([]);
  });

  test('a first observation strips with CPython’s whitespace class', () => {
    expect(rowChanges(['a', 'b', 'c'], table([], []), [['u1', '﻿x ', '\x1fy']])).toEqual([
      'u1 ﻿x  \x1fy (first observation)',
    ]);
  });

  test('column 0 is the key, so it never reads as a change', () => {
    expect(
      rowChanges(['alias', 'unit'], table(['alias', 'unit'], [['u1', 'Settlers']]), [
        ['u1', 'Settlers'],
      ])
    ).toEqual([]);
  });

  test('a new row with nothing to describe keeps no dangling colon', () => {
    expect(
      rowChanges(['alias', 'unit', 'who'], table(['alias', 'unit', 'who'], [['u1', 'x', 'y']]), [
        ['u1', 'x', 'y'],
        ['u2', '-', '-'],
      ])
    ).toEqual(['u2 new']);
  });

  test('a row that lost a column reads as a change to a dash', () => {
    expect(
      rowChanges(
        ['alias', 'unit', 'who'],
        table(['alias', 'unit', 'who'], [['u1', 'Settlers', 'own']]),
        [['u1', 'Settlers']]
      )
    ).toEqual(['u1: who own -> -']);
  });
});
