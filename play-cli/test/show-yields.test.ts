/**
 * `just show map --yields` — the per-tile yield overlay.
 *
 * Ports `test_v2_show_map_yields_reads_two_local_files_and_no_socket` from
 * `play/tests/test_client.py`, plus the shapes `render_map_yields`
 * (`state_mirror.py:901-969`) has that the Python suite only reaches through a
 * live game: the degraded list for a window wider than 24, the "nothing priced
 * yet" remedy and the drifted-header case.
 *
 * Every `GOLDEN` entry is the verbatim stdout of `python3 client.py show map
 * --yields` run against the byte-identical mirror below.  Two local files in,
 * zero sockets: `runShow` requires `SessionStore | PrivateFs` and no more.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import { runShow, type ShowOptions } from 'src/commands/show.cmd';
import { renderMapYields, YIELD_TILE_RE, YIELD_WINDOW_MAX } from 'src/render/mirror/yields-overlay';
import { v2StateSchema } from 'src/services/aliases';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, sessionStoreFor } from 'src/services/session-store';
import { FIXTURE_AGENT_ID, FIXTURE_GAME_ID, scratchWorkspace, sessionFile, type Scratch } from 'test/_fixtures';

const YIELD_MIRROR: ReadonlyArray<readonly [string, string]> = [
  ["state/map.txt", "# rev 7 turn 3\n# map 64x64 · 12 of 12 tiles known\n# window x 30..33 y 71..73\n# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n# terrain F=Forest · G=Grassland · H=Hills · O=Ocean\n   71 |GGFH\n   72 |GGgh\n   73 |OOOO\n"],
  ["state/yields.tsv", "# rev 7 turn 3\n# yields 3/3 complete\ntile    \tfood\tshields\ttrade\tworked\tcity\nT(30,71)\t3   \t0      \t1    \tno    \t-\nT(31,72)\t2   \t1      \t0    \tyes   \tc1\nT(32,72)\t1   \t2      \t1    \tno    \t-\n"],
];

const WIDE_MIRROR: ReadonlyArray<readonly [string, string]> = [
  ["state/map.txt", "# rev 7 turn 3\n# map 64x64 · 12 of 12 tiles known\n# window x 30..60 y 71..73\n# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n# terrain F=Forest · G=Grassland · H=Hills · O=Ocean\n   71 |GGFHGGGGGGGGGGGGGGGGGGGGGGGGGGG\n   72 |GGgh\n   73 |OOOO\n"],
  ["state/yields.tsv", "# rev 7 turn 3\n# yields 2/2 complete\ntile    \tfood\tshields\ttrade\tworked\tcity\nT(30,71)\t3   \t0      \t1    \tno    \t-\nT(60,71)\t1   \t2      \t1    \tno    \t-\n"],
];

const OTHER_COLUMNS_MIRROR: ReadonlyArray<readonly [string, string]> = [
  ["state/map.txt", "# rev 7 turn 3\n# map 64x64 · 12 of 12 tiles known\n# window x 30..33 y 71..73\n# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n# terrain F=Forest · G=Grassland · H=Hills · O=Ocean\n   71 |GGFH\n   72 |GGgh\n   73 |OOOO\n"],
  ["state/yields.tsv", "# rev 7 turn 3\nalias\tname\nu1\tSettlers\n"],
];

const GOLDEN = {
  "current/yields_grep": {"code": 2, "stdout": "", "stderr": "error: --yields overlays the terrain grid; run `just show map --yields`\n"},
  "current/yields_map": {"code": 0, "stdout": "# rev 7 turn 3\n# yields · 3 tiles priced · window x 30..32 y 71..72\n# cell TERRAIN food/shields/trade · '?' = not read for that tile\n      30     31     32\n   71 |G3/0/1 G?     F?\n   72 |G?     G2/1/0 g1/2/1\n", "stderr": ""},
  "current/yields_map_json": {"code": 0, "stdout": "{\"command\":\"show\",\"lines\":[\"# rev 7 turn 3\",\"# yields \\u00b7 3 tiles priced \\u00b7 window x 30..32 y 71..72\",\"# cell TERRAIN food/shields/trade \\u00b7 '?' = not read for that tile\",\"      30     31     32\",\"   71 |G3/0/1 G?     F?\",\"   72 |G?     G2/1/0 g1/2/1\"],\"schema_version\":1,\"selection\":\"map --yields\"}\n", "stderr": ""},
  "current/yields_units": {"code": 2, "stdout": "", "stderr": "error: --yields overlays the terrain grid; run `just show map --yields`\n"},
  "empty/yields": {"code": 2, "stdout": "", "stderr": "error: this seat has no map projection yet; run `just turn` or `just state --section map_tiles` to write one\n"},
  "nomap/yields": {"code": 2, "stdout": "", "stderr": "error: this seat has no map projection yet; run `just turn` or `just state --section map_tiles` to write one\n"},
  "nopriced/yields": {"code": 0, "stdout": "# rev 7 turn 3\n# no tile yields read yet; price a city's tiles with `just state --section city_citizens --actor_id c1`\n", "stderr": ""},
  "othercolumns/yields": {"code": 0, "stdout": "# rev 7 turn 3\n# no tile yields read yet; price a city's tiles with `just state --section city_citizens --actor_id c1`\n", "stderr": ""},
  "stale/yields_map": {"code": 0, "stdout": "# rev 7 turn 3\n# yields · 3 tiles priced · window x 30..32 y 71..72\n# cell TERRAIN food/shields/trade · '?' = not read for that tile\n      30     31     32\n   71 |G3/0/1 G?     F?\n   72 |G?     G2/1/0 g1/2/1\n", "stderr": ""},
  "wide/yields": {"code": 0, "stdout": "# rev 7 turn 3\n# yields · 2 tiles priced · window x 30..60 y 71..71\n# cell TERRAIN food/shields/trade · '?' = not read for that tile\n30,71 G 3/0/1\n60,71 G 1/2/1\n", "stderr": ""},
} as const;

// ---------------------------------------------------------------------------

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

const clientState = (revision: number): unknown => ({
  schema_version: 5,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  last_revision: { revision, state_token: `token_3_${revision}`, turn: 3 },
  actions: {},
  pending_catalogs: {},
  batches: {},
  receipts: {},
  action_aliases: { state_revision: null, by_alias: {} },
  entity_aliases: {},
  tile_aliases: {},
  drained_actors: [],
});

interface Seat {
  readonly sessionPath: string;
  readonly mirror: string;
  readonly layer: Layer.Layer<SessionStore | PrivateFs>;
}

const seat = (
  files: ReadonlyArray<readonly [string, string]> = YIELD_MIRROR,
  revision = 7
): Seat => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const home = path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID);
  fs.mkdirSync(home, { mode: 0o700, recursive: true });
  const sessionPath = path.join(home, 'codex-test.json');
  fs.writeFileSync(sessionPath, `${JSON.stringify(sessionFile(), null, 2)}\n`, { mode: 0o600 });
  fs.writeFileSync(
    path.join(home, 'codex-test.v2-state'),
    `${JSON.stringify(clientState(revision), null, 2)}\n`,
    { mode: 0o600 }
  );
  const mirror = path.join(home, 'codex-test');
  for (const [relative, text] of files) {
    const target = path.join(mirror, relative);
    fs.mkdirSync(path.dirname(target), { mode: 0o700, recursive: true });
    fs.writeFileSync(target, text, { mode: 0o600 });
  }
  const store = sessionStoreFor(scratch.workspace, scratch.files, v2StateSchema, {});
  return {
    sessionPath,
    mirror,
    layer: Layer.merge(scratch.layer, Layer.succeed(SessionStore, store)),
  };
};

const show = async (
  fixture: Seat,
  overrides: Partial<ShowOptions> = {}
): Promise<{ readonly stdout: string; readonly stderr: string }> => {
  const out: string[] = [];
  const originalLog = console.log;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.map(String).join(' '));
  try {
    const result = await Effect.runPromise(
      Effect.either(
        Effect.provide(
          runShow({
            session: fixture.sessionPath,
            name: 'map',
            grep: '',
            regex: false,
            yields: true,
            json: false,
            ...overrides,
          }),
          fixture.layer
        )
      )
    );
    return {
      stdout: out.length === 0 ? '' : `${out.join('\n')}\n`,
      stderr: Either.isLeft(result) ? `error: ${result.left.message}\n` : '',
    };
  } finally {
    console.log = originalLog;
  }
};

const golden = async (
  key: keyof typeof GOLDEN,
  fixture: Seat,
  overrides: Partial<ShowOptions> = {}
): Promise<void> => {
  const expected = GOLDEN[key];
  const actual = await show(fixture, overrides);
  expect(actual.stdout).toBe(expected.stdout);
  expect(actual.stderr).toBe(expected.stderr);
};

const overlay = (fixture: Seat): Promise<ReadonlyArray<string>> =>
  Effect.runPromise(Effect.provide(renderMapYields(fixture.mirror), fixture.layer));

// ---------------------------------------------------------------------------

describe('the grid overlay', () => {
  test('two local files become one priced grid', async () => {
    await golden('current/yields_map', seat());
  });

  test('the same lines come back under --json with `map --yields` as the selection', async () => {
    await golden('current/yields_map_json', seat(), { json: true });
  });

  test('a tile inside the window that was never priced renders `?`, not a zero', async () => {
    const lines = await overlay(seat());
    // (31,71) is inside the priced bounding box but carries no yields row.
    expect(lines[4]).toBe('   71 |G3/0/1 G?     F?');
    expect(lines.join('\n')).not.toContain('G0/0/0');
  });

  test('a remembered tile keeps its lowercase terrain character', async () => {
    const lines = await overlay(seat());
    expect(lines[5]).toBe('   72 |G?     G2/1/0 g1/2/1');
  });

  test('the overlay never rewrites either file it read', async () => {
    const fixture = seat();
    const before = fs.readFileSync(path.join(fixture.mirror, 'state', 'map.txt'), 'utf8');
    await golden('current/yields_map', fixture);
    expect(fs.readFileSync(path.join(fixture.mirror, 'state', 'map.txt'), 'utf8')).toBe(before);
  });

  test('the overlay is never prefixed with the staleness banner', async () => {
    // `show map` at the same revision *is* banner-eligible; `--yields` returns
    // before the banner is ever computed, exactly like CPython.
    await golden('stale/yields_map', seat(YIELD_MIRROR, 12));
  });
});

describe('the degraded shapes', () => {
  test('a window wider than 24 columns prints one line per priced tile', async () => {
    await golden('wide/yields', seat(WIDE_MIRROR));
  });

  test('the threshold is inclusive: exactly 24 columns still prints a grid', async () => {
    const wide = (span: number): ReadonlyArray<readonly [string, string]> => {
      const row = 'G'.repeat(span);
      const last = 30 + span - 1;
      return [
        [
          'state/map.txt',
          `# rev 7 turn 3\n# window x 30..${last} y 71..71\n   71 |${row}\n`,
        ],
        [
          'state/yields.tsv',
          '# rev 7 turn 3\ntile\tfood\tshields\ttrade\tworked\tcity\n' +
            `T(30,71)\t3\t0\t1\tno\t-\nT(${last},71)\t1\t2\t1\tno\t-\n`,
        ],
      ];
    };
    expect((await overlay(seat(wide(YIELD_WINDOW_MAX))))[3]).toContain('  30 ');
    expect((await overlay(seat(wide(YIELD_WINDOW_MAX + 1))))[3]).toBe('30,71 G 3/0/1');
  });

  test('a mirror with no yields file names the command that prices tiles', async () => {
    await golden('nopriced/yields', seat([YIELD_MIRROR[0] as readonly [string, string]]));
  });

  test('a yields table whose header drifted is ignored rather than misread', async () => {
    await golden('othercolumns/yields', seat(OTHER_COLUMNS_MIRROR));
  });

  test('no map projection at all is a refusal that names both writers', async () => {
    await golden('nomap/yields', seat([YIELD_MIRROR[1] as readonly [string, string]]));
    await golden('empty/yields', seat([]));
  });

  test('an empty overlay is an empty list, which is not an empty grid', async () => {
    expect(await overlay(seat([]))).toEqual([]);
    expect(await overlay(seat([YIELD_MIRROR[0] as readonly [string, string]]))).toHaveLength(2);
  });
});

describe('--yields anywhere else', () => {
  test('it refuses on another section and on --grep', async () => {
    await golden('current/yields_units', seat(), { name: 'units' });
    await golden('current/yields_grep', seat(), { name: '', grep: 'Settlers' });
    await golden('current/yields_units', seat(), { name: '' });
  });

  test('`map` plus --grep is the --yields refusal, not the "not both" one', async () => {
    await golden('current/yields_grep', seat(), { name: '', grep: 'G' });
  });
});

describe('the tile handle', () => {
  test('only a well-formed `T(x,y)` with at most four digits is priced', () => {
    expect(YIELD_TILE_RE.test('T(31,72)')).toBe(true);
    expect(YIELD_TILE_RE.test('T(-1,-2)')).toBe(true);
    expect(YIELD_TILE_RE.test('T(12345,1)')).toBe(false);
    expect(YIELD_TILE_RE.test('t(1,2)')).toBe(false);
    expect(YIELD_TILE_RE.test('T(1, 2)')).toBe(false);
    expect(YIELD_TILE_RE.test('u1')).toBe(false);
  });
});
