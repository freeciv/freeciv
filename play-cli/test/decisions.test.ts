/**
 * U12 — the focus loop.
 *
 * Ports `test_v2_turn_decisions_lists_actors_and_refetches_only_what_is_stale`,
 * `test_v2_composed_order_never_prints_a_command_that_would_refuse`,
 * `test_v2_focus_tail_composes_every_actor_into_one_do`,
 * `test_v2_focus_tail_falls_back_when_the_composed_line_overflows`,
 * `test_v2_focus_tail_keeps_one_actor_and_teaches_the_one_call_ending`,
 * `test_v2_focus_line_degrades_instead_of_fetching_after_a_bump` and
 * `test_v2_tier1_verbs_reach_the_capabilities_they_name`.
 *
 * The seat is staged the way the shipped client leaves it: two mirror TSVs
 * written exactly as U07 writes them, plus a `.v2-state` carrying the actor
 * catalogs at one revision.  Nothing here opens a socket, which is the whole
 * contract of the projection — the Python's own test asserts the request list
 * is empty.
 */
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect } from 'effect';
import { batchFocusCommand, decisionLine, type DecisionRow } from 'src/render/decisions';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import type { Revision } from 'src/schema/revision';
import {
  briefingDecisionLines,
  decisionOptionRank,
  decisionOptions,
  decisionOrder,
  decisionRows,
  decisionVerb,
  liveDecisionDeps,
  nextFocusLine,
  type DecisionOptions,
} from 'src/services/decisions';
import { compactLegalAction } from 'src/services/legal-compact';
import { meetingGroups, meetingRemedy, openMeetings } from 'src/services/meetings';
import type { PrivateFs } from 'src/services/private-fs';
import type { V2ClientState } from 'src/services/session-store';
import { mirrorDir, writeMirror } from 'src/services/mirror';
import { mirrorNumber } from 'src/services/turn-pages';
import { scratchWorkspace, type Scratch } from 'test/_fixtures';

const GAME_ID = 'game_12345678901234567890';
const UNIT_ONE = `unit_${'a'.repeat(32)}`;
const UNIT_TWO = `unit_${'b'.repeat(32)}`;
const CITY_ONE = `city_${'c'.repeat(32)}`;

const REVISION: Revision = { turn: 3, revision: 7, state_token: 'token_3_7' };

const tileId = (x: number, y: number): string =>
  `tile_${`${String(x).padStart(4, '0')}${String(y).padStart(4, '0')}`.padStart(32, '0')}`;

const actorAction = (options: {
  readonly actionId: string;
  readonly actorId: string;
  readonly kind?: string;
  readonly operation?: string;
  readonly label?: string;
  readonly x?: number;
  readonly y?: number;
  readonly schema?: JsonObject;
}): JsonObject => {
  const x = options.x ?? 31;
  const y = options.y ?? 72;
  return {
    action_id: options.actionId,
    kind: options.kind ?? 'unit.order',
    label: options.label ?? 'Move',
    subject: {
      operation: options.operation ?? 'move',
      actor: { id: options.actorId, type: 'unit', name: 'Settlers' },
      target: { id: tileId(x, y), x, y },
      probability: { kind: 'exact', minimum_percent: 100, maximum_percent: 100 },
    },
    arguments_schema: options.schema ?? { type: 'object' },
    state_revision: { ...REVISION },
  };
};

const cityAction = (options: {
  readonly actionId: string;
  readonly kind: string;
  readonly operation: string;
  readonly label: string;
  readonly target: JsonValue;
}): JsonObject => ({
  action_id: options.actionId,
  kind: options.kind,
  label: options.label,
  subject: {
    operation: options.operation,
    actor: { id: CITY_ONE, type: 'city', name: 'London' },
    target: options.target,
    probability: { kind: 'exact', minimum_percent: 100, maximum_percent: 100 },
  },
  arguments_schema: { type: 'object' },
  state_revision: { ...REVISION },
});

const productionTarget = (name: string, suffix: string): JsonValue => ({
  type: 'production',
  id: `production_${suffix.repeat(8)}`,
  kind: 'improvement',
  name,
});

const FOUND = `action_found${'0'.repeat(20)}`;
const MOVE = `action_move1${'0'.repeat(20)}`;
const SENTRY = `action_sentry${'0'.repeat(19)}`;
const ROAD = `action_road0${'0'.repeat(20)}`;
const PROD_A = `action_prod01${'0'.repeat(19)}`;
const PROD_B = `action_prod02${'0'.repeat(19)}`;

