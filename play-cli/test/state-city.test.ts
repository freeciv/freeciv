/**
 * The city drilldowns: `city_detail`, `city_citizens` and `city_build_choices`.
 *
 * Ports `test_v2_city_detail_prints_the_numbers_it_used_to_elide`
 * (test_client.py:10321), `test_v2_city_citizens_totals_the_tiles_its_citizens_
 * work` (10451), `test_v2_buy_cost_rides_the_city_row_and_the_decision_line`
 * (10494) and `test_v2_build_choice_forfeit_appears_only_when_it_is_derivable`
 * (10551).
 *
 * The output table is the fussiest byte surface in the unit: which of
 * base/gross/waste/unhappy/net/used/surplus survives is decided per page, and
 * the whole point of the section is that the surviving columns explain where
 * the shields went.  So the tables are asserted as whole lines.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect } from 'effect';
import { renderStatePage, type StatePageValue } from 'src/render/state/page';
import { cityOutputRows } from 'src/render/state/city-outputs';
import { buildChoiceNote, CITIES_MIRROR_FILE } from 'src/render/state/build-choices';
import { cityGranaryText, cityManagementLines } from 'src/render/state/city-detail';
import { table, type AliasMap, type JsonValue } from 'src/render/primitives';
import type { JsonObject } from 'src/schema/primitives';
import type { Revision } from 'src/schema/revision';
import { PrivateFs, privateFsFor } from 'src/services/private-fs';
import { scratchWorkspace, type Scratch } from 'test/_fixtures';

const NO_FILES = privateFsFor({ root: '/nonexistent', stateRoot: '/nonexistent/.sessions' });

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

const revision = (number: number, turn: number): Revision => ({
  turn,
  revision: number,
  state_token: `state_${String(number).padStart(32, '0')}`,
});

const statePage = (
  section: string,
  items: ReadonlyArray<JsonValue>,
  at: Revision
): StatePageValue => ({
  state_revision: at,
  page: {
    section,
    items,
    total_items: items.length,
    next_cursor: null,
    cursor_expires_at: null,
  },
});

const rendered = (
  value: StatePageValue,
  aliases: AliasMap | null = null,
  sessionPath: string | null = null,
  files = NO_FILES
): ReadonlyArray<string> =>
  Effect.runSync(
    Effect.provideService(
      renderStatePage(value, { aliases, sessionPath }),
      PrivateFs,
      files
    )
  );

// ---------------------------------------------------------------------------
// The fixture, ported from `city_outputs` / `city_detail_item`
// ---------------------------------------------------------------------------

const CITY_DETAIL = `city_${'4'.repeat(32)}`;
const CITY_ONE = `city_${'1'.repeat(32)}`;
const REV = revision(56, 8);

type OutputTuple = readonly [
  base: number,
  net: number,
  surplus: number,
  usage: number,
  waste: number,
  unhappy: number,
];

const cityOutputs = (named: Readonly<Record<string, OutputTuple>>): JsonObject =>
  Object.fromEntries(
    Object.entries(named).map(([name, [base, net, surplus, usage, waste, unhappy]]) => [
      name,
      {
        citizen_base: base,
        net,
        surplus,
        usage,
        waste,
        unhappy_penalty: unhappy,
        gross: net + waste + unhappy,
      },
    ])
  );

const cityDetailItem = (overrides: JsonObject = {}): JsonObject => ({
  id: CITY_DETAIL,
  owner_player_id: `player_${'c'.repeat(32)}`,
  name: 'London',
  tile_id: `tile_${'d'.repeat(32)}`,
  x: 45,
  y: 46,
  size: 1,
  surplus: { food: 2, shields: 5, trade: 1 },
  production: {
    id: `production_${'e'.repeat(32)}`,
    kind: 'unit',
    name: 'Settlers',
    shield_stock: 25,
    shield_cost: 40,
    buy_cost: 41,
    can_buy: true,
    can_change: true,
  },
  airlift: { remaining: 0, maximum: 0 },
  trade_routes: { count: 0, capacity: 2 },
  governor_enabled: false,
  citizens: { happy: 0, content: 1, unhappy: 0, angry: 0, workers: 1, specialists: 0 },
  citizen_counts_consistent: true,
  food_storage: { stock: 14, granary_size: 20, growth_turns: 3 },
  pollution: 0,
  outputs: cityOutputs({
    food: [3, 3, 2, 1, 0, 0],
    shields: [5, 5, 5, 0, 0, 0],
    trade: [1, 1, 1, 0, 0, 0],
    gold: [0, 0, 0, 0, 0, 0],
    science: [0, 1, 1, 0, 0, 0],
  }),
  counts: {
    citizen_tiles: 19,
    specialist_types: 3,
    worklist: 0,
    build_choices: 106,
    improvements: 1,
    trade_routes: 0,
  },
  management: {
    did_sell: false,
    rally: { active: false, persistent: false, vigilant: false, order_count: 0, plan_id: null },
    governor: { enabled: false },
    options: { allow_disband: false, new_citizens: 'default', conflict: false },
  },
  ...overrides,
});

const ALIASES: AliasMap = { [CITY_DETAIL]: 'c1' };

// ---------------------------------------------------------------------------
// city_detail
// ---------------------------------------------------------------------------

describe('city_detail', () => {
  test('a calm city is the whole page, with no ellipsis anywhere in it', () => {
    const lines = rendered(statePage('city_detail', [cityDetailItem()], REV), ALIASES);
    expect(lines).toEqual([
      'rev56/t8 city_detail 1/1 complete',
      'c1 London @45,46 sz1 f+2 s+5 t+1',
      '  granary 14/20 food +2/turn grows in 3t',
      '  citizens 1: 1 content',
      '  build Settlers unit 25/40 shields +5/turn done in 3t · buy 41 gold',
      '  output   base  gross  used  surplus',
      '  food     3     3      1     +2',
      '  shields  5     5      0     +5',
      '  trade    1     1      0     +1',
      '  science  0     1      0     +1',
      '  19 tile yields: just state --section city_citizens --actor_id c1',
      '  106 build choices: just state --section city_build_choices --actor_id c1',
      '  1 improvements: just state --section city_improvements --actor_id c1',
    ]);
    const body = lines.join('\n');
    expect(body).not.toContain('…');
    expect(body).not.toContain('outputs.food');
    // An output this city neither makes nor spends is not a row at all.
    expect(lines.some((line) => line.trim().startsWith('gold'))).toBe(false);
  });

  test('a besieged city prints the waste and unhappy terms that explain the loss', () => {
    const lines = rendered(
      statePage(
        'city_detail',
        [
          cityDetailItem({
            name: 'York',
            size: 7,
            surplus: { food: -2, shields: 1, trade: 4 },
            pollution: 3,
            production: {
              id: `production_${'f'.repeat(32)}`,
              kind: 'improvement',
              name: 'City Walls',
              shield_stock: 0,
              shield_cost: 60,
              buy_cost: 240,
              can_buy: false,
              can_change: false,
            },
            citizens: { happy: 1, content: 2, unhappy: 3, angry: 1, workers: 7, specialists: 0 },
            food_storage: { stock: 9, granary_size: 60, growth_turns: -4 },
            outputs: cityOutputs({
              food: [12, 12, -2, 14, 0, 0],
              shields: [9, 4, 1, 3, 2, 3],
              trade: [11, 8, 4, 4, 3, 0],
            }),
            management: {
              did_sell: true,
              rally: {
                active: true,
                persistent: true,
                vigilant: false,
                order_count: 3,
                plan_id: `rally_${'a'.repeat(32)}`,
              },
              governor: { enabled: true },
              options: { allow_disband: false, new_citizens: 'gold', conflict: false },
            },
          }),
        ],
        REV
      ),
      ALIASES
    );
    expect(lines).toEqual([
      'rev56/t8 city_detail 1/1 complete',
      'c1 York @45,46 sz7 f-2 s+1 t+4 !pollution 3',
      '  granary 9/60 food -2/turn !starving, famine in 4t',
      '  citizens 7: 1 happy, 2 content, 3 unhappy, 1 angry !disorder',
      '  build City Walls improvement 0/60 shields +1/turn done in 60t · !cannot buy this turn',
      '  !locked: this city already bought this turn',
      '  !sold here this turn: no second sale until next turn !governor on: tiles are assigned for you',
      '  !rally 3 orders persistent !new_citizens=gold',
      '  output   base  waste  unhappy  net  used  surplus',
      '  food     12    0      0        12   14    -2',
      '  shields  9     2      3        4    3     +1',
      '  trade    11    3      0        8    4     +4',
      '  19 tile yields: just state --section city_citizens --actor_id c1',
      '  106 build choices: just state --section city_build_choices --actor_id c1',
      '  1 improvements: just state --section city_improvements --actor_id c1',
    ]);
    for (const line of lines) expect(line.length).toBeLessThanOrEqual(120);
  });

  test('without an alias cache the drill-down types the opaque ID it can resolve', () => {
    const lines = rendered(statePage('city_detail', [cityDetailItem()], REV));
    expect(lines).toContain(
      `  19 tile yields: just state --section city_citizens --actor_id ${CITY_DETAIL}`
    );
  });

  test('a granary with no growth at all says so, and a full one says why', () => {
    const text = (storage: JsonValue, food: number): string =>
      Effect.runSync(cityGranaryText({ food_storage: storage }, { food }));
    expect(text({ stock: 5, granary_size: 20, growth_turns: null }, 0)).toBe(
      'granary 5/20 food +0/turn no growth'
    );
    expect(text({ stock: 20, granary_size: 20, growth_turns: 0 }, 3)).toBe(
      'granary 20/20 food +3/turn !full, growth blocked'
    );
  });

  test('management prints only what is set away from its default', () => {
    expect(cityManagementLines({ management: {} })).toEqual([]);
    expect(
      cityManagementLines({
        management: { options: { allow_disband: true, new_citizens: 'default', conflict: true } },
      })
    ).toEqual(['!allow_disband !options_conflict']);
  });
});

// ---------------------------------------------------------------------------
// the output chain
// ---------------------------------------------------------------------------

describe('cityOutputRows', () => {
  const rows = (outputs: JsonValue): ReadonlyArray<string> =>
    table(Effect.runSync(cityOutputRows(outputs)));

  test('a chain step that merely repeats the step before it is dropped', () => {
    expect(rows(cityOutputs({ food: [3, 3, 2, 1, 0, 0] }))).toEqual([
      'output  base  used  surplus',
      'food    3     1     +2',
    ]);
  });

  test('a page of nothing but zeroes renders no table at all', () => {
    expect(Effect.runSync(cityOutputRows(cityOutputs({ gold: [0, 0, 0, 0, 0, 0] })))).toEqual([]);
    expect(Effect.runSync(cityOutputRows({}))).toEqual([]);
    expect(Effect.runSync(cityOutputRows(null))).toEqual([]);
  });

  test('an output the chain does not name sorts after the ones it does', () => {
    const lines = rows(
      cityOutputs({ pollution: [1, 1, 1, 0, 0, 0], food: [2, 2, 2, 0, 0, 0] })
    );
    expect(lines[1]).toContain('food');
    expect(lines[2]).toContain('pollution');
  });

  test('a non-integer metric is refused rather than rendered as a blank', () => {
    const outcome = Effect.runSyncExit(cityOutputRows({ food: { citizen_base: 'three' } }));
    expect(outcome._tag).toBe('Failure');
  });
});

// ---------------------------------------------------------------------------
// city_citizens
// ---------------------------------------------------------------------------

describe('city_citizens', () => {
  test('the worked rows are totalled before they are listed', () => {
    const items: ReadonlyArray<JsonObject> = [
      {
        city_id: CITY_DETAIL,
        kind: 'tile',
        tile_id: `tile_${'1'.repeat(32)}`,
        worked: true,
        free_worked: true,
        can_work: true,
        yields: { food: 2, shields: 1, trade: 1 },
      },
      {
        city_id: CITY_DETAIL,
        kind: 'tile',
        tile_id: `tile_${'2'.repeat(32)}`,
        worked: true,
        free_worked: false,
        can_work: true,
        yields: { food: 2, shields: 2, trade: 0 },
      },
      {
        city_id: CITY_DETAIL,
        kind: 'tile',
        tile_id: `tile_${'3'.repeat(32)}`,
        worked: false,
        free_worked: false,
        can_work: true,
        yields: { food: 1, shields: 0, trade: 3 },
      },
      {
        city_id: CITY_DETAIL,
        kind: 'specialist',
        id: `specialist_${'9'.repeat(32)}`,
        name: 'Entertainer',
        count: 1,
        counts_toward_population: true,
        can_use: true,
        is_default: true,
        yields: { luxury: 2 },
      },
    ];
    const lines = rendered(statePage('city_citizens', items, revision(10, 1)), ALIASES);
    // The total the tile swap is judged against — and `city_detail`'s `base`.
    // Fifteen columns is past the table budget, so the rows go long-form; the
    // page constant is hoisted, so the 37-char city ID is not on every one.
    expect(lines).toEqual([
      'rev10/t1 city_citizens 4/4 complete',
      'worked 2 of 4 rows on this page: f4 s3 t1',
      'specialists: 1 Entertainer',
      `constants: city_id=${CITY_DETAIL}`,
      `1  kind=tile tile_id=tile_${'1'.repeat(32)} worked=yes free_worked=yes can_work=yes ` +
        'yields.food=2 yields.shields=1 yields.trade=1',
      `2  kind=tile tile_id=tile_${'2'.repeat(32)} worked=yes free_worked=no can_work=yes ` +
        'yields.food=2 yields.shields=2 yields.trade=0',
      `3  kind=tile tile_id=tile_${'3'.repeat(32)} worked=no free_worked=no can_work=yes ` +
        'yields.food=1 yields.shields=0 yields.trade=3',
      `4  kind=specialist id=specialist_${'9'.repeat(32)} name=Entertainer count=1 ` +
        'counts_toward_population=yes can_use=yes is_default=yes yields.luxury=2',
    ]);
  });

  test('a worked tile whose yields are not integers is refused', () => {
    const outcome = Effect.runSyncExit(
      Effect.provideService(
        renderStatePage(
          statePage(
            'city_citizens',
            [{ city_id: CITY_DETAIL, kind: 'tile', worked: true, yields: { food: 'two' } }],
            revision(10, 1)
          )
        ),
        PrivateFs,
        NO_FILES
      )
    );
    expect(outcome._tag).toBe('Failure');
  });
});

// ---------------------------------------------------------------------------
// city_build_choices
// ---------------------------------------------------------------------------

const CHOICES: ReadonlyArray<JsonObject> = [
  {
    city_id: CITY_ONE,
    id: `production_${'1'.repeat(32)}`,
    kind: 'improvement',
    name: 'City Walls',
    can_queue: true,
    can_build_now: true,
    cost: { shields: 60, shield_stock_after_change: 12, turns: 15, turns_with_stock: 12 },
    upkeep: { gold: 1, food: 0, shields: 0 },
    happy_cost: null,
    unit: null,
    building: { genus: 'Improvement' },
  },
  {
    city_id: CITY_ONE,
    id: `production_${'2'.repeat(32)}`,
    kind: 'unit',
    name: 'Musketeers',
    can_queue: true,
    can_build_now: false,
    cost: { shields: 30, shield_stock_after_change: 25, turns: 6, turns_with_stock: 1 },
    upkeep: { gold: 0, food: 0, shields: 1 },
    happy_cost: 1,
    unit: { attack: 3 },
    building: null,
  },
];

/** Write the `cities` mirror table this seat would have written at `at`. */
const stageCitiesMirror = (scratch: Scratch, sessionPath: string, at: Revision | null): void => {
  const directory = path.join(path.dirname(sessionPath), path.basename(sessionPath, '.json'));
  fs.mkdirSync(path.join(directory, 'state'), { recursive: true, mode: 0o700 });
  const stamp = at === null ? '# rev - turn -' : `# rev ${at.revision} turn ${at.turn}`;
  fs.writeFileSync(
    path.join(directory, ...CITIES_MIRROR_FILE),
    `${stamp}\n# cities 1/1 complete\n` +
      'alias\tcity\tpos\tsize\tbuilding\tshields\tsurplus\tbuy\n' +
      'c1   \tLondon\t31,72\t3\tMusketeers\t25/30\tf1 s2 t3\t12\n',
    { mode: 0o600 }
  );
  void scratch;
};

