/**
 * U12 — the turn briefing's rendering and its flag matrix.
 *
 * Ports `test_v2_turn_briefing_groups_units_and_names_the_decision`,
 * `test_v2_briefing_counts_new_events_and_names_the_feed`'s line assertions,
 * `test_v2_brief_without_a_wake_is_refused_with_the_form_that_works` and
 * `test_v2_turn_decisions_refuses_to_double_as_the_phase_end` from
 * `play/tests/test_client.py`.
 *
 * The renderers the briefing leans on are U10's, imported live: a golden that
 * re-implemented `_economy_text` here would pass while the shipped briefing
 * diverged.
 */
import { describe, expect, test } from 'bun:test';
import { Effect } from 'effect';
import { economyText, researchText, scoreText } from 'src/render/state/overview';
import { renderSectionItems } from 'src/render/state/page';
import { renderTiles } from 'src/render/state/tiles';
import { routeSummary } from 'src/render/state/units';
import {
  briefingNeedsDecision,
  briefingTruncation,
  renderTurn,
  researchableNames,
  unitStatus,
  type RenderTurnDeps,
  type TurnCompactPage,
  type TurnReadyResult,
  type TurnResult,
} from 'src/render/turn';
import type { JsonValue } from 'src/schema/primitives';
import type { PageEnvelope } from 'src/schema/page';
import type { Revision } from 'src/schema/revision';
import type { TurnHealthContext } from 'src/services/health-context';
import { briefingEventsLine, turnCompactPage, turnNextCommands } from 'src/services/turn-pages';
import {
  AWAIT_WITHOUT_END,
  BRIEF_WITHOUT_WAKE,
  DECISIONS_WITH_END,
  checkTurnFlags,
} from 'src/commands/turn.cmd';
import { FIXTURE_AGENT_ID, FIXTURE_GAME_ID, pagePayload } from 'test/_fixtures';

const deps: RenderTurnDeps = {
  routeSummary,
  economyText,
  researchText,
  scoreText,
  renderSectionItems,
  renderTiles,
};

const run = <A>(effect: Effect.Effect<A, unknown>): A => Effect.runSync(effect as Effect.Effect<A>);

const revision = (value: number, turn = 3): Revision => ({
  turn,
  revision: value,
  state_token: `token_${turn}_${value}`,
});

const emptyPage = (): TurnCompactPage => ({
  shown: 0,
  total: 0,
  truncated: false,
  items: [],
  next_cursor: null,
  cursor_expires_at: null,
});

const context = (overrides: Partial<TurnHealthContext> = {}): TurnHealthContext => ({
  game_state: 'running',
  objective: 'score',
  max_turns: 5000,
  turns_remaining: 4999,
  agent: { agent_id: FIXTURE_AGENT_ID, controller_label: 'codex-test-model' },
  seat: { place: 1, seat_id: 'place-1', player_name: 'AgentPlace1' },
  sidecar: { state: 'ready', generation: 1 },
  observation_available: true,
  legal_actions_available: true,
  phase: {
    state: 'awaiting_agent',
    turn: 1,
    phase: 0,
    active: true,
    timing: {
      mode: 'default',
      timeout_s: 180,
      deadline_started_at: 1000,
      deadline_at: 1180,
      elapsed_s: 1,
      remaining_s: 179,
    },
  },
  last_phase_end: null,
  ...overrides,
});

const TILE = `tile_${'b'.repeat(32)}`;

const settler = (index: number): JsonValue => ({
  id: `unit_${index}${'0'.repeat(30)}`,
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
});

const explorer: JsonValue = {
  id: `unit_3${'0'.repeat(30)}`,
  scope: 'own',
  type: 'Explorer',
  tile_id: TILE,
  x: 31,
  y: 72,
  hp: 10,
  moves: 9,
  type_stats: { max_hp: 10, move_rate: 9 },
  activity: { name: 'sentry' },
  automation: { controller: 'player', has_orders: true },
  route: null,
};

const overview: JsonValue = {
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
  research: {
    target: 'Bronze Working',
    bulbs_researched: 0,
    cost: 28,
    output: 3,
    goal: null,
  },
  counts: { units: 3, cities: 0 },
};

const briefing = (): TurnReadyResult => ({
  schema_version: 1,
  command: 'turn',
  status: 'ready',
  context: context(),
  state_revision: revision(8, 1),
  overview,
  cities: emptyPage(),
  units: {
    shown: 3,
    total: 3,
    truncated: false,
    items: [settler(1), settler(2), explorer],
    next_cursor: null,
    cursor_expires_at: null,
  },
  research: {
    shown: 16,
    total: 88,
    truncated: true,
    items: [],
    next_cursor: `cursor_${'a'.repeat(32)}`,
    cursor_expires_at: null,
  },
  next_commands: ['just wait --session S'],
});

