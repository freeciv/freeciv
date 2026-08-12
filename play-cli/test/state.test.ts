/**
 * `play state`: the query it builds, the refusals it prints, and the order in
 * which one page is ingested and then rendered.
 *
 * Ports `test_v2_state_scoped_query_construction_is_strict`
 * (test_client.py:3269), `test_v2_state_refusals_spell_every_flag_the_way_just_
 * accepts` (10857) and the `command_state` half of
 * `test_v2_buy_cost_rides_the_city_row_and_the_decision_line` (10494) and
 * `test_v2_diplomacy_is_mirrored_and_reachable_by_relation_alias` (10700).
 *
 * The refusal texts are asserted character for character on purpose: every flag
 * they name is spelled the way `just` accepts it (underscored), because a
 * remedy naming `--actor-id` fails as printed at the moment an agent is already
 * lost.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer, Option } from 'effect';
import { runState, type StateOptions } from 'src/commands/state.cmd';
import { stateLimit, stateQuery, type StateQueryArgs } from 'src/services/state-query';
import type { PlayerError } from 'src/errors';
import { V2_SECTIONS } from 'src/constants';
import type { DualSpelling } from 'src/options';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import { httpFor } from 'src/services/http';
import { v2StateSchema } from 'src/services/aliases';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, sessionStoreFor } from 'src/services/session-store';
import { V2Client, v2ClientFor } from 'src/services/v2-client';
import {
  FIXTURE_GAME_ID,
  errorPayload,
  recordingFetch,
  scratchWorkspace,
  sessionFile,
  type RecordedRequest,
  type Scratch,
} from 'test/_fixtures';

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

// ---------------------------------------------------------------------------
// _state_query
// ---------------------------------------------------------------------------

const CITY_ID = `city_${'a'.repeat(32)}`;
const UNIT_ID = `unit_${'e'.repeat(32)}`;
const TILE_ID = `tile_${'b'.repeat(32)}`;
const RELATION_ID = `relation_${'d'.repeat(32)}`;
const CURSOR = `cursor_${'c'.repeat(32)}`;

const args = (overrides: Partial<StateQueryArgs> = {}): StateQueryArgs => ({
  section: '',
  actorId: '',
  relationId: '',
  centerId: '',
  radius: null,
  limit: null,
  cursor: '',
  ...overrides,
});

const query = (overrides: Partial<StateQueryArgs> = {}): string =>
  Effect.runSync(stateQuery(args(overrides)));

const queryRefusal = (overrides: Partial<StateQueryArgs>): PlayerError => {
  const outcome = Effect.runSync(Effect.either(stateQuery(args(overrides))));
  if (Either.isRight(outcome)) throw new Error(`expected a refusal, got ${outcome.right}`);
  return outcome.left;
};

describe('stateQuery', () => {
  test('every accepted scope shape produces the query the section wants', () => {
    expect(query({ section: 'city_citizens', actorId: CITY_ID, limit: '3' })).toBe(
      `section=city_citizens&limit=3&actor_id=${CITY_ID}`
    );
    expect(query({ section: 'tile_window', centerId: TILE_ID, radius: 4 })).toBe(
      `section=tile_window&limit=16&center_id=${TILE_ID}&radius=4`
    );
    expect(query({ section: 'unit_route', actorId: UNIT_ID, limit: '5' })).toBe(
      `section=unit_route&limit=5&actor_id=${UNIT_ID}`
    );
    expect(query({ section: 'pregame_nations' })).toBe('section=pregame_nations&limit=16');
    expect(query({ section: 'pregame_styles', limit: '4' })).toBe(
      'section=pregame_styles&limit=4'
    );
    expect(query({ section: 'diplomacy_clauses', relationId: RELATION_ID, limit: '2' })).toBe(
      `section=diplomacy_clauses&limit=2&relation_id=${RELATION_ID}`
    );
  });

  test('a cursor is the whole request, or it is a refusal', () => {
    expect(query({ cursor: CURSOR })).toBe(`cursor=${CURSOR}`);
    expect(queryRefusal({ cursor: CURSOR, actorId: CITY_ID }).message).toBe(
      'state cursor must be the only page option'
    );
    expect(queryRefusal({ cursor: 'cursor_short' }).message).toBe(
      'state cursor must be the only page option'
    );
  });

  test('no section at all is the whole-state request, and takes no scope', () => {
    expect(query({})).toBe('');
    expect(queryRefusal({ actorId: CITY_ID }).message).toBe(
      'state scope options require --section'
    );
    expect(queryRefusal({ limit: '3' }).message).toBe('state --limit requires --section');
  });

  test.each([
    ['a city section with no actor', { section: 'city_detail' }],
    ['a city section given a relation', { section: 'city_detail', actorId: CITY_ID, relationId: RELATION_ID }],
    ['a plain section given an actor', { section: 'cities', actorId: CITY_ID }],
    ['a plain section given a relation', { section: 'cities', relationId: RELATION_ID }],
    ['unit_route with no actor', { section: 'unit_route' }],
    ['unit_route given a city', { section: 'unit_route', actorId: CITY_ID }],
    ['tile_window with no radius', { section: 'tile_window', centerId: TILE_ID }],
    ['tile_window past radius 8', { section: 'tile_window', centerId: TILE_ID, radius: 9 }],
    [
      'tile_window given an actor too',
      { section: 'tile_window', actorId: CITY_ID, centerId: TILE_ID, radius: 1 },
    ],
    ['diplomacy_clauses with no relation', { section: 'diplomacy_clauses' }],
    ['diplomacy_clauses given junk', { section: 'diplomacy_clauses', relationId: 'not/opaque' }],
    [
      'diplomacy_clauses given a player',
      { section: 'diplomacy_clauses', relationId: `player_${'d'.repeat(32)}` },
    ],
  ] as ReadonlyArray<readonly [string, Partial<StateQueryArgs>]>)(
    'refuses %s',
    (_label, overrides) => {
      expect(queryRefusal(overrides)).toBeDefined();
    }
  );

  test('radius 0 through 8 is inclusive at both ends', () => {
    expect(query({ section: 'tile_window', centerId: TILE_ID, radius: 0 })).toContain('radius=0');
    expect(query({ section: 'tile_window', centerId: TILE_ID, radius: 8 })).toContain('radius=8');
  });

  test('an unknown section lists the real ones and points economy at overview', () => {
    const message = queryRefusal({ section: 'economy' }).message;
    expect(message).toBe(
      "state section 'economy' is not supported; valid sections: " +
        `${[...V2_SECTIONS].sort().join(', ')}. ` +
        'Economy and current government are in overview; ' +
        'government choices are in governments'
    );
  });

  test('every refusal spells its flags the way `just` accepts them', () => {
    const cases: ReadonlyArray<readonly [Partial<StateQueryArgs>, ReadonlyArray<string>]> = [
      [{ section: 'city_detail' }, ['--actor_id']],
      [{ section: 'unit_route' }, ['--actor_id']],
      [{ section: 'tile_window', centerId: `tile_${'a'.repeat(32)}` }, ['--center_id', '--radius']],
      [{ section: 'diplomacy_clauses' }, ['--relation_id']],
    ];
    for (const [overrides, expected] of cases) {
      const message = queryRefusal(overrides).message;
      for (const flag of expected) {
        expect(message).toContain(flag);
        // The hyphenated form must be absent — but only where there is one.
        if (flag.includes('_')) expect(message).not.toContain(flag.replace(/_/g, '-'));
      }
    }
  });

  test('the refusal texts themselves are the contract', () => {
    expect(queryRefusal({ section: 'city_detail' }).message).toBe(
      'city state sections require exactly one opaque --actor_id'
    );
    expect(queryRefusal({ section: 'unit_route' }).message).toBe(
      'unit_route requires exactly one opaque unit --actor_id'
    );
    expect(queryRefusal({ section: 'tile_window' }).message).toBe(
      'tile_window requires --center_id and --radius from 0 through 8'
    );
    expect(queryRefusal({ section: 'diplomacy_clauses' }).message).toBe(
      'diplomacy_clauses requires exactly one opaque --relation_id from a ' +
        '`just state --section diplomacy` row'
    );
  });
});

describe('stateLimit', () => {
  test('1 through 16 canonical, and nothing else', () => {
    expect(Effect.runSync(stateLimit(null))).toBe(16);
    expect(Effect.runSync(stateLimit('1'))).toBe(1);
    expect(Effect.runSync(stateLimit('16'))).toBe(16);
    for (const bad of ['0', '17', '01', '+3', ' 3', '3.0', '']) {
      const outcome = Effect.runSync(Effect.either(stateLimit(bad)));
      expect(Either.isLeft(outcome)).toBe(true);
      if (Either.isLeft(outcome)) {
        expect(outcome.left.message).toBe(
          'limit must be a canonical integer from 1 through 16'
        );
      }
    }
  });
});

// ---------------------------------------------------------------------------
// command_state
// ---------------------------------------------------------------------------

const REVISION = { turn: 4, revision: 19, state_token: `state_${'0'.repeat(29)}019` };

const sectionPage = (section: string, items: ReadonlyArray<JsonValue>): JsonObject => ({
  schema_version: 2,
  control_protocol: 'full-control-v2',
  game_id: FIXTURE_GAME_ID,
  agent_id: 'agent_0123456789abcdef',
  state_revision: REVISION,
  page: {
    section,
    items,
    total_items: items.length,
    next_cursor: null,
    cursor_expires_at: null,
  },
});

const noDual: DualSpelling<string> = { dashed: Option.none(), underscored: Option.none() };

const stateOptions = (overrides: Partial<StateOptions> = {}): StateOptions => ({
  session: '',
  section: '',
  actorId: noDual,
  relationId: noDual,
  centerId: noDual,
  radius: Option.none(),
  limit: '',
  cursor: '',
  json: false,
  ...overrides,
});

interface Fixture {
  readonly layer: Layer.Layer<SessionStore | V2Client | PrivateFs>;
  readonly requests: ReadonlyArray<RecordedRequest>;
  /** The mirror directory `_mirror_page` projects this seat's pages into. */
  readonly mirrorDir: string;
}

