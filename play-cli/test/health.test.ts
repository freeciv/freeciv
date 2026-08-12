/**
 * The `health` block and the `health` command.
 *
 * Ports `test_v2_health_and_join_render_compact_cards` (test_client.py:1560),
 * `test_v2_health_accepts_and_renders_standing_and_waiting_on` (3954),
 * `test_v2_health_rejects_bad_standing_and_waiting_on_shapes` (3992),
 * `test_v2_health_rejects_controller_identity_mismatch` (4051),
 * `test_v2_health_accepts_non_actionable_terminalizing_handoff` (4070),
 * the render half of `test_v2_health_survives_an_additive_server_field` (7104)
 * and `test_health_leads_with_whose_turn_it_is_and_names_them` (11432).
 *
 * The five cases the unit brief names — no phase, your phase active, another
 * seat holding with a deadline, game over, and a phase that ended by timeout —
 * each get a whole-block golden, because a line that moves between commands is
 * the failure this unit exists to prevent.
 *
 * `--json` is asserted at the byte level against goldens CPython produced (see
 * `JSON_GOLDEN` and NOTES §10.4): re-parsing both sides cannot see a
 * serialization divergence, and the supervisor's timings are Python floats.
 * The mirror write `_mirror_health` makes (client.py:6552) is asserted off
 * disk, because it is the only observable this command has besides stdout.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import { BLOCKED_NEXT_LINE, renderHealth } from 'src/render/health';
import { healthLines, runHealth } from 'src/commands/health.cmd';
import { decodeHealth, type HealthEnvelope, type JsonObject } from 'src/schema/index';
import { httpFor } from 'src/services/http';
import {
  HEALTH_FLOAT_PATHS,
  healthFloatPathsUnder,
  healthJsonText,
  healthPyValue,
  pyValueWithFloats,
  turnHealthContextPyValue,
} from 'src/services/health-json';
import { turnHealthContext } from 'src/services/health-context';
import { pyDumps } from 'src/services/canonical-body';
import { compactJson } from 'src/services/json-output';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, emptyV2ClientState, sessionStoreFor } from 'src/services/session-store';
import { V2Client, v2ClientFor } from 'src/services/v2-client';
import {
  FIXTURE_CONTROLLER,
  FIXTURE_GAME_ID,
  errorPayload,
  fakeFetch,
  healthPayload,
  identity,
  scratchWorkspace,
  sessionFile,
  type Scratch,
} from 'test/_fixtures';
import {
  PHASE_HEADLINE,
  phaseHealthPayload,
  priorEndPayload,
} from 'test/_fixtures/phase-goldens';

const scratches: Scratch[] = [];

afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

const decode = (payload: JsonObject): HealthEnvelope =>
  Effect.runSync(decodeHealth(payload, identity()));

const IDENTITY_LINE = `seat 1 Alice (${FIXTURE_CONTROLLER})`;
const SIDECAR = 'obs yes legal yes | sidecar ready gen 3';

/** One `last_phase_end` row, with the three floats the journal carries. */
const LAST_PHASE_END: JsonObject = {
  sequence: 9,
  turn: 7,
  phase: 1,
  place: 1,
  seat_id: 'seat_one',
  player_name: 'Alice',
  player_color: '#0067A5',
  controller_label: FIXTURE_CONTROLLER,
  controller_type: 'external',
  source: 'timeout',
  receipt_state: 'applied',
  resolution: 'advanced',
  deadline_started_at: 1000.0,
  ended_at: 1180.0,
  elapsed_s: 180.0,
};

/**
 * What `python3 client.py health --json` prints, byte for byte.
 *
 * Produced by running `json.dumps(validated, sort_keys=True,
 * separators=(",", ":"))` in CPython over the dict `_validate_health` builds
 * from these same fixture payloads.  The `.0` suffixes are the point: the
 * supervisor's timings are Python floats (`V2_TIMING_MODE_TIMEOUTS["default"]
 * = 600.0`, `V2_AUTO_END_IDLE_GRACE_S = 20.0`), and a `--json` consumer diffing
 * the two clients sees every one of them.
 */