describe('_render_turn', () => {
  test('groups units that share a type, tile and status, and names the decision', () => {
    const lines = run(renderTurn(briefing(), deps));

    expect(lines[0]?.startsWith('T1 rev8/t1 | running |')).toBe(true);
    expect(lines[0]).toContain('179s left');
    expect(lines[0]).toContain('4999 turns left');
    expect(lines[1]).toContain('Despotism gold 50 tax40/lux0/sci60');
    expect(lines[1]).toContain('research Bronze Working 0/28 +3/turn');
    expect(lines).toContain('  u1,u2 Settlers @31,72 idle mv3/3');
    expect(lines).toContain('  u3 Explorer @31,72 sentry mv9/9');

    const decision = lines.filter((line) => line.startsWith('needs decision: '));
    expect(decision).toHaveLength(1);
    expect(decision[0]?.startsWith('needs decision: 2 idle unit(s)')).toBe(true);
    // A count taken over a truncated page must say so rather than claim to be
    // the empire's authoritative decision list.
    expect(decision[0]).toContain('shown page only');
    expect(lines.some((line) => line.startsWith('research page 16/88 (truncated)'))).toBe(true);
  });

  test('prints the continuation cursor of every truncated section, last', () => {
    const lines = run(renderTurn(briefing(), deps));
    expect(lines[lines.length - 1]).toBe(
      `next: just state --cursor cursor_${'a'.repeat(32)}   # rest of research`
    );
  });

  test('the decisions block and the events line sit between the count and the cursor', () => {
    const lines = run(
      renderTurn(briefing(), deps, {
        decisions: ['u1 Settlers @31,72 idle — a1 found_city'],
        events: 'events: 5 new — just state --section chat',
      })
    );
    const at = (text: string): number => lines.findIndex((line) => line.startsWith(text));
    expect(at('needs decision: ')).toBeLessThan(at('u1 Settlers'));
    expect(at('u1 Settlers')).toBeLessThan(at('events: '));
    expect(at('events: ')).toBeLessThan(at('next: just state --cursor'));
  });

  test('a non-ready status prints one summary line and its next commands', () => {
    const notReady: TurnResult = {
      schema_version: 1,
      command: 'turn',
      status: 'not_ready',
      context: context({ observation_available: false }),
      next_commands: ['just wait'],
    };
    expect(run(renderTurn(notReady, deps))).toEqual([
      'turn not_ready | running | phase awaiting_agent t1/p0 active 179s left | obs no legal yes',
      'next: just wait',
    ]);
  });

  test('an overview without a turn is drift, not a blank line', () => {
    const drifted = { ...briefing(), overview: { player: null } as JsonValue };
    const failure = Effect.runSync(Effect.either(renderTurn(drifted, deps)));
    expect(failure._tag).toBe('Left');
  });
});

describe('_unit_status', () => {
  test('names the activity, the route, a foreign controller and the moves', () => {
    expect(
      run(
        unitStatus(
          {
            activity: { name: 'idle' },
            automation: { controller: 'server' },
            moves: 2,
            type_stats: { move_rate: 3 },
            route: {
              mode: 'goto',
              order_count: 4,
              path_available: true,
              path_step_count: 4,
              destination: { x: 40, y: 60 },
            },
          },
          deps
        )
      )
    ).toBe('idle →(40,60) 4st !controller=server mv2/3');
  });

  test('an activity the payload does not name is "unknown", and a rate it lacks is "?"', () => {
    expect(run(unitStatus({ moves: 1 }, deps))).toBe('unknown mv1/?');
  });
});

describe('_briefing_needs_decision', () => {
  test('counts idle units, a missing research target and a city with no production', () => {
    expect(
      briefingNeedsDecision(
        { research: { target: null } },
        [{ activity: { name: 'idle' }, automation: { has_orders: false } }],
        [{ name: 'London', production: null }]
      )
    ).toBe('needs decision: 1 idle unit(s), no research target, London has no production');
  });

  test('nothing idle teaches the one-call ending', () => {
    expect(briefingNeedsDecision({ research: { target: 'Alphabet' } }, [], [])).toBe(
      'needs decision: nothing idle; just turn --end --await --brief'
    );
  });

  test('a unit with queued orders is not idle', () => {
    expect(
      briefingNeedsDecision(
        { research: { target: 'Alphabet' } },
        [{ activity: { name: 'idle' }, automation: { has_orders: true } }],
        []
      )
    ).toBe('needs decision: nothing idle; just turn --end --await --brief');
  });
});