type Plan =
  | ReadonlyMap<string, { status?: number; body: unknown }>
  | ReadonlyArray<{ status?: number; body: unknown }>;

const fixture = (routes: Plan): Fixture => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const target = path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID, 'seat.json');
  Effect.runSync(scratch.files.writeJson(target, sessionFile()));
  const store = sessionStoreFor(scratch.workspace, scratch.files, v2StateSchema, {});
  const recorder = recordingFetch(routes);
  const requests = recorder.requests;
  const client = v2ClientFor(httpFor(recorder.fetch), () => Effect.void);
  return {
    layer: Layer.mergeAll(
      Layer.succeed(SessionStore, store),
      Layer.succeed(V2Client, client),
      Layer.succeed(PrivateFs, scratch.files)
    ),
    requests,
    mirrorDir: path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID, 'seat'),
  };
};

const capture = async <E>(
  effect: Effect.Effect<void, E, SessionStore | V2Client | PrivateFs>,
  active: Fixture
): Promise<ReadonlyArray<string>> => {
  const out: string[] = [];
  const original = console.log;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
  try {
    await Effect.runPromise(Effect.provide(effect, active.layer));
    return out;
  } finally {
    console.log = original;
  }
};

const CITY = `city_${'1'.repeat(32)}`;