const JSON_GOLDEN = {
  simple:
    '{"agent":{"agent_id":"agent_0123456789abcdef","controller_label":"codex-gpt-5.6-sol"},' +
    '"control_protocol":"full-control-v2","game_id":"game_Hsit9YEuBjKdJPPouFoGVYlk",' +
    '"game_state":"running","last_phase_end":null,"legal_actions_available":true,' +
    '"observation_available":true,"phase":{"active":true,"phase":1,"state":"awaiting_agent",' +
    '"timing":{"deadline_at":1600.0,"deadline_started_at":1000.0,"elapsed_s":13.0,' +
    '"mode":"default","remaining_s":587.0,"timeout_s":600.0},"turn":3},"schema_version":2,' +
    '"seat":{"place":1,"player_name":"Alice","seat_id":"seat_one"},' +
    '"sidecar":{"generation":3,"state":"ready"}}',
  full:
    '{"agent":{"agent_id":"agent_0123456789abcdef","controller_label":"codex-gpt-5.6-sol"},' +
    '"control_protocol":"full-control-v2","game_id":"game_Hsit9YEuBjKdJPPouFoGVYlk",' +
    '"game_state":"running","last_phase_end":{"controller_label":"codex-gpt-5.6-sol",' +
    '"controller_type":"external","deadline_started_at":1000.0,"elapsed_s":180.0,' +
    '"ended_at":1180.0,"phase":1,"place":1,"player_color":"#0067A5","player_name":"Alice",' +
    '"receipt_state":"applied","resolution":"advanced","seat_id":"seat_one","sequence":9,' +
    '"source":"timeout","turn":7},"legal_actions_available":true,"observation_available":true,' +
    '"phase":{"active":true,"auto_end":{"armed":true,"enabled":true,"grace_s":20.0,' +
    '"remaining_s":12.0},"phase":1,"prior_end":{"controller_label":"pi-gpt-5.6-sol",' +
    '"elapsed_s":600.0,"orders_submitted":0,"phase":0,"place":2,"player_name":"AgentPlace2",' +
    '"receipt_state":"applied","resolution":"advanced","seat_id":"place-2","source":"timeout",' +
    '"turn":3},"state":"awaiting_agent","timing":{"deadline_at":1600.0,' +
    '"deadline_started_at":1000.0,"elapsed_s":13.0,"mode":"default","remaining_s":587.0,' +
    '"timeout_s":600.0},"turn":3},"schema_version":2,' +
    '"seat":{"place":1,"player_name":"Alice","seat_id":"seat_one"},' +
    '"sidecar":{"generation":3,"state":"ready"}}',
} as const;

// ---------------------------------------------------------------------------
// The block
// ---------------------------------------------------------------------------

