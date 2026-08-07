/**
 * The mirror's section renderers and the `update_from_page` dispatch.
 *
 * Ports the renderer half of `TableRenderingTests`, the merge half of
 * `RevisionStampTests`, `ApiTests.test_update_from_page_returns_the_files_it_wrote`
 * and `DriftTests` from `play/tests/test_state_mirror.py`, plus the receipt
 * cases of `DeltaTests`.  Every golden string was produced by the CPython
 * original on the same input.
 *
 * The round-trip case is this unit's own: what a renderer writes, `parseTable`
 * has to read back cell-for-cell, because the merge contract is exactly that
 * round trip.
 */
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import type { PrivateFs } from 'src/services/private-fs';
import { mirrorDir, parseTable, readMirror } from 'src/services/mirror';
import { updateFromPage } from 'src/services/mirror/update-page';
import { updateFromReceipt } from 'src/services/mirror/update-receipt';
import { renderDiplomacy } from 'src/render/mirror/diplomacy';
import { renderGovernments } from 'src/render/mirror/governments';
import { renderNations } from 'src/render/mirror/nations';
import { renderStyles } from 'src/render/mirror/styles';
import { renderUnits } from 'src/render/mirror/units';
import { scratchWorkspace, type Scratch } from 'test/_fixtures';

const GAME_ID = 'game_12345678901234567890';
const AGENT_ID = 'agent_test-controller';
const UNIT_A = `unit_${'a'.repeat(32)}`;
const UNIT_B = `unit_${'b'.repeat(32)}`;
const CITY_A = `city_${'c'.repeat(32)}`;

// ---------------------------------------------------------------------------
// The CPython test module's payload builders, ported verbatim
// ---------------------------------------------------------------------------

const revision = (number = 7, turn = 3): JsonObject => ({
  turn,
  revision: number,
  state_token: `state_token_${String(number).padStart(32, '0')}`,
});

interface PageOptions {
  readonly cursor?: string | null;
  readonly total?: number;
  readonly scope?: JsonObject;
}

const page = (
  section: string,
  rev: JsonObject,
  items: ReadonlyArray<JsonValue>,
  options: PageOptions = {}
): JsonObject => ({
  schema_version: 2,
  control_protocol: 'full-control-v2',
  game_id: GAME_ID,
  agent_id: AGENT_ID,
  state_revision: rev,
  page: {
    section,
    items,
    total_items: options.total ?? items.length,
    next_cursor: options.cursor ?? null,
    cursor_expires_at: null,
    ...(options.scope === undefined ? {} : { scope: options.scope }),
  },
});

interface UnitOptions {
  readonly kind?: string;
  readonly x?: number;
  readonly y?: number;
  readonly moves?: number;
  readonly hp?: number;
  readonly extra?: JsonObject;
}

const unit = (identifier: string = UNIT_A, options: UnitOptions = {}): JsonObject => ({
  id: identifier,
  scope: 'own',
  owner_player_id: `player_${'e'.repeat(32)}`,
  type: options.kind ?? 'Settlers',
  type_id: 'unit_type_1',
  tile_id: `tile_${'f'.repeat(32)}`,
  x: options.x ?? 31,
  y: options.y ?? 72,
  hp: options.hp ?? 20,
  moves: options.moves ?? 3,
  type_stats: { max_hp: 20, move_rate: 3 },
  activity: { name: 'idle', progress: 0, target: null },
  route: null,
  ...(options.extra ?? {}),
});

const city = (name = 'London', size = 1): JsonObject => ({
  id: CITY_A,
  name,
  x: 31,
  y: 72,
  size,
  surplus: { food: 2, shields: 1, trade: 0 },
  production: {
    kind: 'unit',
    name: 'Warriors',
    shield_stock: 2,
    shield_cost: 10,
    buy_cost: 44,
    can_buy: true,
  },
});

const receipt = (state = 'applied', rev: JsonObject = revision(8)): JsonObject => ({
  schema_version: 2,
  control_protocol: 'full-control-v2',
  game_id: GAME_ID,
  agent_id: AGENT_ID,
  batch_id: `batch_${'9'.repeat(32)}`,
  receipt_state: state,
  idempotent: false,
  state_revision: rev,
  error:
    state === 'rejected'
      ? {
          schema_version: 2,
          control_protocol: 'full-control-v2',
          error: {
            code: 'illegal_action',
            message: 'that action is not legal now',
            retryable: false,
            details: {},
          },
          state_revision: rev,
        }
      : null,
  observation: null,
});

