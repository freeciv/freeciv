/**
 * The table round trip and the map parse primitives.
 *
 * Ports the parse half of `TableRenderingTests` and the read half of `MapTests`
 * from `play/tests/test_state_mirror.py`.  The renderers that feed `tableText`
 * are U07's and the map writers are U08's; what is under test here is the
 * contract they both build on — column widths, the tab layout, the `# rev`
 * stamp, and a grid that survives being written and read back.
 */
import { describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import {
  knownTiles,
  mapSize,
  pageNote,
  parseDelta,
  parseMap,
  parseTable,
  parseTileKey,
  splitLines,
  tableByKey,
  tableText,
  terrainChars,
  tileChars,
  tileKey,
  writeMirror,
  type MirrorTable,
} from 'src/services/mirror';
import { scratchWorkspace } from 'test/_fixtures';

const REV = { turn: 3, revision: 9 } as const;

const UNITS = tableText(
  REV,
  ['units 2/2 complete'],
  ['alias', 'unit', 'who', 'pos', 'moves'],
  [
    ['u1', 'Settlers', 'own', '31,72', '3/3'],
    ['u2', 'Workers', 'own', '30,71', '1/3'],
  ]
);

describe('tableText', () => {
  test('the rendering is byte-identical to CPython', () => {
    expect(UNITS).toBe(
      [
        '# rev 9 turn 3',
        '# units 2/2 complete',
        'alias\tunit    \twho\tpos  \tmoves',
        'u1   \tSettlers\town\t31,72\t3/3',
        'u2   \tWorkers \town\t30,71\t1/3',
        '',
      ].join('\n')
    );
  });

  // Regression: widths and padding are measured with CPython's `len` /
  // `str.ljust`, which count code points.  Counting UTF-16 units made one
  // astral cell over-wide and pushed every row of the file off the golden.
  test('an astral cell is padded by code points', () => {
    expect(
      tableText(
        { turn: 9, revision: 3 },
        ['cities 2/2 complete'],
        ['alias', 'city', 'pos'],
        [
          ['c1', '\u{1F600}\u{1F600}', '31,72'],
          ['c2', 'Londonderry', '30,71'],
        ]
      )
    ).toBe(
      [
        '# rev 3 turn 9',
        '# cities 2/2 complete',
        'alias\tcity       \tpos',
        'c1   \t\u{1F600}\u{1F600}         \t31,72',
        'c2   \tLondonderry\t30,71',
        '',
      ].join('\n')
    );
  });

  test('column 0 is aligned across every row', () => {
    const body = splitLines(UNITS).filter((line) => !line.startsWith('#'));
    expect(new Set(body.map((line) => line.indexOf('\t'))).size).toBe(1);
  });

  test('the trailing column never carries its padding into the file', () => {
    expect(
      tableText(REV, [], ['a', 'bbbb'], [
        ['xxxxxx', 'y'],
        ['z'],
      ])
    ).toBe(['# rev 9 turn 3', 'a     \tbbbb', 'xxxxxx\ty', 'z', ''].join('\n'));
  });

  test('a row wider than the header keeps its extra cells unpadded', () => {
    expect(tableText(REV, [], ['a'], [['x', 'extra']])).toBe(
      ['# rev 9 turn 3', 'a', 'x\textra', ''].join('\n')
    );
  });

  test('notes are comment lines between the stamp and the header row', () => {
    expect(splitLines(tableText(REV, ['one', 'two'], ['a'], []))).toEqual([
      '# rev 9 turn 3',
      '# one',
      '# two',
      'a',
    ]);
  });
});

describe('parseTable', () => {
  test('the round trip recovers the revision, columns and rows', () => {
    const table = parseTable(UNITS);
    expect(table.revision).toEqual({ turn: 3, revision: 9 });
    expect(table.columns).toEqual(['alias', 'unit', 'who', 'pos', 'moves']);
    expect(table.rows).toEqual([
      ['u1', 'Settlers', 'own', '31,72', '3/3'],
      ['u2', 'Workers', 'own', '30,71', '1/3'],
    ]);
  });

  test('an absent or empty file is an empty table', () => {
    for (const text of [null, undefined, '']) {
      expect(parseTable(text)).toEqual({ revision: null, columns: [], rows: [] });
    }
  });

  test('a file that is only comments keeps its stamp and holds no rows', () => {
    expect(parseTable('# rev 9 turn 3\n# units 0/0 complete\n')).toEqual({
      revision: { turn: 3, revision: 9 },
      columns: [],
      rows: [],
    });
  });

  test('an unstamped file parses with no revision', () => {
    expect(parseTable('alias\tunit\nu1\tSettlers\n').revision).toBe(null);
  });

  test('rows are keyed by column 0, the last row winning', () => {
    const table: MirrorTable = {
      revision: null,
      columns: ['alias', 'unit'],
      rows: [
        ['u1', 'Settlers'],
        ['u2', 'Workers'],
        ['u1', 'Explorer'],
      ],
    };
    expect([...tableByKey(table)]).toEqual([
      ['u1', ['u1', 'Explorer']],
      ['u2', ['u2', 'Workers']],
    ]);
  });

  /**
   * Regression: the read half must use CPython's `str.strip()`, not JavaScript
   * `.trim()`.  The two character classes are not nested — `.trim()` removes
   * U+FEFF, which Python keeps, and keeps U+001C-U+001F and U+0085, which
   * Python removes.  `cell` deliberately does not strip U+FEFF, so a
   * server-supplied name carrying one is written verbatim; reading it back as a
   * *different* string makes `rowChanges` report a change that never happened,
   * on every refresh, forever.  Golden from `state_mirror._parse_table`.
   */
  test('cells are stripped with CPython’s whitespace class, not trim’s', () => {
    const table = parseTable('# rev 4 turn 2 extra\n#note\n\na\tb\n ﻿x\ty \n');
    expect(table.columns).toEqual(['a', 'b']);
    expect(table.rows).toEqual([['﻿x', 'y']]);
  });

  test('a BOM in a name survives the write/read round trip intact', () => {
    const text = tableText(
      REV,
      ['units 1/1 complete'],
      ['alias', 'kind', 'at'],
      [['u1', '﻿Artillery', '31,72']]
    );
    expect(parseTable(text).rows).toEqual([['u1', '﻿Artillery', '31,72']]);
  });

  test('an information separator ends a row the way splitlines ends it', () => {
    // U+001C-U+001E are `str.splitlines()` boundaries in CPython but not
    // `String.split('\n')` ones; treating one as text merges two rows.
    expect(parseTable('# rev 4 turn 2\na\tb\nu1\txu2\ty\n').rows).toEqual([
      ['u1', 'x'],
      ['u2', 'y'],
    ]);
  });

  test('a merge at one revision cannot erase the page before it', () => {
    const first = parseTable(
      tableText(REV, ['units 1/2 partial - fetch the next cursor'], ['alias', 'unit'], [
        ['u1', 'Settlers'],
      ])
    );
    const merged = new Map(tableByKey(first));
    merged.set('u2', ['u2', 'Workers']);
    const text = tableText(REV, ['units 2/2 complete'], ['alias', 'unit'], [...merged.values()]);
    expect(text).toContain('Settlers');
    expect(text).toContain('Workers');
    expect(text).toContain('# units 2/2 complete');
  });
});

describe('pageNote', () => {
  test('every shape CPython emits', () => {
    expect(pageNote('units', 2, 2, true)).toBe('units 2/2 complete');
    expect(pageNote('units', 1, 2, false)).toBe('units 1/2 partial - fetch the next cursor');
    expect(pageNote('units', 1, null, false)).toBe('units 1/- partial - fetch the next cursor');
  });
});

describe('terrainChars', () => {
  test('the documented codes win, and an invented terrain draws an initial', () => {
    expect([...terrainChars(['Grassland', 'Ocean', 'Glacier', 'Weird Wasteland', 'Grassland'])]).toEqual(
      [
        ['Glacier', 'C'],
        ['Grassland', 'G'],
        ['Ocean', 'O'],
        ['Weird Wasteland', 'W'],
      ]
    );
  });

  test('a code the file already uses never changes meaning mid-grid', () => {
    const seeded = terrainChars(['Grassland', 'Gravel Pit'], new Map([['Gravel Pit', 'G']]));
    expect(seeded.get('Gravel Pit')).toBe('G');
    expect(seeded.get('Grassland')).toBe('E');
  });
});

const MAP_TEXT = [
  '# rev 9 turn 3',
  '# map 64x64 · 2 of 4 tiles known',
  '# window x 30..31 y 71..72',
  "# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered",
  '# terrain G=Grassland · O=Ocean',
  '   71 |Go',
  '   72 |?G',
  '',
].join('\n');

describe('parseMap', () => {
  test('the grid, the legend and the stamp all come back', () => {
    const map = parseMap(MAP_TEXT);
    expect(map.revision).toEqual({ turn: 3, revision: 9 });
    expect([...map.legend]).toEqual([
      ['Grassland', 'G'],
      ['Ocean', 'O'],
    ]);
    expect([...map.grid]).toEqual([
      ['30,71', 'G'],
      ['31,71', 'o'],
      ['30,72', '?'],
      ['31,72', 'G'],
    ]);
    expect(knownTiles(map)).toBe(3);
  });

  test('a map with no window line yields no grid', () => {
    const map = parseMap('# rev 9 turn 3\n# no tiles known yet\n');
    expect(map.grid.size).toBe(0);
    expect(map.revision).toEqual({ turn: 3, revision: 9 });
  });

  test('a blank column inside a row is not a tile', () => {
    const map = parseMap(
      ['# rev 9 turn 3', '# window x 30..32 y 71..71', '   71 |G G', ''].join('\n')
    );
    expect([...map.grid.keys()]).toEqual(['30,71', '32,71']);
  });

  test('an absent file is an empty map', () => {
    expect(parseMap(null)).toEqual({ revision: null, legend: new Map(), grid: new Map() });
  });

  test('tile keys round trip', () => {
    expect(parseTileKey(tileKey(-3, 72))).toEqual([-3, 72]);
    expect(parseTileKey('nope')).toBe(null);
  });

  /**
   * The legend is what gives a character its meaning across a merge: `_render_map`
   * seeds `terrainChars` with the legend already on disk so a merged grid never
   * changes what a character means mid-file.  `.trim()` on the legend pair would
   * drop a U+FEFF the writer kept, the seeded name would not match the incoming
   * one, and the terrain would be re-assigned a different code.
   */
  test('a legend name keeps every character CPython keeps', () => {
    // `.trim()` would eat the BOM off `﻿Grassland` and the seeded legend would
    // no longer answer for the name `_tile_chars` is handed on the next merge.
    const map = parseMap('# rev 9 turn 3\n# terrain G=﻿Grassland · O=\x1fOcean\n');
    expect([...map.legend]).toEqual([
      ['﻿Grassland', 'G'],
      ['\x1fOcean', 'O'],
    ]);
  });

  test('a legend pair whose char is not Python-\\S is dropped whole', () => {
    // `\S` consumes the BOM as the character, then fails on `=`; CPython keeps
    // only the second pair, and so must the port.
    expect([...parseMap('# rev 9 turn 3\n# terrain ﻿G=Grass · O=Ocean\n').legend]).toEqual([
      ['Ocean', 'O'],
    ]);
  });

  /**
   * `_render_map` writes the server's terrain name into `# terrain …` without
   * passing it through `_cell`, so a name holding a newline forges whole lines
   * in `state/map.txt`.  Whether a forged line reads as a grid row is decided
   * by `_MAP_ROW_RE`'s leading `\s*`, and Python's `\s` is not JavaScript's:
   * it matches U+001F and does not match U+FEFF.  Goldens from `_parse_map`.
   */
  test('a row indented with U+001F is a row, exactly as CPython reads it', () => {
    const map = parseMap('# rev 9 turn 3\n# window x 30..32 y 71..71\n\x1f 71 |GGO\n');
    expect([...map.grid]).toEqual([
      ['30,71', 'G'],
      ['31,71', 'G'],
      ['32,71', 'O'],
    ]);
  });

  test('a row indented with U+FEFF is not a row, exactly as CPython reads it', () => {
    expect(parseMap('# rev 9 turn 3\n# window x 30..32 y 71..71\n﻿71 |GGO\n').grid.size).toBe(0);
  });

  test('U+FEFF is a legend character to Python’s \\S, so it is one here', () => {
    // JavaScript's `\S` excludes U+FEFF, which would have thrown this pair away
    // where CPython keeps it — and a dropped pair silently re-assigns the
    // terrain's code on the next merge.
    expect([...parseMap('# rev 9 turn 3\n# terrain ﻿=Grass\n').legend]).toEqual([
      ['Grass', '﻿'],
    ]);
  });
});

describe('tileChars', () => {
  const run = <A, E>(effect: Effect.Effect<A, E>): Either.Either<A, E> =>
    Effect.runSync(Effect.either(effect));

  test('visibility alone decides fog, and an unknown tile hides its terrain', () => {
    const either = run(
      tileChars(
        [
          { id: 't1', x: 30, y: 71, visibility: 'visible', terrain: 'Grassland' },
          { id: 't2', x: 31, y: 71, visibility: 'fogged', terrain: 'Ocean' },
          { id: 't3', x: 30, y: 72, visibility: 'unknown', terrain: 'Mountains' },
        ],
        new Map()
      )
    );
    expect(Either.isRight(either)).toBe(true);
    if (Either.isLeft(either)) return;
    expect([...either.right.grid]).toEqual([
      ['30,71', 'G'],
      ['31,71', 'o'],
      ['30,72', '?'],
    ]);
    // `Mountains` never entered the legend: the seat has not seen that tile.
    expect([...either.right.chars]).toEqual([
      ['Grassland', 'G'],
      ['Ocean', 'O'],
    ]);
  });

  test('a non-object item is refused rather than blanked', () => {
    const either = run(tileChars(['oops'], new Map()));
    expect(Either.isLeft(either) ? either.left.message : '').toBe(
      'state mirror: tile item is not an object'
    );
  });

  test('a tile without integer coordinates is refused', () => {
    const either = run(
      tileChars([{ id: 't1', x: '30', y: 71, visibility: 'visible' }], new Map())
    );
    expect(Either.isLeft(either) ? either.left.message : '').toBe(
      'state mirror: tile item carries no coordinates'
    );
  });
});

describe('mapSize', () => {
  test('the overview row named map supplies the size the map header prints', () => {
    const scratch = scratchWorkspace();
    try {
      const dir = `${scratch.workspace.stateRoot}/game_x/codex-test`;
      const write = (rows: ReadonlyArray<ReadonlyArray<string>>): void => {
        Effect.runSync(
          Effect.provide(
            writeMirror(
              dir,
              ['state', 'overview.tsv'],
              tableText(REV, ['overview 1/1 complete'], ['fact', 'value'], rows)
            ),
            scratch.layer
          )
        );
      };
      const read = (): string | null =>
        Effect.runSync(Effect.provide(mapSize(dir), scratch.layer));

      write([
        ['turn', '3'],
        ['map', '64x48'],
      ]);
      expect(read()).toBe('64x48');

      write([['map', '-']]);
      expect(read()).toBe(null);

      write([['turn', '3']]);
      expect(read()).toBe(null);
    } finally {
      scratch.cleanup();
    }
  });

  test('no overview at all is no size', () => {
    const scratch = scratchWorkspace();
    try {
      expect(
        Effect.runSync(
          Effect.provide(mapSize(`${scratch.workspace.stateRoot}/game_x/codex-test`), scratch.layer)
        )
      ).toBe(null);
    } finally {
      scratch.cleanup();
    }
  });
});

describe('splitLines', () => {
  test('CPython semantics: a trailing newline is not a trailing empty line', () => {
    expect(splitLines('a\nb\n')).toEqual(['a', 'b']);
    expect(splitLines('a\nb')).toEqual(['a', 'b']);
    expect(splitLines('')).toEqual([]);
    expect(splitLines('\n')).toEqual(['']);
  });

  test('the delta parser sees the same lines the table parser does', () => {
    expect(parseDelta('# rev 9 turn 3\nno earlier mirror · last update: state\n').revision).toEqual(
      { turn: 3, revision: 9 }
    );
  });
});