describe('renderHealth', () => {
  test('your own turn is two lines, and the first one answers "can I act"', () => {
    const lines = renderHealth(decode(phaseHealthPayload({ mine: true, remainingS: 592, elapsedS: 8 })));
    expect(lines).toEqual([
      `health running | ${PHASE_HEADLINE.yourTurn} | ${SIDECAR}`,
      IDENTITY_LINE,
    ]);
    expect(lines.join('\n')).not.toContain('state_token');
  });

  test('another seat holding it is named, and the block says how long it has been blocked', () => {
    const lines = renderHealth(decode(phaseHealthPayload({ mine: false, remainingS: 13, elapsedS: 587 })));
    expect(lines).toEqual([
      `health running | ${PHASE_HEADLINE.holder} | ${SIDECAR}`,
      IDENTITY_LINE,
      'waiting on other_seat: Seat 2 AgentPlace2 (pi-gpt-5.6-sol) holds turn 3 phase 1 ' +
        'and has not ended it. (blocked 9m47s)',
    ]);
  });

  test('a terminal game has no phase, and the block says so rather than going quiet', () => {
    const lines = renderHealth(decode(phaseHealthPayload({ mine: false, gameState: 'completed' })));
    expect(lines).toEqual([
      `health completed | ${PHASE_HEADLINE.none} | obs no legal no | sidecar ready gen 3`,
      IDENTITY_LINE,
    ]);
  });

  test('a standing that is not "active" is disclosed; "active" is not noise', () => {
    const surrendered = renderHealth(
      decode(
        healthPayload({
          seat: { place: 1, seat_id: 'seat_one', player_name: 'Alice', standing: 'surrendered' },
        })
      )
    );
    expect(surrendered[1]).toBe(`${IDENTITY_LINE} | standing surrendered`);
    const active = renderHealth(
      decode(
        healthPayload({
          seat: { place: 1, seat_id: 'seat_one', player_name: 'Alice', standing: 'active' },
        })
      )
    );
    expect(active[1]).toBe(IDENTITY_LINE);
  });

  test('a blocked seat gets the waiting_on kind, its summary and a legible block duration', () => {
    const lines = renderHealth(
      decode(
        healthPayload({
          phase: {
            state: 'native_phase',
            turn: 5,
            phase: 0,
            active: false,
            timing: {
              mode: 'default',
              timeout_s: 60,
              deadline_started_at: 1000,
              deadline_at: 1060,
              elapsed_s: 4,
              remaining_s: 56,
            },
            waiting_on: {
              kind: 'seat_not_ready',
              summary: 'fixedlength holds native turn-done',
              // CPython's round() is half-to-even: 12.5 seconds is "12s".
              waiting_s: 12.5,
              seats: [],
            },
          },
        })
      )
    );
    expect(lines).toContain(
      'waiting on seat_not_ready: fixedlength holds native turn-done (blocked 12s)'
    );
  });

  test('the evaluation triple prints as objective and a turn budget', () => {
    const evaluation = { objective: 'Maximize final score', max_turns: 5000, turns_remaining: 4997 };
    const health = Effect.runSync(
      decodeHealth({ ...phaseHealthPayload({ mine: true }), ...evaluation }, identity({ evaluation }))
    );
    expect(renderHealth(health)[1]).toBe(
      `${IDENTITY_LINE} | objective Maximize final score | turns 4997/5000 remaining`
    );
  });

  test('an armed auto-end says when the phase goes without you', () => {
    const armed = renderHealth(
      decode(
        phaseHealthPayload({
          mine: true,
          autoEnd: { enabled: true, armed: true, grace_s: 30, remaining_s: 12 },
        })
      )
    );
    expect(armed).toContain(
      'auto-end armed: nothing idle and no decision pending, this phase ends in 12s unless you act'
    );
    const disarmed = renderHealth(
      decode(
        phaseHealthPayload({
          mine: true,
          autoEnd: { enabled: true, armed: false, grace_s: 30, remaining_s: 12 },
        })
      )
    );
    expect(disarmed.join('\n')).not.toContain('auto-end armed');
  });

  test('the last phase end names its source, and auto_idle explains itself', () => {
    const event = {
      sequence: 9,
      turn: 7,
      phase: 1,
      place: 1,
      seat_id: 'seat_one',
      player_name: 'Alice',
      player_color: '#0067A5',
      controller_label: FIXTURE_CONTROLLER,
      controller_type: 'external',
      source: 'timeout',
      receipt_state: 'applied',
      resolution: 'advanced',
      deadline_started_at: 1000.0,
      ended_at: 1180.25,
      elapsed_s: 180.25,
    };
    const timedOut = renderHealth(
      decode({ ...phaseHealthPayload({ mine: false }), last_phase_end: event })
    );
    expect(timedOut).toContain('last phase end t7/p1 source=timeout applied advanced 180.25s');
    const autoIdle = renderHealth(
      decode({
        ...phaseHealthPayload({ mine: false }),
        last_phase_end: { ...event, source: 'auto_idle' },
      })
    );
    expect(autoIdle).toContain(
      'last phase end t7/p1 source=auto_idle applied advanced 180.25s ' +
        '(ended for you: nothing idle, no decision pending)'
    );
  });

  test('a phase that ended by timeout with no orders says the opponent was not there', () => {
    const lines = renderHealth(
      decode(
        phaseHealthPayload({
          mine: true,
          remainingS: 592,
          elapsedS: 8,
          priorEnd: priorEndPayload('timeout', 0),
        })
      )
    );
    expect(lines).toEqual([
      `health running | ${PHASE_HEADLINE.yourTurn} | ${SIDECAR}`,
      IDENTITY_LINE,
      'opponent seat 2 AgentPlace2 (pi-gpt-5.6-sol) ended t3/p0 in 10m0s ' +
        '(timeout — they issued no orders)',
    ]);
  });

  test('a recovery that rewound applied actions says so and names the re-read', () => {
    const recovery = {
      attempt: 1,
      client_state: 'running',
      exit_code: null,
      exit_signal: null,
      format: 'freeciv-full-control-v2-recovery',
      game_id: FIXTURE_GAME_ID,
      kind: 'sidecar_reattach',
      outcome: 'recovered',
      place: 1,
      recovered_to_turn: 51,
      rewound_applied_actions: true,
      schema_version: 1,
      seat_id: 'seat_one',
      sidecar_generation: 2,
      timestamp: '2026-08-05T09:49:19.508Z',
      trigger: 'sidecar_exit',
      turn: 52,
    };
    const rendered = renderHealth(
      decode({ ...phaseHealthPayload({ mine: true }), last_recovery: recovery })
    ).join('\n');
    expect(rendered).toContain(
      'last recovery t52 sidecar_reattach recovered trigger=sidecar_exit rewound to t51 ' +
        '— applied actions were rolled back; re-read `just turn`'
    );
    // An absent or null `last_recovery` prints no line at all.
    expect(
      renderHealth(decode({ ...phaseHealthPayload({ mine: true }), last_recovery: null })).join('\n')
    ).not.toContain('last recovery');
  });
});