// ---------------------------------------------------------------------------
// The workspace harness
// ---------------------------------------------------------------------------

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

interface Mirror {
  readonly dir: string;
  readonly run: <A, E>(effect: Effect.Effect<A, E, PrivateFs>) => Either.Either<A, E>;
  readonly write: (
    command: string,
    payload: JsonObject,
    aliases?: Readonly<Record<string, string>>
  ) => ReadonlyArray<string>;
  readonly read: (...relative: ReadonlyArray<string>) => string;
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
    write: (command, payload, aliases) =>
      right(
        run(
          updateFromPage(dir, command, payload, aliases === undefined ? {} : { aliases })
        )
      ),
    read: (...relative) => {
      const text = right(run(readMirror(dir, relative)));
      expect(typeof text).toBe('string');
      return text ?? '';
    },
  };
};

const right = <A, E>(either: Either.Either<A, E>): A => {
  if (Either.isLeft(either)) {
    const failure: unknown = either.left;
    const message =
      typeof failure === 'object' && failure !== null && 'message' in failure
        ? String((failure as { message: unknown }).message)
        : String(failure);
    throw new Error(`expected success, got: ${message}`);
  }
  return either.right;
};

const leftMessage = <A, E>(either: Either.Either<A, E>): string => {
  expect(Either.isLeft(either)).toBe(true);
  if (!Either.isLeft(either)) return '';
  const failure: unknown = either.left;
  return typeof failure === 'object' && failure !== null && 'message' in failure
    ? String((failure as { message: unknown }).message)
    : String(failure);
};

const bodyLines = (text: string): ReadonlyArray<string> =>
  text.split('\n').filter((line) => line !== '' && !line.startsWith('#'));

const pure = <A, E>(effect: Effect.Effect<A, E>): A => Effect.runSync(effect);

// ---------------------------------------------------------------------------
// TableRenderingTests
// ---------------------------------------------------------------------------

describe('the units table', () => {
  test('is tab delimited and column aligned', () => {
    const mirror = freshMirror();
    mirror.write(
      'state',
      page('units', revision(9), [
        unit(UNIT_A),
        unit(UNIT_B, {
          kind: 'Workers',
          x: 30,
          y: 71,
          moves: 1,
          extra: {
            route: {
              mode: 'goto',
              path_step_count: 4,
              destination: { tile_id: 'tile_z', x: 27, y: 65 },
            },
          },
        }),
      ])
    );
    const body = bodyLines(mirror.read('state', 'units.tsv'));
    expect((body[0] ?? '').split('\t').map((name) => name.trim())).toEqual([
      'alias',
      'unit',
      'who',
      'pos',
      'moves',
      'hp',
      'activity',
      'orders',
    ]);
    const offsets = new Set(
      body.filter((line) => line.includes('\t')).map((line) => line.indexOf('\t'))
    );
    expect(offsets.size).toBe(1);
    const second = (body[2] ?? '').split('\t');
    expect(second[1]?.trim()).toBe('Workers');
    expect(second[3]?.trim()).toBe('30,71');
    expect(second[7]?.trim()).toBe('→(27,65) 4st');
  });

  test('uses the aliases the CLI supplied, verbatim', () => {
    const mirror = freshMirror();
    mirror.write('state', page('units', revision(9), [unit(UNIT_A)]), { [UNIT_A]: 'u1' });
    const rows = mirror
      .read('state', 'units.tsv')
      .split('\n')
      .filter((line) => line.startsWith('u1'));
    expect(rows).toHaveLength(1);
    expect((rows[0] ?? '').split('\t')[1]?.trim()).toBe('Settlers');
  });

  test('renders a fact the payload omits as a visible dash', () => {
    const mirror = freshMirror();
    const item = unit();
    delete (item as Record<string, unknown>)['moves'];
    mirror.write('state', page('units', revision(9), [item]));
    const row = mirror
      .read('state', 'units.tsv')
      .split('\n')
      .find((line) => line.startsWith('u.'));
    expect((row ?? '').split('\t')[4]?.trim()).toBe('-');
  });

  test('falls back to a display handle when no alias map is supplied', () => {
    const rendered = pure(renderUnits([unit(UNIT_A)], null));
    expect(rendered.rows[0]?.[0]).toBe(`u.${UNIT_A.slice(5, 11)}`);
  });

  test('summarizes a route whose path is unavailable by its order count', () => {
    const rendered = pure(
      renderUnits(
        [
          unit(UNIT_A, {
            extra: {
              route: {
                mode: 'patrol',
                path_available: false,
                path_step_count: 4,
                order_count: 2,
                destination: { x: 27, y: 65 },
              },
            },
          }),
        ],
        { [UNIT_A]: 'u1' }
      )
    );
    expect(rendered.rows[0]?.[7]).toBe('→(27,65) 2st patrol');
  });

  test('names the activity target when the payload carries one', () => {
    const rendered = pure(
      renderUnits(
        [
          unit(UNIT_A, {
            extra: { activity: { name: 'building', target: { name: 'Road' } } },
          }),
        ],
        { [UNIT_A]: 'u1' }
      )
    );
    expect(rendered.rows[0]?.[6]).toBe('building:Road');
  });
});