/** One city row, rich enough for the mirror to record a shield stock. */
const LONDON: JsonObject = {
  id: CITY,
  name: 'London',
  x: 31,
  y: 72,
  size: 3,
  surplus: { food: 1, shields: 2, trade: 3 },
  production: {
    kind: 'unit',
    name: 'Musketeers',
    shield_stock: 25,
    shield_cost: 30,
    buy_cost: 12,
    can_buy: true,
  },
};

/** The `city_build_choices` fixture of `test_v2_build_choice_forfeit_…`. */
const BUILD_CHOICES: ReadonlyArray<JsonObject> = [
  {
    city_id: CITY,
    id: `production_${'1'.repeat(32)}`,
    kind: 'improvement',
    name: 'City Walls',
    can_build_now: true,
    cost: { shields: 60, shield_stock_after_change: 12, turns: 15, turns_with_stock: 12 },
    upkeep: { gold: 1, food: 0, shields: 0 },
    happy_cost: null,
  },
  {
    city_id: CITY,
    id: `production_${'2'.repeat(32)}`,
    kind: 'unit',
    name: 'Musketeers',
    can_build_now: false,
    cost: { shields: 30, shield_stock_after_change: 25, turns: 6, turns_with_stock: 1 },
    upkeep: { gold: 0, food: 0, shields: 1 },
    happy_cost: 1,
  },
];