// ---------------------------------------------------------------------------
// The remedy line
// ---------------------------------------------------------------------------

describe('the blocked-seat remedy', () => {
  test('a seat that is waiting on somebody is told the one command that helps', () => {
    const lines = healthLines(decode(phaseHealthPayload({ mine: false })));
    expect(lines.at(-1)).toBe(BLOCKED_NEXT_LINE);
    expect(BLOCKED_NEXT_LINE).toContain('just wait --for-turn');
    expect(BLOCKED_NEXT_LINE).toContain('exit 0 = go, 75 = still theirs');
  });

  test('a seat holding its own phase is told nothing extra', () => {
    const lines = healthLines(decode(phaseHealthPayload({ mine: true })));
    expect(lines).toEqual(renderHealth(decode(phaseHealthPayload({ mine: true }))));
  });

  test('a blocked phase with no nameable holder gets no remedy either', () => {
    const lines = healthLines(decode(phaseHealthPayload({ mine: false, waitingSeats: [] })));
    expect(lines.join('\n')).not.toContain('just wait --for-turn');
  });
});

// ---------------------------------------------------------------------------
// The command
// ---------------------------------------------------------------------------

interface Fixture {
  readonly layer: Layer.Layer<PrivateFs | SessionStore | V2Client>;
  /** `state/…` under the session file's own mirror directory. */
  readonly mirror: (...parts: ReadonlyArray<string>) => string;
}