const ACTIONS: ReadonlyArray<readonly [string, JsonObject]> = [
  [
    FOUND,
    actorAction({
      actionId: FOUND,
      actorId: UNIT_ONE,
      kind: 'unit.found_city',
      operation: 'found_city',
      label: 'Found city',
    }),
  ],
  [MOVE, actorAction({ actionId: MOVE, actorId: UNIT_ONE })],
  [
    SENTRY,
    actorAction({
      actionId: SENTRY,
      actorId: UNIT_TWO,
      kind: 'unit.sentry',
      operation: 'sentry',
      label: 'Sentry',
    }),
  ],
  [
    ROAD,
    actorAction({
      actionId: ROAD,
      actorId: UNIT_TWO,
      kind: 'unit.start_activity',
      operation: 'road',
      label: 'Build road',
    }),
  ],
  [
    PROD_A,
    cityAction({
      actionId: PROD_A,
      kind: 'city.set_production',
      operation: 'set_production',
      label: 'Build Warriors',
      target: productionTarget('Warriors', 'a'),
    }),
  ],
  [
    PROD_B,
    cityAction({
      actionId: PROD_B,
      kind: 'city.set_production',
      operation: 'set_production',
      label: 'Build Granary',
      target: productionTarget('Granary', 'b'),
    }),
  ],
];

const ALIAS_ORDER = [FOUND, MOVE, SENTRY, ROAD, PROD_A, PROD_B] as const;

const actorOf = (actionId: string): string =>
  actionId === SENTRY || actionId === ROAD
    ? UNIT_TWO
    : actionId === PROD_A || actionId === PROD_B
      ? CITY_ONE
      : UNIT_ONE;

const focusState = (revision: Revision | null = REVISION): V2ClientState => ({
  schema_version: 5,
  game_id: GAME_ID,
  agent_id: 'agent_test',
  last_revision: revision,
  actions: Object.fromEntries(ACTIONS.map(([id, value]) => [id, value])),
  pending_catalogs: {},
  batches: {},
  receipts: {},
  action_aliases: {
    state_revision: { ...REVISION },
    by_alias: Object.fromEntries(
      ALIAS_ORDER.map((actionId, index) => [
        `a${index + 1}`,
        { action_id: actionId, actor_id: actorOf(actionId), semantics: `s${index}` },
      ])
    ),
  },
  entity_aliases: { u1: UNIT_ONE, u2: UNIT_TWO, c1: CITY_ONE },
  tile_aliases: {},
  drained_actors: [UNIT_ONE, UNIT_TWO, CITY_ONE],
});

const UNITS_TSV = [
  '# rev 7 turn 3',
  '# units 3/3 complete',
  'alias\tunit\twho\tpos\tmoves\thp\tactivity\torders',
  'u1   \tSettlers\town\t31,72\t3/3\t20/20\tidle\t-',
  'u2   \tWorkers \town\t31,72\t2/2\t10/10\tidle\t-',
  'u3   \tExplorer\town\t31,72\t3/3\t10/10\tidle\t→(31,72) 4st',
].join('\n');

const CITIES_TSV = [
  '# rev 7 turn 3',
  '# cities 1/1 complete',
  'alias\tcity\tpos\tsize\tbuilding\tshields\tsurplus\tbuy',
  'c1\tLondon\t31,72\t1\t-\t-\tf2 s1 t0\t-',
].join('\n');

const DIPLOMACY_TSV = [
  '# rev 7 turn 3',
  '# diplomacy 2/2 complete',
  'alias\tplayer\tnation\tstate\tembassy\tmeeting\tclauses\taccepted',
  'r1\tSpain\tSpanish\tcease-fire\tyes\topen\t2\tthem',
  'r2\tRome \tRoman  \tpeace     \tyes\topen\t1\tyou+them',
].join('\n');

interface Seat {
  readonly sessionPath: string;
  readonly run: <A, E>(effect: Effect.Effect<A, E, PrivateFs>) => A;
}

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

const stageSeat = (files: ReadonlyArray<readonly [ReadonlyArray<string>, string]>): Seat => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const sessionPath = path.join(scratch.workspace.stateRoot, GAME_ID, 'codex-test.json');
  const dir = Effect.runSync(mirrorDir(sessionPath));
  const run = <A, E>(effect: Effect.Effect<A, E, PrivateFs>): A =>
    Effect.runSync(Effect.provide(effect, scratch.layer) as Effect.Effect<A>);
  for (const [parts, text] of files) run(writeMirror(dir, parts, text));
  return { sessionPath, run };
};