describe('play state', () => {
  test('the section and limit ride the query string, and the page renders', async () => {
    const active = fixture(
      new Map([
        [
          '/state',
          {
            body: sectionPage('cities', [
              {
                id: CITY,
                name: 'London',
                x: 31,
                y: 72,
                size: 3,
                surplus: { food: 1, shields: 2, trade: 3 },
                production: {
                  kind: 'unit',
                  name: 'Musketeers',
                  shield_stock: 30,
                  shield_cost: 30,
                  buy_cost: 4,
                  can_buy: true,
                },
              },
            ]),
          },
        ],
      ])
    );
    const lines = await capture(runState(stateOptions({ section: 'cities' })), active);
    expect(active.requests[0]?.url).toContain('/state?section=cities&limit=16');
    expect(active.requests[0]?.method).toBe('GET');
    // The page was ingested before it was rendered: `c1` is the alias this very
    // response taught, and the opaque ID column is gone with it.
    expect(lines).toEqual([
      'rev19/t4 cities 1/1 complete',
      'c1  London  @31,72 sz3 Musketeers 30/30 f+1 s+2 t+3 buy=4',
    ]);
  });

  test('a relation alias is learned here and named on the clause command', async () => {
    const relation = `relation_${'d'.repeat(32)}`;
    const active = fixture(
      new Map([
        [
          '/state',
          {
            body: sectionPage('diplomacy', [
              {
                relation_id: relation,
                player_id: `player_${'b'.repeat(32)}`,
                player_name: 'Isabella',
                nation: 'Spanish',
                alive: true,
                state: 'cease-fire',
                has_embassy: true,
                treaty_turns_left: 4,
                meeting: {
                  meeting_id: `meeting_${'e'.repeat(32)}`,
                  self_accepted: false,
                  other_accepted: true,
                  clause_count: 2,
                  clauses_token: 'treaty_x',
                },
              },
            ]),
          },
        ],
      ])
    );
    const lines = await capture(runState(stateOptions({ section: 'diplomacy' })), active);
    expect(lines).toEqual([
      'rev19/t4 diplomacy 1/1 complete',
      'r1  Isabella  (Spanish) cease-fire embassy 4t left · !meeting open 2 clauses ' +
        'accepted by them, awaiting you',
      'clauses: just state --section diplomacy_clauses --relation_id r1',
    ]);
    expect(lines.join('\n')).not.toContain(relation);
    for (const line of lines) expect(line.length).toBeLessThanOrEqual(120);
  });

  test('--json prints the validated envelope, not the rendered text', async () => {
    const active = fixture(
      new Map([['/state', { body: sectionPage('chat', [{ id: 'chat_1', sender: 'Ada' }]) }]])
    );
    const lines = await capture(
      runState(stateOptions({ section: 'chat', json: true })),
      active
    );
    expect(lines).toHaveLength(1);
    const parsed = JSON.parse(lines[0] as string) as JsonObject;
    expect((parsed['page'] as JsonObject)['section']).toBe('chat');
    expect(parsed['state_revision']).toEqual(REVISION);
  });

  test('a --cursor request carries only the cursor', async () => {
    const active = fixture(
      new Map([['/state', { body: sectionPage('units', []) }]])
    );
    await capture(runState(stateOptions({ cursor: CURSOR })), active);
    expect(active.requests[0]?.url).toContain(`/state?cursor=${CURSOR}`);
    expect(active.requests[0]?.url).not.toContain('section=');
  });

  test('a refused query never opens a socket', async () => {
    const active = fixture(new Map([['/state', { body: sectionPage('units', []) }]]));
    const outcome = await Effect.runPromise(
      Effect.either(
        Effect.provide(runState(stateOptions({ section: 'city_detail' })), active.layer)
      )
    );
    expect(Either.isLeft(outcome)).toBe(true);
    expect(active.requests).toHaveLength(0);
  });

  test('an expired cursor is re-raised, and the staged catalog it named is dropped', async () => {
    const active = fixture(
      new Map([
        [
          '/state',
          {
            status: 409,
            body: errorPayload({
              error: {
                code: 'cursor_expired',
                message: 'that cursor has expired',
                retryable: false,
                details: {},
              },
            }),
          },
        ],
      ])
    );
    const outcome = await Effect.runPromise(
      Effect.either(Effect.provide(runState(stateOptions({ cursor: CURSOR })), active.layer))
    );
    expect(Either.isLeft(outcome)).toBe(true);
    if (Either.isLeft(outcome)) {
      expect(outcome.left._tag).toBe('V2ResponseError');
    }
  });

  test('a non-cursor refusal is raised untouched', async () => {
    const active = fixture(
      new Map([['/state', { status: 400, body: errorPayload() }]])
    );
    const outcome = await Effect.runPromise(
      Effect.either(Effect.provide(runState(stateOptions({ section: 'units' })), active.layer))
    );
    expect(Either.isLeft(outcome)).toBe(true);
    if (Either.isLeft(outcome)) {
      expect(outcome.left._tag).toBe('V2ResponseError');
    }
  });

  test('an entity alias is expanded before the query is built', async () => {
    const active = fixture([
      { body: sectionPage('cities', [{ id: CITY, name: 'London', x: 1, y: 1, size: 1 }]) },
      { body: sectionPage('city_detail', []) },
    ]);
    // The first page teaches `c1`; the second is asked for by that name.
    await capture(runState(stateOptions({ section: 'cities' })), active);
    await capture(
      runState(
        stateOptions({
          section: 'city_detail',
          actorId: { dashed: Option.some('c1'), underscored: Option.none() },
        })
      ),
      active
    );
    expect(active.requests[1]?.url).toContain(`actor_id=${CITY}`);
    expect(active.requests[1]?.url).not.toContain('actor_id=c1');
  });

  test('an alias that names nothing is refused before the request', async () => {
    const active = fixture(new Map([['/state', { body: sectionPage('units', []) }]]));
    const outcome = await Effect.runPromise(
      Effect.either(
        Effect.provide(
          runState(
            stateOptions({
              section: 'city_detail',
              actorId: { dashed: Option.some('c9'), underscored: Option.none() },
            })
          ),
          active.layer
        )
      )
    );
    expect(Either.isLeft(outcome)).toBe(true);
    expect(active.requests).toHaveLength(0);
  });

  test('the fetched page is projected into the mirror, not only rendered', async () => {
    const active = fixture(
      new Map([['/state', { body: sectionPage('cities', [LONDON]) }]])
    );
    await capture(runState(stateOptions({ section: 'cities' })), active);
    const written = fs.readFileSync(
      path.join(active.mirrorDir, 'state', 'cities.tsv'),
      'utf8'
    );
    // Stamped at this page's revision, keyed by the alias this page taught.
    expect(written.split('\n').slice(0, 4)).toEqual([
      '# rev 19 turn 4',
      '# cities 1/1 complete',
      'alias\tcity  \tpos  \tsize\tbuilding  \tshields\tsurplus \tbuy',
      'c1   \tLondon\t31,72\t3   \tMusketeers\t25/30  \tf1 s2 t3\t12',
    ]);
  });

  test('the forfeit `state` alone can derive: cities first, then build choices', async () => {
    const active = fixture([
      { body: sectionPage('cities', [LONDON]) },
      { body: sectionPage('city_build_choices', BUILD_CHOICES) },
    ]);
    await capture(runState(stateOptions({ section: 'cities' })), active);
    const lines = await capture(
      runState(
        stateOptions({
          section: 'city_build_choices',
          actorId: { dashed: Option.some(CITY), underscored: Option.none() },
        })
      ),
      active
    );
    // Nothing was staged by hand: the stock is the one the previous `state`
    // call projected, at the revision both pages were served at.
    expect(lines).toEqual([
      'rev19/t4 city_build_choices 2/2 complete',
      'stock 25 shields; a switch to another production class forfeits half of it',
      '#  name        kind         shields  turns  note',
      '1  City Walls  improvement  60       12     !forfeits 13 of 25 shields !upkeep gold 1',
      '2  Musketeers  unit         30       1      !worklist only !upkeep shields 1 !unhappy 1',
    ]);
  });

  test('a city_citizens page prices the tiles the yields overlay reads back', async () => {
    const tile = `tile_${'7'.repeat(32)}`;
    const active = fixture(
      new Map([
        [
          '/state',
          {
            body: sectionPage('city_citizens', [
              {
                city_id: CITY,
                kind: 'tile',
                tile_id: tile,
                worked: true,
                yields: { food: 2, shields: 1, trade: 0 },
              },
              { city_id: CITY, kind: 'specialist', specialist: 'Elvis' },
            ]),
          },
        ],
      ])
    );
    await capture(
      runState(
        stateOptions({
          section: 'city_citizens',
          actorId: { dashed: Option.some(CITY), underscored: Option.none() },
        })
      ),
      active
    );
    const written = fs.readFileSync(
      path.join(active.mirrorDir, 'state', 'yields.tsv'),
      'utf8'
    );
    // The specialist prices no tile, so it is silently absent — and the count
    // is over the rows the file holds, not the items the page carried.  The
    // city keeps its opaque handle: a citizens page teaches no `cN`.
    expect(written.split('\n').slice(0, 4)).toEqual([
      '# rev 19 turn 4',
      '# yields 1/1 complete',
      'tile                                 \tfood\tshields\ttrade\tworked\tcity',
      `${tile}\t2   \t1      \t0    \tyes   \t${CITY}`,
    ]);
  });

  test('--json still projects the page it printed', async () => {
    const active = fixture(
      new Map([['/state', { body: sectionPage('cities', [LONDON]) }]])
    );
    await capture(runState(stateOptions({ section: 'cities', json: true })), active);
    expect(fs.existsSync(path.join(active.mirrorDir, 'state', 'cities.tsv'))).toBe(true);
  });

  test('both spellings of one flag at once is a refusal, not a coin toss', async () => {
    const active = fixture(new Map([['/state', { body: sectionPage('units', []) }]]));
    const outcome = await Effect.runPromise(
      Effect.either(
        Effect.provide(
          runState(
            stateOptions({
              section: 'city_detail',
              actorId: { dashed: Option.some(CITY), underscored: Option.some(CITY) },
            })
          ),
          active.layer
        )
      )
    );
    expect(Either.isLeft(outcome)).toBe(true);
    if (Either.isLeft(outcome)) {
      expect(outcome.left.message).toBe('pass only one of --actor-id or --actor_id');
    }
    expect(active.requests).toHaveLength(0);
  });
});