const commandFixture = (payload: JsonObject, status = 200): Fixture => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const target = path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID, 'seat.json');
  Effect.runSync(scratch.files.writeJson(target, sessionFile()));
  const store = sessionStoreFor(
    scratch.workspace,
    scratch.files,
    {
      empty: emptyV2ClientState,
      validate: () => Effect.void,
      cursorExpired: () => false,
    },
    {}
  );
  const client = v2ClientFor(
    httpFor(fakeFetch(new Map([['/health', { status, body: payload }]]))),
    () => Effect.void
  );
  return {
    layer: Layer.mergeAll(
      Layer.succeed(SessionStore, store),
      Layer.succeed(V2Client, client),
      Layer.succeed(PrivateFs, scratch.files)
    ),
    mirror: (...parts) =>
      path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID, 'seat', 'state', ...parts),
  };
};

const capture = async <E>(
  effect: Effect.Effect<void, E, PrivateFs | SessionStore | V2Client>,
  fixture: Fixture
): Promise<ReadonlyArray<string>> => {
  const out: string[] = [];
  const original = console.log;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
  try {
    await Effect.runPromise(Effect.provide(effect, fixture.layer));
    return out;
  } finally {
    console.log = original;
  }
};

describe('play health', () => {
  test('it prints the rendered block and nothing else', async () => {
    const payload = phaseHealthPayload({ mine: true, remainingS: 592, elapsedS: 8 });
    const fixture = commandFixture(payload);
    const out = await capture(runHealth({ session: '', json: false }), fixture);
    expect(out).toEqual([
      `health running | ${PHASE_HEADLINE.yourTurn} | ${SIDECAR}`,
      IDENTITY_LINE,
    ]);
  });

  test('a blocked seat gets the block plus the remedy, in that order', async () => {
    const payload = phaseHealthPayload({ mine: false, remainingS: 13, elapsedS: 587 });
    const fixture = commandFixture(payload);
    const out = await capture(runHealth({ session: '', json: false }), fixture);
    expect(out[0]).toBe(`health running | ${PHASE_HEADLINE.holder} | ${SIDECAR}`);
    expect(out.at(-1)).toBe(BLOCKED_NEXT_LINE);
  });

  test('--json prints the validated envelope, not the raw body', async () => {
    const payload = phaseHealthPayload({ mine: true });
    const fixture = commandFixture(payload);
    const out = await capture(runHealth({ session: '', json: true }), fixture);
    expect(out).toHaveLength(1);
    expect(JSON.parse(out[0] ?? '')).toEqual(
      JSON.parse(JSON.stringify(decode(payload))) as unknown
    );
    // The envelope the server sent carries no agent_id at the top level, and the
    // validated one does not invent it.
    expect(out[0]).not.toContain('state_token');
  });

  test('--json is byte-identical to json.dumps(sort_keys, separators)', async () => {
    const fixture = commandFixture(phaseHealthPayload({ mine: true }));
    const out = await capture(runHealth({ session: '', json: true }), fixture);
    expect(out[0]).toBe(JSON_GOLDEN.simple);
  });

  test('--json keeps every wire float a float, on all twelve timing fields', async () => {
    const fixture = commandFixture({
      ...phaseHealthPayload({
        mine: true,
        autoEnd: { enabled: true, armed: true, grace_s: 20.0, remaining_s: 12.0 },
        priorEnd: priorEndPayload('timeout', 0),
      }),
      last_phase_end: LAST_PHASE_END,
    });
    const out = await capture(runHealth({ session: '', json: true }), fixture);
    expect(out[0]).toBe(JSON_GOLDEN.full);
  });

  test('it refreshes the state mirror between validating and rendering', async () => {
    const payload = phaseHealthPayload({ mine: false, remainingS: 13, elapsedS: 587 });
    const fixture = commandFixture(payload);
    await capture(runHealth({ session: '', json: false }), fixture);
    // `_mirror_health` writes exactly these two files, and `show`'s header and
    // `wait`'s cached-phase note read them back.
    const header = fs.readFileSync(fixture.mirror('header.txt'), 'utf8');
    expect(header).toContain('phase     awaiting_agent · turn 3 phase 1 · active no');
    expect(header).toContain('game_state running · observation yes · legal_actions yes');
    const marker: unknown = JSON.parse(fs.readFileSync(fixture.mirror('phase.json'), 'utf8'));
    expect(marker).toMatchObject({ turn: 3, phase: 1, active: false, state: 'awaiting_agent' });
  });

  test('--json refreshes the mirror too — the write is not part of the render', async () => {
    const fixture = commandFixture(phaseHealthPayload({ mine: true }));
    await capture(runHealth({ session: '', json: true }), fixture);
    expect(fs.existsSync(fixture.mirror('header.txt'))).toBe(true);
    expect(fs.existsSync(fixture.mirror('phase.json'))).toBe(true);
  });

  test('a drifted controller label is a refusal, not a rendered line', async () => {
    const payload = {
      ...phaseHealthPayload({ mine: true }),
      agent: { agent_id: identity().agentId, controller_label: 'claude-other-model' },
    };
    const fixture = commandFixture(payload);
    const outcome = await Effect.runPromise(
      Effect.either(Effect.provide(runHealth({ session: '', json: false }), fixture.layer))
    );
    expect(Either.isLeft(outcome)).toBe(true);
    if (Either.isLeft(outcome)) {
      expect(outcome.left.message).toContain('health agent identity');
    }
  });

  test('a non-2xx body is raised as a validated refusal', async () => {
    const fixture = commandFixture(errorPayload(), 404);
    const outcome = await Effect.runPromise(
      Effect.either(Effect.provide(runHealth({ session: '', json: false }), fixture.layer))
    );
    expect(Either.isLeft(outcome)).toBe(true);
    if (Either.isLeft(outcome)) {
      expect(outcome.left._tag).toBe('V2ResponseError');
    }
  });
});