describe('sanitization', () => {
  test('a hostile name cannot forge a header line', () => {
    const mirror = freshMirror();
    mirror.write('state', page('cities', revision(9), [city('Lon\ndon\t# rev 999 turn 999')]));
    const text = mirror.read('state', 'cities.tsv');
    const lines = text.split('\n');
    expect(lines.filter((line) => line.startsWith('#'))).toHaveLength(2);
    expect(text).toContain('Lon don # rev 999 turn 999');
    expect(lines.filter((line) => line.startsWith('c.'))).toHaveLength(1);
  });
});

describe('the overview table', () => {
  test('renders one row per fact', () => {
    const mirror = freshMirror();
    mirror.write(
      'state',
      page('overview', revision(9), [
        {
          turn: 3,
          map: { width: 64, height: 48 },
          player: {
            government: 'Despotism',
            economy: { gold: 50, tax: 40, luxury: 0, science: 60 },
          },
          research: null,
        },
      ])
    );
    const text = mirror.read('state', 'overview.tsv');
    expect(text).toContain('gold');
    expect(text).toContain('50');
    expect(text).toContain('tax40 lux0 sci60');
    expect(text).toContain('64x48');
    expect(text).toContain('(none yet)');
    expect(text).not.toContain('nation');
  });

  test('leads with the exact score and falls back to the provable bound', () => {
    const mirror = freshMirror();
    mirror.write(
      'state',
      page('overview', revision(9), [
        {
          score: { lower_bound: 17, components: { units: 3, cities: 0 } },
          counts: { cities: 1, units: 2, known_tiles: 40, legal_actions: 9, chat: 4 },
        },
      ])
    );
    const text = mirror.read('state', 'overview.tsv');
    expect(text).toContain('>=17');
    // `cities 0` is falsy, so it is not one of the components named.
    expect(text).toContain('units 3');
    expect(text).not.toContain('cities 0');
    expect(text).toContain('count_chat');
  });
});

describe('the catalog tables', () => {
  test('nations, styles and governments render their id columns', () => {
    const nations = pure(
      renderNations([{ id: 'nation_1', name: 'English', default_style_id: 'style_1' }], null)
    );
    expect(nations.columns).toEqual(['id', 'nation', 'default_style_id']);
    expect(nations.rows[0]).toEqual(['nation_1', 'English', 'style_1']);
    const styles = pure(renderStyles([{ id: 'style_1', name: 'European' }], null));
    expect(styles.rows[0]).toEqual(['style_1', 'European']);
    const governments = pure(
      renderGovernments(
        [
          {
            id: 'government_1',
            name: 'Despotism',
            current: true,
            target: false,
            can_change: false,
          },
        ],
        null
      )
    );
    expect(governments.rows[0]).toEqual(['government_1', 'Despotism', 'yes', 'no', 'no']);
  });

  test('a diplomacy row spells out an open meeting', () => {
    const rendered = pure(
      renderDiplomacy(
        [
          {
            relation_id: `relation_${'a'.repeat(32)}`,
            player_name: 'Bob',
            nation: 'French',
            state: 'war',
            has_embassy: false,
            meeting: { clause_count: 2, self_accepted: true, other_accepted: false },
          },
          {
            relation_id: `relation_${'b'.repeat(32)}`,
            player_name: 'Carol',
            nation: 'Roman',
            state: 'peace',
            has_embassy: true,
            meeting: null,
          },
        ],
        { [`relation_${'a'.repeat(32)}`]: 'r1', [`relation_${'b'.repeat(32)}`]: 'r2' }
      )
    );
    expect(rendered.rows[0]).toEqual(['r1', 'Bob', 'French', 'war', 'no', 'open', '2', 'you']);
    expect(rendered.rows[1]).toEqual([
      'r2',
      'Carol',
      'Roman',
      'peace',
      'yes',
      '-',
      '-',
      '-',
    ]);
  });

  test('an open meeting nobody has accepted says so', () => {
    const rendered = pure(
      renderDiplomacy([{ relation_id: 'relation_1', meeting: { clause_count: 1 } }], null)
    );
    expect(rendered.rows[0]?.[7]).toBe('neither');
  });
});

