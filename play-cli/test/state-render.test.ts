/**
 * The live-state section renderers.
 *
 * Ports `test_v2_state_sections_render_aligned_tables_and_fog`
 * (test_client.py:1238), `test_v2_renderers_fail_closed_on_contract_drift`
 * (1341), `test_v2_unit_rows_carry_a_route_summary_everywhere` (7400) and
 * `test_v2_a_changeable_government_names_the_catalog_that_holds_it` (10905).
 *
 * Every assertion here is on whole lines, not substrings: the column alignment
 * *is* the contract — an agent greps `^u1` and reads the ID column by offset,
 * so a one-space drift is a silent behavioural change.
 */
import { describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import type { PlayerError } from 'src/errors';
import { renderStatePage, renderSectionItems, type StatePageValue } from 'src/render/state/page';
import { routeSummary, unitRow } from 'src/render/state/units';
import { terrainCode, terrainCodes } from 'src/render/state/tiles';
import { scoreText } from 'src/render/state/overview';
import { meetingSummary } from 'src/render/state/diplomacy';
import { flattenItem, renderGenericItems } from 'src/render/state/generic';
import { governmentScopeLines } from 'src/render/state/government';
import type { AliasMap, JsonValue } from 'src/render/primitives';
import type { JsonObject } from 'src/schema/primitives';
import type { Revision } from 'src/schema/revision';
import { privateFsFor, PrivateFs } from 'src/services/private-fs';
import type { V2ClientState } from 'src/services/session-store';

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

/** `_render_state_page` never touches the mirror unless a session path is passed. */
const NO_FILES = privateFsFor({ root: '/nonexistent', stateRoot: '/nonexistent/.sessions' });

const revision = (number = 7, turn = 3): Revision => ({
  turn,
  revision: number,
  state_token: `state_${String(number).padStart(32, '0')}`,
});

const statePage = (
  section: string,
  items: ReadonlyArray<JsonValue>,
  at: Revision = revision(9)
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
  state: V2ClientState | null = null
): ReadonlyArray<string> =>
  Effect.runSync(
    Effect.provideService(
      renderStatePage(value, { aliases, sessionPath: null, state }),
      PrivateFs,
      NO_FILES
    )
  );

const refusal = (value: StatePageValue, aliases: AliasMap | null = null): PlayerError => {
  const outcome = Effect.runSync(
    Effect.either(
      Effect.provideService(renderStatePage(value, { aliases }), PrivateFs, NO_FILES)
    )
  );
  if (Either.isRight(outcome)) throw new Error(`expected a refusal, got ${outcome.right.join('\n')}`);
  return outcome.left as PlayerError;
};

const TILE = `tile_${'b'.repeat(32)}`;

// ---------------------------------------------------------------------------
// units
// ---------------------------------------------------------------------------

const UNITS: ReadonlyArray<JsonObject> = [
  {
    id: `unit_${'1'.repeat(32)}`,
    scope: 'own',
    type: 'Settlers',
    tile_id: TILE,
    x: 31,
    y: 72,
    hp: 20,
    moves: 3,
    type_stats: { max_hp: 20, move_rate: 3 },
    activity: { name: 'idle' },
    automation: { controller: 'player', has_orders: false },
    route: null,
  },
  {
    id: `unit_${'2'.repeat(32)}`,
    scope: 'own',
    type: 'Workers',
    tile_id: TILE,
    x: 31,
    y: 72,
    hp: 10,
    moves: 1,
    type_stats: { max_hp: 10, move_rate: 3 },
    activity: { name: 'irrigate' },
    automation: { controller: 'ai', has_orders: true },
    route: { mode: 'goto', order_count: 4, destination: { tile_id: TILE, x: 33, y: 70 } },
  },
];

describe('units', () => {
  test('the whole page is one aligned table, ID column last', () => {
    const lines = rendered(statePage('units', UNITS));
    expect(lines).toEqual([
      'rev9/t3 units 2/2 complete',
      `u1  Settlers  @31,72 mv3/3 hp20/20 idle                                  unit_${'1'.repeat(32)}`,
      `u2  Workers   @31,72 mv1/3 hp10/10 irrigate →(33,70) 4st !controller=ai  unit_${'2'.repeat(32)}`,
    ]);
    // The ID column starts at the same offset on every row.
    expect(lines[1]?.indexOf(`unit_${'1'.repeat(32)}`)).toBe(
      lines[2]?.indexOf(`unit_${'2'.repeat(32)}`) as number
    );
  });

  test('an alias cache drops the opaque ID column entirely', () => {
    const aliases: AliasMap = {
      [`unit_${'1'.repeat(32)}`]: 'u1',
      [`unit_${'2'.repeat(32)}`]: 'u2',
    };
    const lines = rendered(statePage('units', UNITS), aliases);
    expect(lines[1]).toBe('u1  Settlers  @31,72 mv3/3 hp20/20 idle');
    expect(lines.join('\n')).not.toContain('unit_11111');
  });

  test('an empty page names the section it found nothing in', () => {
    expect(rendered(statePage('units', []))[1]).toBe('(no units items on this page)');
  });
});

describe('routeSummary', () => {
  const walking: JsonObject = {
    mode: 'goto',
    order_count: 5,
    path_available: true,
    path_step_count: 3,
    destination: { tile_id: TILE, x: 40, y: 60 },
  };
  const summary = (route: JsonValue): string => Effect.runSync(routeSummary(route));

  test('the path step count leads, because that is what the wire carries', () => {
    expect(summary(walking)).toBe('→(40,60) 3st');
  });

  test('an unreconstructable path falls back to the queued order count', () => {
    expect(summary({ ...walking, mode: 'patrol', path_available: false })).toBe(
      '→(40,60) 5st patrol'
    );
  });

  test('neither count named prints `?st` rather than inventing one', () => {
    expect(summary({ destination: { x: 40, y: 60 } })).toBe('→(40,60) ?st');
  });

  test('vigilant is a non-default, so it prints; `goto` is the default and does not', () => {
    expect(summary({ ...walking, vigilant: true })).toBe('→(40,60) 3st vigilant');
  });

  test('a destination the payload does not carry is `?`, not a blank', () => {
    expect(summary({ path_step_count: 2 })).toBe('→(?) 2st');
  });

  test('no route at all says nothing about one', () => {
    expect(summary(null)).toBe('');
    expect(Effect.runSync(unitRow('u3', { ...UNITS[0], route: null } as JsonValue)).join(' ')).not.toContain('→');
  });
});

// ---------------------------------------------------------------------------
// cities
// ---------------------------------------------------------------------------

describe('cities', () => {
  const city: JsonObject = {
    id: `city_${'1'.repeat(32)}`,
    name: 'London',
    x: 31,
    y: 72,
    size: 1,
    surplus: { food: 2, shields: 1, trade: 0 },
    production: { kind: 'unit', name: 'Warriors', shield_stock: 0, shield_cost: 10 },
  };

  test('size, build, stock and every surplus ride one row', () => {
    const lines = rendered(statePage('cities', [city]));
    expect(lines[1]).toBe(
      `c1  London  @31,72 sz1 Warriors 0/10 f+2 s+1 t+0  city_${'1'.repeat(32)}`
    );
  });

  test('a rush-buy price rides the row, because it rides nothing else', () => {
    const buyable: JsonObject = {
      ...city,
      production: {
        kind: 'unit',
        name: 'Musketeers',
        shield_stock: 30,
        shield_cost: 30,
        buy_cost: 4,
        can_buy: true,
      },
    };
    const lines = rendered(statePage('cities', [buyable]), { [city['id'] as string]: 'c1' });
    expect(lines[1]).toBe('c1  London  @31,72 sz1 Musketeers 30/30 f+2 s+1 t+0 buy=4');
  });
});

// ---------------------------------------------------------------------------
// research
// ---------------------------------------------------------------------------

describe('research', () => {
  test('a technology is named, never numbered: the name is the handle', () => {
    const lines = rendered(
      statePage('research', [
        {
          id: 'tech_1',
          name: 'Alphabet',
          state: 'researchable',
          can_target: true,
          can_goal: true,
          path_cost: 1,
          unknown_prerequisite_count: 0,
        },
      ])
    );
    expect(lines[1]).toBe('Alphabet  researchable  path1 targetable goalable  tech_1');
  });

  test('an unknown-prerequisite count of zero is not a fact worth a cell', () => {
    const lines = rendered(
      statePage('research', [
        { id: 'tech_2', name: 'Bronze Working', state: 'unknown', unknown_prerequisite_count: 3 },
      ]),
      {}
    );
    expect(lines[1]).toBe('Bronze Working  unknown  needs3');
  });
});

// ---------------------------------------------------------------------------
// tiles
// ---------------------------------------------------------------------------

describe('tiles', () => {
  const window = (): ReadonlyArray<JsonObject> => {
    const items: JsonObject[] = [];
    for (const y of [71, 72]) {
      for (const x of [30, 31]) {
        const unknown = x === 30 && y === 71;
        items.push({
          id: `tile_${x}${y}${'0'.repeat(28)}`,
          x,
          y,
          visibility: unknown ? 'unknown' : 'visible',
          ...(unknown
            ? {}
            : {
                terrain: x === 30 ? 'Ocean' : 'Desert',
                owner_player_id: null,
                infrastructure_placement: null,
              }),
        });
      }
    }
    return items;
  };

  test('the grid is a coordinate table and the legend explains every glyph', () => {
    const grid = rendered(statePage('tile_window', window()));
    expect(grid[1]?.split(/\s+/)).toEqual(['y\\x', '30', '31']);
    expect(grid[2]?.split(/\s+/)).toEqual(['71', '?', 'De']);
    expect(grid[3]?.split(/\s+/)).toEqual(['72', 'Oc', 'De']);
    expect(grid[4]).toBe(
      'legend De=Desert Oc=Ocean ?=unknown/fogged .=not on this page lowercase=remembered'
    );
  });

  test('a remembered tile is the same glyph in lowercase, never a different one', () => {
    const grid = rendered(
      statePage('map_tiles', [
        { id: `tile_a${'0'.repeat(31)}`, x: 1, y: 1, visibility: 'visible', terrain: 'Hills' },
        { id: `tile_b${'0'.repeat(31)}`, x: 2, y: 1, visibility: 'fogged', terrain: 'Hills' },
      ])
    );
    expect(grid[2]).toBe('1    Hi  hi');
  });

  test('a tile carrying an owner or a placement gets its own note line', () => {
    const lines = rendered(
      statePage('known_tiles', [
        {
          id: `tile_c${'0'.repeat(31)}`,
          x: 4,
          y: 5,
          visibility: 'visible',
          terrain: 'Plains',
          owner_player_id: 7,
          infrastructure_placement: { name: 'Irrigation' },
        },
      ])
    );
    expect(lines[lines.length - 1]).toBe(
      `T(4,5) owner=7 building=Irrigation tile_c${'0'.repeat(31)}`
    );
  });

  test('a terrain code is a function of the name alone, so a glyph never moves', () => {
    expect(terrainCode('Deep Ocean')).toBe('Do');
    expect(terrainCode('Sludge')).toBe('Sl');
    expect(terrainCode('')).toBe('X0');
    expect(terrainCode('Q')).toBe('Q0');
  });

  test('a genuine collision is broken over the colliding names alone', () => {
    const codes = terrainCodes(new Set(['Slime', 'Sludge', 'Slag']));
    expect([...codes.entries()].sort()).toEqual([
      ['Slag', 'Sl'],
      ['Slime', 'S0'],
      ['Sludge', 'S1'],
    ]);
    // The same set collides the same way on any page.
    expect(terrainCodes(new Set(['Sludge', 'Slag', 'Slime']))).toEqual(codes);
  });

  test('a window too wide for a grid falls back to one row per tile', () => {
    const items: JsonObject[] = [];
    for (let x = 0; x <= 41; x += 1) {
      items.push({ id: `tile_${String(x).padStart(32, '0')}`, x, y: 0, visibility: 'visible', terrain: 'Plains' });
    }
    const lines = rendered(statePage('map_tiles', items));
    expect(lines[1]).toBe('0,0   Pl  visible');
    expect(lines[42]).toBe('41,0  Pl  visible');
  });
});

// ---------------------------------------------------------------------------
// overview
// ---------------------------------------------------------------------------

describe('overview', () => {
  const item: JsonObject = {
    client_state: 'running',
    turn: 1,
    phase: 0,
    phase_count: 1,
    player: {
      name: 'Ada',
      nation: 'English',
      government: 'Despotism',
      economy: { gold: 50, tax: 40, science: 60, luxury: 0 },
    },
    research: { target: 'Bronze Working', bulbs_researched: 0, cost: 28, output: 3, goal: null },
    counts: { units: 3, cities: 0 },
    map: { width: 64, height: 32, topology: 'wrapx' },
  };

  test('the head line is the four facts a phase decision needs, pipe separated', () => {
    const lines = rendered(statePage('overview', [item]));
    expect(lines).toEqual([
      'rev9/t3 overview 1/1 complete',
      'turn 1 | phase 0/1 | running | Ada (English) Despotism gold 50 tax40/lux0/sci60',
      'research Bronze Working 0/28 +3/turn',
      'map 64x32 wrapx',
      'counts units 3',
    ]);
  });

  test('no research target says so rather than printing an empty cell', () => {
    const lines = rendered(
      statePage('overview', [{ ...item, research: { target: null, bulbs_researched: 4, cost: 28 } }])
    );
    expect(lines[2]).toBe('research NO TARGET 4/28');
  });

  test('a provable lower bound is marked, so it is never read as the score', () => {
    expect(scoreText({ lower_bound: 17, components: { cities: 3, techs: 0 } })).toBe(
      'score >=17 (cities 3)'
    );
    expect(scoreText({ exact: 19 })).toBe('score 19');
    expect(scoreText({ exact: null, lower_bound: null })).toBe('');
  });

  // A state-page item is validated by `_json_value` alone, so `[]` and `{}` do
  // reach the renderer, and CPython's `if value:` calls both of them false.
  test('an empty container is as absent as a null, exactly as CPython reads it', () => {
    expect(
      rendered(statePage('overview', [{ ...item, research: { target: [], goal: {} } }]))[2]
    ).toBe('research NO TARGET');
    expect(
      rendered(statePage('overview', [{ ...item, research: { target: {}, goal: [] } }]))[2]
    ).toBe('research NO TARGET');
    expect(
      rendered(
        statePage('overview', [
          { turn: 3, counts: { b: [], c: {}, f: 'x', g: 0 }, player: {}, research: {} },
        ])
      )
    ).toEqual(['rev9/t3 overview 1/1 complete', 'turn 3 | ', 'research NO TARGET', 'counts f x']);
    expect(scoreText({ lower_bound: 5, components: { x: [], z: 'q', y: {}, w: 0 } })).toBe(
      'score >=5 (z q)'
    );
    expect(scoreText({ lower_bound: 5, components: { x: [], y: {} } })).toBe('score >=5');
  });

  // ...and a container with something in it is truthy, and prints as compact JSON.
  test('a container that holds something prints, rather than vanishing with the empties', () => {
    expect(scoreText({ exact: 7, components: { a: [1], b: { k: 1 } } })).toBe(
      'score 7 (a [1], b {"k":1})'
    );
    expect(
      rendered(statePage('overview', [{ ...item, research: { target: 'Alphabet', goal: ['x'] } }]))[2]
    ).toBe('research Alphabet goal ["x"]');
  });
});

// ---------------------------------------------------------------------------
// diplomacy
// ---------------------------------------------------------------------------

describe('diplomacy', () => {
  const items: ReadonlyArray<JsonObject> = [
    {
      relation_id: `relation_${'d'.repeat(32)}`,
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
    {
      relation_id: `relation_${'f'.repeat(32)}`,
      player_id: `player_${'a'.repeat(32)}`,
      player_name: 'Caesar',
      nation: 'Roman',
      alive: true,
      state: 'war',
      has_embassy: false,
      treaty_turns_left: null,
      meeting: null,
    },
  ];

  test('an open meeting is on the row, and the clause page is named as a command', () => {
    const aliases: AliasMap = {
      [`relation_${'d'.repeat(32)}`]: 'r1',
      [`relation_${'f'.repeat(32)}`]: 'r2',
    };
    const lines = rendered(statePage('diplomacy', items), aliases);
    expect(lines).toEqual([
      'rev9/t3 diplomacy 2/2 complete',
      'r1  Isabella  (Spanish) cease-fire embassy 4t left · !meeting open 2 clauses ' +
        'accepted by them, awaiting you',
      'r2  Caesar    (Roman) war',
      'clauses: just state --section diplomacy_clauses --relation_id r1',
    ]);
    expect(lines.join('\n')).not.toContain(`relation_${'d'.repeat(32)}`);
  });

  test('a meeting this seat has accepted no longer says it is awaiting you', () => {
    expect(meetingSummary({ clause_count: 1, self_accepted: true, other_accepted: false })).toBe(
      '!meeting open 1 clauses accepted by you'
    );
  });
});

// ---------------------------------------------------------------------------
// the generic flattener
// ---------------------------------------------------------------------------

describe('generic items', () => {
  test('an unmodelled section still renders every field it carries', () => {
    const chat = rendered(
      statePage('chat', [
        { id: 'chat_1', sender: 'Ada', text: 'hi' },
        { id: 'chat_2', sender: 'Bob', text: 'hello' },
      ])
    );
    expect(chat).toEqual([
      'rev9/t3 chat 2/2 complete',
      '#  id      sender  text',
      '1  chat_1  Ada     hi',
      '2  chat_2  Bob     hello',
    ]);
  });

  test('one level of nesting spreads into `parent.child` cells', () => {
    expect([...flattenItem({ id: 'x', cost: { shields: 3, turns: 1 }, empty: {} })]).toEqual([
      ['id', 'x'],
      ['cost.shields', '3'],
      ['cost.turns', '1'],
      ['empty', '…'],
    ]);
  });

  test('a column identical on every row is hoisted out as a page constant', () => {
    const lines = renderGenericItems([
      { city_id: `city_${'1'.repeat(32)}`, slot: 1, name: 'Warriors' },
      { city_id: `city_${'1'.repeat(32)}`, slot: 2, name: 'Settlers' },
    ]);
    expect(lines).toEqual([
      `constants: city_id=city_${'1'.repeat(32)}`,
      '#  slot  name',
      '1  1     Warriors',
      '2  2     Settlers',
    ]);
  });

  test('a page whose every field is constant says so instead of printing a table', () => {
    expect(renderGenericItems([{ kind: 'gold' }, { kind: 'gold' }])).toEqual([
      'constants: kind=gold',
      '2 item(s), all fields constant',
    ]);
  });

  test('a non-object item is numbered and scalar-rendered', () => {
    expect(renderGenericItems(['Ada', 7])).toEqual(['1  Ada', '2  7']);
  });

  test('past fourteen columns the rows go long-form rather than off the screen', () => {
    const wide = (seed: string): Record<string, JsonValue> => {
      const item: Record<string, JsonValue> = {};
      for (let index = 0; index < 15; index += 1) item[`f${index}`] = `${seed}${index}`;
      return item;
    };
    const lines = renderGenericItems([wide('a'), wide('b')]);
    expect(lines).toHaveLength(2);
    expect(lines[0]).toBe(
      '1  f0=a0 f1=a1 f2=a2 f3=a3 f4=a4 f5=a5 f6=a6 f7=a7 f8=a8 f9=a9 f10=a10 f11=a11 f12=a12 f13=a13 f14=a14'
    );
  });

  test('a section falls through to the flattener when one item is short a key', () => {
    const lines = Effect.runSync(
      renderSectionItems('units', [{ id: 'unit_x', type: 'Settlers', x: 1 }], null)
    );
    expect(lines[0]).toBe('#  id      type      x');
  });
});

// ---------------------------------------------------------------------------
// governments
// ---------------------------------------------------------------------------

describe('governments', () => {
  const PLAYER = `player_${'b'.repeat(32)}`;
  const at = revision(210, 40);
  const empty = (): V2ClientState => ({
    schema_version: 5,
    game_id: 'game_test',
    agent_id: 'agent_test',
    last_revision: at,
    actions: {},
    pending_catalogs: {},
    batches: {},
    receipts: {},
    action_aliases: { state_revision: null, by_alias: {} },
    entity_aliases: {},
    tile_aliases: {},
    drained_actors: [],
  });

  const cached = (): V2ClientState => ({
    ...empty(),
    entity_aliases: { p1: PLAYER },
    actions: {
      [`action_${'8'.repeat(32)}`]: {
        action_id: `action_${'8'.repeat(32)}`,
        kind: 'player.send_chat',
        label: 'Send chat',
        subject: { operation: 'send_chat', actor: { type: 'player', id: PLAYER } },
        arguments_schema: { type: 'object', properties: {} },
        state_revision: { ...at },
      },
    },
  });

  const settled: ReadonlyArray<JsonValue> = [
    { id: 'government_1', name: 'Despotism', current: true, target: false, can_change: false },
  ];
  const changeable: ReadonlyArray<JsonValue> = [
    ...settled,
    { id: 'government_2', name: 'Monarchy', current: false, target: false, can_change: true },
  ];
  const HINT = 'government actions are player-scoped: just legal --actor_id p1 --all';

  test('a changeable government names the catalog that actually enumerates it', () => {
    const lines = rendered(statePage('governments', changeable, at), {}, cached());
    expect(lines[lines.length - 1]).toBe(HINT);
  });

  test('nothing to change, nothing to say', () => {
    expect(rendered(statePage('governments', settled, at), {}, cached())).not.toContain(HINT);
  });

  test('without a cache there is no player alias, so no command is printed', () => {
    expect(rendered(statePage('governments', changeable, at), {}, null)).not.toContain(HINT);
  });

  test('a cache with no player-scoped descriptor names the placeholder, not a wrong alias', () => {
    expect(Effect.runSync(governmentScopeLines(changeable, empty()))).toEqual([
      'government actions are player-scoped: just legal --actor_id YOUR_PLAYER_ID --all',
    ]);
  });
});

// ---------------------------------------------------------------------------
// fail-closed
// ---------------------------------------------------------------------------

describe('contract drift', () => {
  test('a non-integer coordinate refuses and names the --json escape hatch', () => {
    const error = refusal(
      statePage('tile_window', [
        { id: `tile_${'a'.repeat(32)}`, x: 31, y: 'seventy-two', visibility: 'visible', terrain: 'Desert' },
      ])
    );
    expect(error.message).toContain('--json');
  });

  test('a terrain that is not a name refuses by naming the field that drifted', () => {
    const error = refusal(
      statePage('tile_window', [
        { id: `tile_${'a'.repeat(32)}`, x: 31, y: 72, visibility: 'visible', terrain: 7 },
      ])
    );
    expect(error.message).toContain('tile terrain');
  });

  test('a cached render whose item carries no usable ID is drift, not a positional guess', () => {
    const error = refusal(statePage('units', [{ type: 'Settlers', x: 1, y: 1, id: 4 }]), {});
    expect(error.message).toContain('entity id');
    // Without a cache the same row is display-only, and numbers positionally.
    expect(rendered(statePage('units', [{ type: 'Settlers', x: 1, y: 1, id: 'unit_x' }]))[1]).toBe(
      'u1  Settlers  @1,1  unit_x'
    );
  });
});