// ---------------------------------------------------------------------------
// The serializer underneath --json
// ---------------------------------------------------------------------------

describe('the float half of --json', () => {
  test('every field the supervisor sends as a float is marked, and no other', () => {
    const text = healthJsonText(
      decode({
        ...phaseHealthPayload({
          mine: true,
          autoEnd: { enabled: true, armed: true, grace_s: 20.0, remaining_s: 12.0 },
          priorEnd: priorEndPayload('timeout', 0),
        }),
        last_phase_end: LAST_PHASE_END,
      })
    );
    for (const marked of [
      '"timeout_s":600.0',
      '"deadline_started_at":1000.0',
      '"deadline_at":1600.0',
      '"elapsed_s":13.0',
      '"remaining_s":587.0',
      '"grace_s":20.0',
      '"remaining_s":12.0',
      '"elapsed_s":600.0',
      '"ended_at":1180.0',
      '"elapsed_s":180.0',
    ]) {
      expect(text).toContain(marked);
    }
    // The counts, ids and turn numbers are Python ints and must not grow a `.0`.
    for (const plain of [
      '"schema_version":2',
      '"turn":3',
      '"phase":1',
      '"place":1',
      '"generation":3',
      '"sequence":9',
      '"orders_submitted":0',
    ]) {
      expect(text).toContain(plain);
    }
  });

  test('the waiting_on block\u2019s waiting_s is a float, and its seats survive', () => {
    const text = healthJsonText(decode(phaseHealthPayload({ mine: false, elapsedS: 587 })));
    expect(text).toContain('"waiting_s":587.0');
    expect(text).toContain('"is_self":false');
  });

  test('a fractional timing keeps CPython\u2019s shortest repr, not String(number)', () => {
    const text = healthJsonText(
      decode(phaseHealthPayload({ mine: true, elapsedS: 12.5, remainingS: 587.004 }))
    );
    expect(text).toContain('"elapsed_s":12.5');
    expect(text).toContain('"remaining_s":587.004');
  });

  test('a terminal envelope has no phase and therefore no float at all', () => {
    const text = healthJsonText(decode(phaseHealthPayload({ mine: false, gameState: 'completed' })));
    expect(text).toContain('"phase":null');
    expect(text).not.toContain('.0');
  });
});