// ---------------------------------------------------------------------------
// The round trip: what a renderer writes, parseTable reads back
// ---------------------------------------------------------------------------

describe('the render/parse round trip', () => {
  test('every cell survives the file it was written to', () => {
    const mirror = freshMirror();
    const rendered = pure(
      renderUnits(
        [
          unit(UNIT_A),
          unit(UNIT_B, {
            kind: 'Workers',
            x: 30,
            y: 71,
            moves: 1,
            extra: {
              route: { mode: 'goto', path_step_count: 4, destination: { x: 27, y: 65 } },
            },
          }),
        ],
        { [UNIT_A]: 'u1', [UNIT_B]: 'u2' }
      )
    );
    mirror.write(
      'state',
      page('units', revision(9), [
        unit(UNIT_A),
        unit(UNIT_B, {
          kind: 'Workers',
          x: 30,
          y: 71,
          moves: 1,
          extra: {
            route: { mode: 'goto', path_step_count: 4, destination: { x: 27, y: 65 } },
          },
        }),
      ]),
      { [UNIT_A]: 'u1', [UNIT_B]: 'u2' }
    );
    const parsed = parseTable(mirror.read('state', 'units.tsv'));
    expect(parsed.revision).toEqual({ turn: 3, revision: 9 });
    expect(parsed.columns).toEqual([...rendered.columns]);
    expect(parsed.rows).toEqual(rendered.rows.map((row) => [...row]));
  });
});

// ---------------------------------------------------------------------------
// The update_from_page dispatch: merge, staleness, drift
// ---------------------------------------------------------------------------

describe('update_from_page', () => {
  test('returns the files it wrote', () => {
    const mirror = freshMirror();
    const written = mirror.write('state', page('units', revision(9), [unit()]));
    expect(written.map((file) => path.relative(mirror.dir, file))).toEqual([
      path.join('state', 'units.tsv'),
      path.join('state', 'delta.md'),
    ]);
  });

  test('an older page never overwrites a newer mirror', () => {
    const mirror = freshMirror();
    mirror.write('state', page('units', revision(9), [unit(UNIT_A, { moves: 1 })]));
    const written = mirror.write('state', page('units', revision(7), [unit(UNIT_A)]));
    expect(written).toEqual([]);
    const text = mirror.read('state', 'units.tsv');
    expect(text.startsWith('# rev 9 turn 3')).toBe(true);
    expect(text).toContain('1/3');
    expect(text).not.toContain('3/3');
  });

  test('a partial page at the same revision merges', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    mirror.write(
      'state',
      page('units', rev, [unit(UNIT_A)], { cursor: `cursor_${'1'.repeat(32)}`, total: 2 })
    );
    mirror.write('state', page('units', rev, [unit(UNIT_B, { kind: 'Workers' })], { total: 2 }));
    const text = mirror.read('state', 'units.tsv');
    expect(text).toContain('Settlers');
    expect(text).toContain('Workers');
    expect(text).toContain('units 2/2 complete');
  });

  test('a complete page at the same revision replaces rather than merges', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    mirror.write('state', page('units', rev, [unit(UNIT_A), unit(UNIT_B, { kind: 'Workers' })]));
    mirror.write('state', page('units', rev, [unit(UNIT_A)]));
    const text = mirror.read('state', 'units.tsv');
    expect(text).toContain('Settlers');
    expect(text).not.toContain('Workers');
    expect(text).toContain('units 1/1 complete');
  });

  test('a windowed page reports itself partial', () => {
    const mirror = freshMirror();
    mirror.write(
      'state',
      page('units', revision(9), [unit(UNIT_A)], {
        cursor: `cursor_${'1'.repeat(32)}`,
        total: 4,
      })
    );
    expect(mirror.read('state', 'units.tsv')).toContain(
      'units 1/4 partial - fetch the next cursor'
    );
  });

  test('the overview page note counts items, not rows', () => {
    const mirror = freshMirror();
    mirror.write('state', page('overview', revision(9), [{ turn: 3, phase: 0 }]));
    expect(mirror.read('state', 'overview.tsv')).toContain('overview 1/1 complete');
  });

  test('an unmirrored section is ignored', () => {
    const mirror = freshMirror();
    expect(mirror.write('state', page('tombstones', revision(9), []))).toEqual([]);
    expect(right(mirror.run(readMirror(mirror.dir, ['state', 'delta.md'])))).toBeNull();
  });

  test('a page without a revision is refused', () => {
    const mirror = freshMirror();
    const broken = page('units', revision(9), [unit()]);
    (broken as Record<string, unknown>)['state_revision'] = { state_token: 'x' };
    const message = leftMessage(mirror.run(updateFromPage(mirror.dir, 'state', broken)));
    expect(message).toContain('state revision counters');
  });

  test('a page without a page envelope is refused', () => {
    const mirror = freshMirror();
    const message = leftMessage(
      mirror.run(updateFromPage(mirror.dir, 'state', { state_revision: revision(9) }))
    );
    expect(message).toContain('page payload carries no page envelope');
  });

  test('a non-object item is refused rather than blanked', () => {
    const mirror = freshMirror();
    const message = leftMessage(
      mirror.run(updateFromPage(mirror.dir, 'state', page('units', revision(9), ['oops'])))
    );
    expect(message).toBe('state mirror: unit item is not an object');
  });

  test('a page carrying too many items is refused', () => {
    const mirror = freshMirror();
    const items = Array.from({ length: 4097 }, (): JsonValue => unit());
    const message = leftMessage(
      mirror.run(updateFromPage(mirror.dir, 'state', page('units', revision(9), items)))
    );
    expect(message).toContain('too many items');
  });

  test('every tiles section routes to U08’s map writer', () => {
    for (const section of ['known_tiles', 'map_tiles', 'tile_window']) {
      const mirror = freshMirror();
      const written = mirror.write(
        'state',
        page(section, revision(9), [
          { id: 'tile_1', x: 31, y: 72, visibility: 'visible', terrain: 'Grassland' },
        ])
      );
      expect(written.map((file) => path.relative(mirror.dir, file))[0]).toBe(
        path.join('state', 'map.txt')
      );
    }
  });

  test('a city_citizens page routes to U08’s yields writer', () => {
    const mirror = freshMirror();
    const written = mirror.write(
      'state',
      page('city_citizens', revision(9), [
        {
          kind: 'tile',
          tile_id: `tile_${'1'.repeat(32)}`,
          city_id: CITY_A,
          worked: true,
          yields: { food: 2, shields: 1, trade: 0 },
        },
      ]),
      { [`tile_${'1'.repeat(32)}`]: 'T(3,4)', [CITY_A]: 'c1' }
    );
    expect(written.map((file) => path.relative(mirror.dir, file))[0]).toBe(
      path.join('state', 'yields.tsv')
    );
    expect(mirror.read('state', 'yields.tsv')).toContain('T(3,4)');
  });
});