const focusSeat = (): Seat =>
  stageSeat([
    [['state', 'units.tsv'], UNITS_TSV],
    [['state', 'cities.tsv'], CITIES_TSV],
  ]);

const compact = (actionId: string): JsonObject => {
  const found = ACTIONS.find(([id]) => id === actionId);
  if (found === undefined) throw new Error(`no fixture action ${actionId}`);
  return Effect.runSync(compactLegalAction(found[1]) as Effect.Effect<JsonObject>);
};

// ---------------------------------------------------------------------------

describe('_decision_verb / _decision_option_rank', () => {
  test('the verb is the operation unless the kind tail already says it', () => {
    expect(decisionVerb(compact(MOVE), liveDecisionDeps)).toBe('move');
    expect(decisionVerb(compact(SENTRY), liveDecisionDeps)).toBe('sentry');
    expect(decisionVerb(compact(FOUND), liveDecisionDeps)).toBe('found_city');
  });

  test('preferred beats unknown beats housekeeping', () => {
    expect(decisionOptionRank(compact(FOUND), liveDecisionDeps)[0]).toBe(0);
    expect(decisionOptionRank(compact(SENTRY), liveDecisionDeps)).toEqual([2, 0, 'sentry']);
    // `found_city` is first in the preferred table, `move` is eleventh.
    expect(decisionOptionRank(compact(FOUND), liveDecisionDeps)[1]).toBeLessThan(
      decisionOptionRank(compact(MOVE), liveDecisionDeps)[1]
    );
  });
});

describe('_decision_rows', () => {
  test('walks units then cities, skips the unit with a standing route', () => {
    const seat = focusSeat();
    const rows = seat.run(decisionRows(seat.sessionPath, focusState(), liveDecisionDeps));

    expect(rows.map((row) => row.alias)).toEqual(['u1', 'u2', 'c1']);
    expect(rows[0]?.state).toBe('Settlers @31,72 idle');
    expect(rows[1]?.state).toBe('Workers @31,72 idle');
    expect(rows[2]?.state).toBe('London @31,72 no production');
    // The Explorer carries orders, so it is not asked to decide again.
    expect(rows.some((row) => row.state.includes('Explorer'))).toBe(false);
  });

  test('each row carries the option aliases `do` accepts, best verb first', () => {
    const seat = focusSeat();
    const rows = seat.run(decisionRows(seat.sessionPath, focusState(), liveDecisionDeps));

    expect(rows[0]?.options).toEqual(['a1 found_city T(31,72)', 'a2 move T(31,72)']);
    expect(rows[1]?.options).toEqual(['a4 road T(31,72)', 'a3 sentry T(31,72)']);
    // Two rows share one verb, so the verb prints once with its count and the
    // target is left to the drill-down.
    expect(rows[2]?.options).toEqual(['a5 set_productionx2']);
    expect(rows[0]?.remedy).toBe('just legal --actor_id u1 --all');
  });

  test('every printed line stays inside the 120-column budget', () => {
    const seat = focusSeat();
    const rows = seat.run(decisionRows(seat.sessionPath, focusState(), liveDecisionDeps));
    for (const row of rows) expect(decisionLine(row).length).toBeLessThanOrEqual(120);
    expect(decisionLine(rows[0] as DecisionRow)).toContain('a1 found_city');
  });

  test('a catalog that died with its revision degrades to the enumeration command', () => {
    const seat = focusSeat();
    // The receipt bumped the revision; every cached descriptor is now stale.
    const bumped: Revision = { turn: 3, revision: 9, state_token: 'token_3_9' };
    const rows = seat.run(
      decisionRows(seat.sessionPath, focusState(bumped), liveDecisionDeps)
    );
    expect(rows[1]?.options).toEqual([]);
    expect(decisionLine(rows[1] as DecisionRow)).toBe(
      'u2 Workers @31,72 idle — just legal --actor_id u2 --all'
    );
  });

  test('an actor already ordered is never offered back', () => {
    const seat = focusSeat();
    const rows = seat.run(
      decisionRows(seat.sessionPath, focusState(), liveDecisionDeps, {
        done: new Set([UNIT_ONE]),
      })
    );
    expect(rows.map((row) => row.alias)).toEqual(['u2', 'c1']);
  });
});