// ---------------------------------------------------------------------------
// The same twelve floats, everywhere the envelope is embedded
// ---------------------------------------------------------------------------

/**
 * `health` is almost never printed alone.
 *
 * `wait --json` prints it under `"health"` (client.py:2353), `turn --json`
 * prints `_turn_health_context`'s projection under `"context"`
 * (client.py:7535 and both `_turn_briefing_locked` payloads), and
 * `turn --end --await --brief --json` prints both inside `_composite_json`
 * (client.py:6914-6919).  Each of those is one `json.dumps` in CPython and one
 * `pyDumps` here, so each carries the same twelve floats — and a consumer that
 * reaches for core's `compactJson` instead reintroduces the divergence on its
 * own surface.  These tests are the shared truth U05/U12/U16 assert against.
 */
describe('the embedded projections', () => {
  /** The envelope that carries every one of the twelve floats. */
  const fullHealth = (): HealthEnvelope =>
    decode({
      ...phaseHealthPayload({
        mine: true,
        autoEnd: { enabled: true, armed: true, grace_s: 20.0, remaining_s: 12.0 },
        priorEnd: priorEndPayload('timeout', 0),
      }),
      last_phase_end: LAST_PHASE_END,
    });

  /**
   * `json.dumps(_turn_health_context(validated), sort_keys=True,
   * separators=(",", ":"))` over the same fixture, produced by CPython.
   *
   * It is `JSON_GOLDEN.full` minus the three header fields the projection drops
   * (`schema_version`, `control_protocol`, `game_id`) plus the three evaluation
   * fields it materializes as `null` — and with all twelve `.0` suffixes still
   * on, because `phase` and `last_phase_end` are copied in by reference.
   */
  const CONTEXT_GOLDEN =
    '{"agent":{"agent_id":"agent_0123456789abcdef","controller_label":"codex-gpt-5.6-sol"},' +
    '"game_state":"running","last_phase_end":{"controller_label":"codex-gpt-5.6-sol",' +
    '"controller_type":"external","deadline_started_at":1000.0,"elapsed_s":180.0,' +
    '"ended_at":1180.0,"phase":1,"place":1,"player_color":"#0067A5","player_name":"Alice",' +
    '"receipt_state":"applied","resolution":"advanced","seat_id":"seat_one","sequence":9,' +
    '"source":"timeout","turn":7},"legal_actions_available":true,"max_turns":null,' +
    '"objective":null,"observation_available":true,"phase":{"active":true,' +
    '"auto_end":{"armed":true,"enabled":true,"grace_s":20.0,"remaining_s":12.0},"phase":1,' +
    '"prior_end":{"controller_label":"pi-gpt-5.6-sol","elapsed_s":600.0,"orders_submitted":0,' +
    '"phase":0,"place":2,"player_name":"AgentPlace2","receipt_state":"applied",' +
    '"resolution":"advanced","seat_id":"place-2","source":"timeout","turn":3},' +
    '"state":"awaiting_agent","timing":{"deadline_at":1600.0,"deadline_started_at":1000.0,' +
    '"elapsed_s":13.0,"mode":"default","remaining_s":587.0,"timeout_s":600.0},"turn":3},' +
    '"seat":{"place":1,"player_name":"Alice","seat_id":"seat_one"},' +
    '"sidecar":{"generation":3,"state":"ready"},"turns_remaining":null}';

  test('turnHealthContext serialized through the projection is CPython byte for byte', () => {
    const context = turnHealthContext(fullHealth());
    expect(pyDumps(turnHealthContextPyValue(context), true)).toBe(CONTEXT_GOLDEN);
  });

  test('core’s compactJson over the same context is the divergence this guards', () => {
    const plain = compactJson(turnHealthContext(fullHealth()));
    // Seven-plus tokens on one line: every timing, both grace fields, prior_end
    // and the whole last_phase_end row.
    for (const marked of [
      '"timeout_s":600.0',
      '"deadline_started_at":1000.0',
      '"deadline_at":1600.0',
      '"elapsed_s":13.0',
      '"remaining_s":587.0',
      '"grace_s":20.0',
      '"remaining_s":12.0',
    ]) {
      expect(CONTEXT_GOLDEN).toContain(marked);
      expect(plain).not.toContain(marked);
    }
    expect(plain).not.toBe(CONTEXT_GOLDEN);
  });

  test('a whole turn --json payload marks the context in place', () => {
    const payload = {
      schema_version: 1,
      command: 'turn',
      status: 'terminal',
      context: turnHealthContext(fullHealth()),
      next_commands: ['just wait --for-turn'],
    };
    expect(pyDumps(pyValueWithFloats(payload, healthFloatPathsUnder('context')), true)).toBe(
      `{"command":"turn","context":${CONTEXT_GOLDEN},` +
        '"next_commands":["just wait --for-turn"],"schema_version":1,"status":"terminal"}'
    );
  });

  test('a whole wait --json envelope marks the health it embeds', () => {
    const health = fullHealth();
    const wait = {
      schema_version: 2,
      control_protocol: 'full-control-v2',
      game_id: FIXTURE_GAME_ID,
      agent_id: 'agent_0123456789abcdef',
      wake_reason: 'phase_active',
      health,
      state_revision: null,
    };
    const golden =
      `{"agent_id":"agent_0123456789abcdef","control_protocol":"full-control-v2",` +
      `"game_id":"${FIXTURE_GAME_ID}","health":${JSON_GOLDEN.full},"schema_version":2,` +
      '"state_revision":null,"wake_reason":"phase_active"}';
    expect(pyDumps(pyValueWithFloats(wait, healthFloatPathsUnder('health')), true)).toBe(golden);
    // The splice form U05 may prefer reaches the identical bytes.
    expect(pyDumps({ ...wait, health: healthPyValue(health) }, true)).toBe(golden);
  });

  test('the --brief composite marks both embeddings in one pass', () => {
    const health = fullHealth();
    const turn = {
      schema_version: 1,
      command: 'turn',
      status: 'terminal',
      context: turnHealthContext(health),
      next_commands: ['just wait --for-turn'],
    };
    const wait = { wake_reason: 'phase_active', health, state_revision: null };
    const composite = {
      schema_version: 1,
      command: 'turn',
      status: 'briefed',
      end: null,
      wait,
      turn,
      turn_error: null,
    };
    const text = pyDumps(
      pyValueWithFloats(composite, healthFloatPathsUnder('wait.health', 'turn.context')),
      true
    );
    expect(text).toBe(
      '{"command":"turn","end":null,"schema_version":1,"status":"briefed","turn":' +
        `{"command":"turn","context":${CONTEXT_GOLDEN},` +
        '"next_commands":["just wait --for-turn"],"schema_version":1,"status":"terminal"},' +
        `"turn_error":null,"wait":{"health":${JSON_GOLDEN.full},"state_revision":null,` +
        '"wake_reason":"phase_active"}}'
    );
  });

  test('re-rooting is a prefix and nothing else', () => {
    expect(healthFloatPathsUnder('')).toEqual(HEALTH_FLOAT_PATHS);
    expect(healthFloatPathsUnder('context').has('context.phase.timing.timeout_s')).toBe(true);
    expect(healthFloatPathsUnder('context').has('phase.timing.timeout_s')).toBe(false);
    expect(healthFloatPathsUnder('a', 'b').size).toBe(HEALTH_FLOAT_PATHS.size * 2);
    // A number at the *unprefixed* path is left alone, so a payload that
    // happens to carry `phase.timing.timeout_s` of its own is untouched.
    expect(
      pyDumps(pyValueWithFloats({ phase: { timing: { timeout_s: 600 } } }, healthFloatPathsUnder('x')), true)
    ).toBe('{"phase":{"timing":{"timeout_s":600}}}');
  });
});