describe('_researchable_names / _briefing_truncation', () => {
  test('only targetable technologies are named, with their path cost', () => {
    expect(
      researchableNames([
        { name: 'Alphabet', can_target: true, path_cost: 2 },
        { name: 'Pottery', can_target: true },
        { name: 'Bronze Working', can_target: false },
        { can_target: true },
      ])
    ).toEqual(['Alphabet/2', 'Pottery']);
  });

  test('a truncated section with no cursor names no continuation', () => {
    const result = briefing();
    expect(
      briefingTruncation({
        ...result,
        research: { ...result.research, next_cursor: null },
      })
    ).toEqual([]);
  });
});

describe('_turn_next_commands / _turn_compact_page', () => {
  const page = (section: string, cursor: string | null): PageEnvelope => ({
    schema_version: 2,
    control_protocol: 'full-control-v2',
    game_id: FIXTURE_GAME_ID,
    agent_id: FIXTURE_AGENT_ID,
    state_revision: revision(9),
    page: {
      section,
      items: [{ id: 'x' }],
      total_items: 2,
      next_cursor: cursor,
      cursor_expires_at: null,
    },
  });

  test('every open cursor leads, then the four fixed enumerations', () => {
    const cursor = `cursor_${'c'.repeat(32)}`;
    expect(
      turnNextCommands({
        overview: page('overview', null),
        cities: page('cities', cursor),
        units: page('units', null),
        research: page('research', null),
      })
    ).toEqual([
      `just state --cursor ${cursor}`,
      'just legal --kind phase.end --all',
      'just legal --kind research.set_target --all',
      'just legal --kind economy.set_rates --all',
      'just legal --actor_id ACTOR_ID --all',
    ]);
  });

  test('a page with a next cursor compacts as truncated', () => {
    const cursor = `cursor_${'c'.repeat(32)}`;
    expect(turnCompactPage(page('cities', cursor))).toEqual({
      shown: 1,
      total: 2,
      truncated: true,
      items: [{ id: 'x' }],
      next_cursor: cursor,
      cursor_expires_at: null,
    });
    // The fixture builder proves the envelope shape this test hand-rolls.
    expect(pagePayload()['schema_version']).toBe(2);
  });
});

describe('_briefing_events_line', () => {
  test('counts only what arrived since the mirror was last written', () => {
    expect(briefingEventsLine({ counts: { chat: 5 } }, null)).toBe(
      'events: 5 new — just state --section chat'
    );
    expect(briefingEventsLine({ counts: { chat: 7 } }, 5)).toBe(
      'events: 2 new — just state --section chat'
    );
  });

  test('an unchanged or absent feed costs no line', () => {
    expect(briefingEventsLine({ counts: { chat: 7 } }, 7)).toBe('');
    expect(briefingEventsLine({ counts: {} }, 7)).toBe('');
    expect(briefingEventsLine({ counts: { chat: true } }, null)).toBe('');
    expect(briefingEventsLine(null, null)).toBe('');
  });
});

describe('the flag matrix', () => {
  const refusal = (options: {
    end: boolean;
    awaitPhase: boolean;
    brief: boolean;
    decisions: boolean;
  }): string => {
    const outcome = Effect.runSync(Effect.either(checkTurnFlags(options)));
    if (outcome._tag !== 'Left') throw new Error('expected a refusal');
    return outcome.left.message;
  };

  test('--await without --end names the form that works', () => {
    expect(
      refusal({ end: false, awaitPhase: true, brief: false, decisions: false })
    ).toBe(AWAIT_WITHOUT_END);
    expect(AWAIT_WITHOUT_END).toContain('just turn --end --await');
  });

  test('--brief without a wake names the whole one-call form', () => {
    for (const options of [
      { end: true, awaitPhase: false, brief: true, decisions: false },
      { end: false, awaitPhase: false, brief: true, decisions: false },
    ]) {
      expect(refusal(options)).toBe(BRIEF_WITHOUT_WAKE);
    }
    expect(BRIEF_WITHOUT_WAKE).toContain('just turn --end --await --brief');
  });

  test('--decisions refuses to double as the phase end', () => {
    expect(refusal({ end: true, awaitPhase: false, brief: false, decisions: true })).toBe(
      DECISIONS_WITH_END
    );
    expect(DECISIONS_WITH_END).toContain('before `just turn --end`');
  });

  test('every legal combination passes', () => {
    for (const options of [
      { end: false, awaitPhase: false, brief: false, decisions: false },
      { end: false, awaitPhase: false, brief: false, decisions: true },
      { end: true, awaitPhase: false, brief: false, decisions: false },
      { end: true, awaitPhase: true, brief: false, decisions: false },
      { end: true, awaitPhase: true, brief: true, decisions: false },
    ]) {
      expect(Effect.runSync(Effect.either(checkTurnFlags(options)))._tag).toBe('Right');
    }
  });
});