describe('_decision_order', () => {
  test('only an order that resolves and binds its whole schema is offered', () => {
    const founding = Effect.runSync(
      compactLegalAction({
        ...actorAction({
          actionId: FOUND,
          actorId: UNIT_ONE,
          kind: 'unit.found_city',
          operation: 'found_city',
          label: 'Found city',
        }),
        arguments_schema: {
          type: 'object',
          properties: { city_name: { type: 'string' } },
          required: ['city_name'],
        },
      }) as Effect.Effect<JsonObject>
    );
    const pool = [founding, compact(MOVE)];
    // `found_city` outranks `move`, but its city name is the player's to
    // choose, so the tail yields rather than printing a line that would refuse.
    expect(Effect.runSync(decisionOrder(pool, 'u1', liveDecisionDeps) as Effect.Effect<string>)).toBe(
      'u1 move T(31,72)'
    );
  });

  test('with no schema to fill, the top-ranked verb is offered first', () => {
    expect(
      Effect.runSync(
        decisionOrder([compact(FOUND), compact(MOVE)], 'u1', liveDecisionDeps) as Effect.Effect<string>
      )
    ).toBe('u1 found_city T(31,72)');
  });

  test('two actions sharing one verb and one target key are both withheld', () => {
    const twins = [
      Effect.runSync(
        compactLegalAction(
          actorAction({
            actionId: `action_twin1${'0'.repeat(20)}`,
            actorId: UNIT_ONE,
            kind: 'unit.start_activity',
            operation: 'road',
            label: 'Road',
          })
        ) as Effect.Effect<JsonObject>
      ),
      Effect.runSync(
        compactLegalAction(
          actorAction({
            actionId: `action_twin2${'0'.repeat(20)}`,
            actorId: UNIT_ONE,
            kind: 'unit.start_activity',
            operation: 'road',
            label: 'Road',
          })
        ) as Effect.Effect<JsonObject>
      ),
    ];
    const run = (pool: ReadonlyArray<JsonObject>, alias: string): string =>
      Effect.runSync(decisionOrder(pool, alias, liveDecisionDeps) as Effect.Effect<string>);
    expect(run(twins, 'u1')).toBe('');
    expect(run([compact(FOUND), compact(MOVE)], '')).toBe('');
    expect(run([], 'u1')).toBe('');
  });

  test('a tier-1 word that fixes no arguments replaces the wire operation', () => {
    // `build` is `city.set_production/set_production` with no fixed arguments.
    expect(
      Effect.runSync(
        decisionOrder([compact(PROD_A), compact(PROD_B)], 'c1', liveDecisionDeps) as Effect.Effect<string>
      )
    ).toBe('c1 build Warriors');
  });
});

describe('_batch_focus_command', () => {
  const row = (overrides: Partial<DecisionRow>): DecisionRow => ({
    alias: 'u1',
    actor_id: 'unit_1',
    state: 'idle',
    options: [],
    option_count: 1,
    order: '',
    remedy: 'just legal --actor_id u1 --all',
    ...overrides,
  });

  test('every actor with a composable order becomes one `just do` line', () => {
    const seat = focusSeat();
    const rows = seat.run(decisionRows(seat.sessionPath, focusState(), liveDecisionDeps));
    expect(batchFocusCommand(rows)).toBe(
      'next 3 actors: just do "u1 found_city T(31,72); u2 road T(31,72); c1 build Warriors"'
    );
    expect(batchFocusCommand(rows).length).toBeLessThanOrEqual(200);
  });

  test('an overflowing line trims the actors it names while the count stays honest', () => {
    const wide = Array.from({ length: 8 }, (_value, index) =>
      row({
        alias: `u${index + 1}`,
        actor_id: `unit_${index + 1}`,
        order: `u${index + 1} connect_route T(100,${String(index + 1).padStart(3, '0')})`,
        remedy: `just legal --actor_id u${index + 1} --all`,
      })
    );
    const trimmed = batchFocusCommand(wide);
    expect(trimmed.startsWith('next 8 actors, top ')).toBe(true);
    expect(trimmed.length).toBeLessThanOrEqual(200);

    const wider = wide.map((entry) => row({ ...entry, order: `${entry.order} ${'x'.repeat(90)}` }));
    expect(batchFocusCommand(wider)).toBe(
      'next 8 actors need orders — just turn --decisions'
    );
  });

  test('one actor is not a batch', () => {
    expect(
      batchFocusCommand([
        row({
          alias: 'u2',
          actor_id: 'unit_2',
          state: 'Workers idle',
          options: ['a4 road'],
          order: 'u2 road T(31,72)',
          remedy: 'just legal --actor_id u2 --all',
        }),
      ])
    ).toBe('');
  });
});