// ---------------------------------------------------------------------------
// update_from_receipt
// ---------------------------------------------------------------------------

describe('update_from_receipt', () => {
  test('reports the receipt and warns when the tables lag', () => {
    const mirror = freshMirror();
    mirror.write('state', page('overview', revision(7), [{ turn: 3 }]));
    const written = right(
      mirror.run(updateFromReceipt(mirror.dir, 'batch', receipt('applied', revision(9))))
    );
    expect(written).toHaveLength(1);
    const text = mirror.read('state', 'delta.md');
    expect(text).toContain('## receipts');
    expect(text).toContain('applied batch batch_9999999999 at rev 9');
    expect(text).toContain('state files still show rev 7');
  });

  test('a rejected receipt reports its error', () => {
    const mirror = freshMirror();
    right(mirror.run(updateFromReceipt(mirror.dir, 'batch', receipt('rejected'))));
    const text = mirror.read('state', 'delta.md');
    expect(text).toContain('rejected');
    expect(text).toContain('illegal_action');
    expect(text).toContain('that action is not legal now');
  });

  test('an idempotent replay says so, and an investigation is spelled out', () => {
    const mirror = freshMirror();
    const observed: JsonObject = {
      ...receipt('applied', revision(9)),
      idempotent: true,
      observation: {
        city: { name: 'Paris', size: 4, production: { name: 'Barracks' } },
      },
    };
    right(mirror.run(updateFromReceipt(mirror.dir, 'batch', observed)));
    const text = mirror.read('state', 'delta.md');
    expect(text).toContain('(idempotent replay)');
    expect(text).toContain('investigated Paris sz4 building Barracks');
  });

  test('a receipt without a revision is refused', () => {
    const mirror = freshMirror();
    const message = leftMessage(
      mirror.run(updateFromReceipt(mirror.dir, 'batch', { batch_id: 'batch_1' }))
    );
    expect(message).toContain('state revision counters');
  });
});