describe('city_build_choices', () => {
  test('the forfeit is stated only when the mirror stands at this page revision', () => {
    const scratch = scratchWorkspace();
    scratches.push(scratch);
    const sessionPath = path.join(scratch.workspace.stateRoot, 'session.json');
    const at = revision(19, 4);
    stageCitiesMirror(scratch, sessionPath, at);
    const lines = rendered(
      statePage('city_build_choices', CHOICES, at),
      { [CITY_ONE]: 'c1' },
      sessionPath,
      scratch.files
    );
    expect(lines).toEqual([
      'rev19/t4 city_build_choices 2/2 complete',
      'stock 25 shields; a switch to another production class forfeits half of it',
      '#  name        kind         shields  turns  note',
      '1  City Walls  improvement  60       12     !forfeits 13 of 25 shields !upkeep gold 1',
      '2  Musketeers  unit         30       1      !worklist only !upkeep shields 1 !unhappy 1',
    ]);
    for (const line of lines) expect(line.length).toBeLessThanOrEqual(120);
  });

  test('one revision on, the mirror stock is a memory and no forfeit is claimed', () => {
    const scratch = scratchWorkspace();
    scratches.push(scratch);
    const sessionPath = path.join(scratch.workspace.stateRoot, 'session.json');
    stageCitiesMirror(scratch, sessionPath, revision(19, 4));
    const lines = rendered(
      statePage('city_build_choices', CHOICES, revision(21, 4)),
      { [CITY_ONE]: 'c1' },
      sessionPath,
      scratch.files
    );
    const body = lines.join('\n');
    expect(body).not.toContain('forfeits');
    expect(body).not.toContain('stock 25 shields');
    expect(body).toContain('keep 12');
  });

  test('no mirror at all is the same answer as a stale one: keep, never forfeit', () => {
    const lines = rendered(statePage('city_build_choices', CHOICES, revision(19, 4)), {
      [CITY_ONE]: 'c1',
    });
    expect(lines).toEqual([
      'rev19/t4 city_build_choices 2/2 complete',
      '#  name        kind         shields  turns  note',
      '1  City Walls  improvement  60       12     keep 12 !upkeep gold 1',
      '2  Musketeers  unit         30       1      keep 25 !worklist only !upkeep shields 1 !unhappy 1',
    ]);
  });

  test('a switch inside the same production class costs nothing and says nothing', () => {
    expect(buildChoiceNote({}, { shield_stock_after_change: 25 }, 25)).toBe('');
    expect(buildChoiceNote({}, { shield_stock_after_change: 12 }, 25)).toBe(
      '!forfeits 13 of 25 shields'
    );
  });

  test('a choice with no cost object is refused rather than rendered blank', () => {
    const outcome = Effect.runSyncExit(
      Effect.provideService(
        renderStatePage(
          statePage(
            'city_build_choices',
            [{ id: 'p1', city_id: CITY_ONE, kind: 'unit', name: 'Warriors', cost: 3 }],
            revision(19, 4)
          )
        ),
        PrivateFs,
        NO_FILES
      )
    );
    expect(outcome._tag).toBe('Failure');
  });
});