describe('_decision_line', () => {
  test('the "more" tail names the remedy when options were cut', () => {
    expect(
      decisionLine({
        alias: 'u1',
        actor_id: 'unit_1',
        state: 'Settlers @31,72 idle',
        options: ['a1 found_city T(31,72)'],
        option_count: 4,
        order: '',
        remedy: 'just legal --actor_id u1 --all',
      })
    ).toBe(
      'u1 Settlers @31,72 idle — a1 found_city T(31,72) · more: just legal --actor_id u1 --all'
    );
  });

  test('an option too wide for the budget is dropped, not truncated', () => {
    const line = decisionLine({
      alias: 'u1',
      actor_id: 'unit_1',
      state: 'idle',
      options: [`a1 ${'x'.repeat(200)}`],
      option_count: 1,
      order: '',
      remedy: 'just legal --actor_id u1 --all',
    });
    expect(line).toBe('u1 idle — just legal --actor_id u1 --all');
  });
});

describe('_next_focus_line / _briefing_decision_lines', () => {
  test('an empty seat teaches the one-call ending', () => {
    const seat = stageSeat([]);
    expect(
      seat.run(nextFocusLine(seat.sessionPath, focusState(), new Set(), liveDecisionDeps))
    ).toBe('next: no actors need orders — just turn --end --await --brief');
  });

  test('one remaining actor degrades to the single-actor row', () => {
    const seat = focusSeat();
    const line = seat.run(
      nextFocusLine(
        seat.sessionPath,
        focusState({ turn: 3, revision: 9, state_token: 'token_3_9' }),
        new Set([UNIT_ONE, CITY_ONE]),
        liveDecisionDeps
      )
    );
    expect(line).toBe('next: u2 Workers @31,72 idle — just legal --actor_id u2 --all');
  });

  test('the briefing block closes with the composed batch', () => {
    const seat = focusSeat();
    const lines = seat.run(
      briefingDecisionLines(seat.sessionPath, focusState(), liveDecisionDeps)
    );
    expect(lines).toHaveLength(4);
    expect(lines[0]?.startsWith('u1 Settlers @31,72 idle')).toBe(true);
    expect(lines[3]?.startsWith('next 3 actors: just do "')).toBe(true);
  });
});

describe('open meetings', () => {
  test('only relations still waiting on this seat are returned', () => {
    const seat = stageSeat([[['state', 'diplomacy.tsv'], DIPLOMACY_TSV]]);
    expect(seat.run(openMeetings(seat.sessionPath))).toEqual({
      r1: 'meeting pending: Spain, cease-fire, 2 clauses',
    });
  });

  test('a meeting known only from the mirror names this seat as the actor', () => {
    expect(meetingRemedy([], 'r1', {}, 'p1')).toBe(
      'just legal --actor_id p1 --target_id r1 --all'
    );
    expect(meetingRemedy([], 'diplomacy', {})).toBe(
      'just legal --actor_id YOUR_PLAYER_ID --target_id RELATION_ID --all'
    );
  });

  test('a relation with an open meeting is a row of its own', () => {
    const seat = stageSeat([
      [['state', 'units.tsv'], UNITS_TSV],
      [['state', 'cities.tsv'], CITIES_TSV],
      [['state', 'diplomacy.tsv'], DIPLOMACY_TSV],
    ]);
    const rows = seat.run(decisionRows(seat.sessionPath, focusState(), liveDecisionDeps));
    const meeting = rows.find((entry) => entry.alias === 'r1');
    expect(meeting?.state).toBe('meeting pending: Spain, cease-fire, 2 clauses');
    // A meeting's actor is this seat's own player, so no order can compose.
    expect(meeting?.order).toBe('');
  });
});

// ---------------------------------------------------------------------------
// A CPython dict has own keys only (NOTES §18.10, §U12.12)
// ---------------------------------------------------------------------------

/**
 * Every expectation in this block is the output of the real CPython helper on
 * the same input, taken from `play/client.py` directly.  A wire string that
 * happens to spell an inherited JavaScript member is an ordinary miss for
 * `dict.get`; a plain property read returns the built-in instead, and for
 * `V2_TIER1_VERBS` reading `.arguments` off that built-in *throws* — a defect,
 * not a typed error, so it escapes the `orElseSucceed` guards that exist to
 * keep a briefing from turning a successful receipt into a failure.
 */
describe('an inherited member name is a miss, not a hit', () => {
  const protoAction = (operation: string, actionId = `action_p${'0'.repeat(23)}`): JsonObject =>
    Effect.runSync(
      compactLegalAction(
        actorAction({ actionId, actorId: UNIT_ONE, operation })
      ) as Effect.Effect<JsonObject>
    );

  const order = (pool: ReadonlyArray<JsonObject>, alias: string): string =>
    Effect.runSync(decisionOrder(pool, alias, liveDecisionDeps) as Effect.Effect<string>);

  const options = (
    pool: ReadonlyArray<JsonObject>,
    aliases: Record<string, string>
  ): ReadonlyArray<string> =>
    Effect.runSync(decisionOptions(pool, aliases, liveDecisionDeps) as Effect.Effect<DecisionOptions>)
      .options;

  test('an operation naming a built-in composes its order instead of throwing', () => {
    // `V2_TIER1_VERBS.get("toString")` misses, `defaults` stays `{}`, and the
    // order composes exactly as `move` does.
    for (const operation of ['toString', 'constructor', 'valueOf', 'hasOwnProperty']) {
      expect(order([protoAction(operation)], 'u1')).toBe(`u1 ${operation} T(31,72)`);
    }
    expect(order([protoAction('move')], 'u1')).toBe('u1 move T(31,72)');
  });

  test('the same operation still reaches the briefing and the focus line', () => {
    const seat = focusSeat();
    // `_next_focus_line` and `_briefing_decision_lines` promise never to turn a
    // successful receipt into an error; a thrown `TypeError` is not something
    // their `orElseSucceed` can absorb, so this asserts they still print.
    const state: V2ClientState = {
      ...focusState(),
      actions: {
        [MOVE]: {
          ...actorAction({ actionId: MOVE, actorId: UNIT_ONE, operation: 'toString' }),
        },
      },
    };
    expect(seat.run(briefingDecisionLines(seat.sessionPath, state, liveDecisionDeps))[0]).toBe(
      'u1 Settlers @31,72 idle — a2 toString T(31,72)'
    );
    expect(
      seat.run(nextFocusLine(seat.sessionPath, state, new Set([UNIT_TWO, CITY_ONE]), liveDecisionDeps))
    ).toBe('next: u1 Settlers @31,72 idle — a2 toString T(31,72)');
  });

  test('an action_id naming a built-in has no alias, not a function body', () => {
    const pool = [protoAction('move', 'toString')];
    expect(options(pool, {})).toEqual(['move T(31,72)']);
    // …and a table that really carries the key still names it, so the guard
    // did not simply drop the lookup.
    expect(options(pool, Object.fromEntries([['toString', 'a1']]))).toEqual(['a1 move T(31,72)']);
  });

  test('a meeting actor or relation naming a built-in stays itself in the remedy', () => {
    const meeting = Effect.runSync(
      compactLegalAction({
        action_id: `action_m${'0'.repeat(23)}`,
        kind: 'diplomacy.meeting',
        label: 'Meet',
        subject: {
          operation: 'meeting',
          actor: { id: 'constructor' },
          target: { id: 'toString' },
          probability: { kind: 'exact', minimum_percent: 100, maximum_percent: 100 },
        },
        arguments_schema: { type: 'object' },
        state_revision: { ...REVISION },
      }) as Effect.Effect<JsonObject>
    );
    // The remedy line's whole job is to be copy-pasteable; `just legal
    // --actor_id function Object() { [native code] } --all` is not a command.
    expect(meetingRemedy([meeting], 'diplomacy', {})).toBe(
      'just legal --actor_id constructor --target_id toString --all'
    );
    // An unaliased target files the group under `diplomacy`, one row for the
    // unnamed remainder — never under the built-in's stringification.
    expect([...meetingGroups([meeting], {}).keys()]).toEqual(['diplomacy']);
  });
});

describe('_mirror_number', () => {
  test('reads the left half of a `3/3` cell, and refuses anything else', () => {
    expect(mirrorNumber('3/3')).toBe(3);
    expect(mirrorNumber('-2')).toBe(-2);
    expect(mirrorNumber('-')).toBeNull();
    expect(mirrorNumber('1234567890')).toBeNull();
  });
});
