/**
 * `join` — the card, the conduct block, and the three writes.
 *
 * Ports `test_v2_health_and_join_render_compact_cards` (the card half),
 * `test_v2_join_card_and_state_header_carry_the_same_contract`,
 * `test_join_identity_defaults_come_from_playconfig`,
 * `test_join_reports_and_saves_exact_timing_contract`,
 * `test_join_rejects_controller_label_different_from_requested_name`,
 * `test_full_control_join_advertises_capability_and_never_prints_v1_loop`,
 * `test_full_control_join_rejects_unplayable_terminal_or_error_result`,
 * `test_join_binds_this_workspace_and_a_second_join_rebinds_it` and
 * `test_stale_invite_rejection_names_owner_recovery_command`.
 *
 * The invariant behind most of them: **nothing is written until the whole join
 * result has been proved**, and the bearer token never reaches stdout, stderr
 * or the seat-binding file.
 */
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Command } from '@effect/cli';
import { BunContext } from '@effect/platform-bun';
import { Effect, Either, Layer, Option } from 'effect';
import { FULL_CONTROL_V2, SEAT_BINDING_NAME } from 'src/constants';
import {
  V2_PROTOCOL_CARD,
  deadlineText,
  joinGuidance,
  renderJoin,
  seatBindingLine,
  timingModeText,
} from 'src/render/join';
import {
  applyPlayDefaults,
  commandJoin,
  joinCommand,
  type JoinArgs,
} from 'src/commands/join.cmd';
import { Http, httpLayer } from 'src/services/http';
import {
  PrivateFs,
  Workspace,
  privateFsFor,
  type WorkspacePaths,
} from 'src/services/private-fs';
import {
  SessionStore,
  emptyV2ClientState,
  sessionKey,
  sessionStoreFor,
} from 'src/services/session-store';
import type { JsonObject } from 'src/schema/primitives';
import type { SeatBinding } from 'src/services/session-store';
import { recordingFetch, type FakeRoute, type RecordedRequest } from 'test/_fixtures';

const GAME_ID = 'game_Hsit9YEuBjKdJPPouFoGVYlk';
const SECOND_ID = 'game_9SecondBoundGame00000000';
const CONTROLLER = 'codex-bind-model';
const BASE = 'http://127.0.0.1:8765';
const TOKEN = 'agent-v2-secret';

const roots: string[] = [];

afterEach(() => {
  while (roots.length > 0) {
    const root = roots.pop();
    if (root !== undefined) fs.rmSync(root, { recursive: true, force: true });
  }
});

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

const schema = {
  empty: emptyV2ClientState,
  validate: () => Effect.void,
  cursorExpired: (expiresAt: string | null): boolean =>
    expiresAt === null ? false : Date.parse(expiresAt) <= Date.now(),
};

interface Bench {
  readonly root: string;
  readonly workspace: WorkspacePaths;
  readonly stage: (gameId: string) => void;
  readonly layer: (
    fetchImpl: typeof fetch
  ) => Layer.Layer<Workspace | PrivateFs | SessionStore | Http>;
}

const bench = (): Bench => {
  const root = fs.realpathSync.native(fs.mkdtempSync(path.join(os.tmpdir(), 'play-cli-u02-')));
  roots.push(root);
  const workspace: WorkspacePaths = { root, stateRoot: path.join(root, '.sessions') };
  const files = privateFsFor(workspace);
  const store = sessionStoreFor(workspace, files, schema, {});
  return {
    root,
    workspace,
    stage: (gameId) => {
      const invites = path.join(root, '.invites');
      fs.mkdirSync(invites, { mode: 0o700, recursive: true });
      const target = path.join(invites, `${gameId}.json`);
      fs.writeFileSync(
        target,
        JSON.stringify({
          schema_version: 1,
          game_id: gameId,
          service_url: BASE,
          join_token: 'join-secret',
        }),
        'utf8'
      );
      fs.chmodSync(target, 0o600);
    },
    layer: (fetchImpl) =>
      Layer.mergeAll(
        Layer.succeed(Workspace, workspace),
        Layer.succeed(PrivateFs, files),
        Layer.succeed(SessionStore, store),
        httpLayer(fetchImpl)
      ),
  };
};

interface Captured {
  readonly out: string;
  readonly err: string;
  readonly requests: ReadonlyArray<RecordedRequest>;
}

const v2Result = (
  gameId: string,
  controller: string,
  overrides: JsonObject = {}
): JsonObject => {
  const prefix = `${BASE}/v2/games/${gameId}/me`;
  return {
    game_id: gameId,
    agent_id: 'agent-v2',
    agent_token: TOKEN,
    place: 1,
    seat_id: 'place-1',
    player_name: 'AgentPlace1',
    controller_label: controller,
    controller_metadata: {},
    controller_fingerprint: 'f'.repeat(64),
    control_protocol: FULL_CONTROL_V2,
    supported_control_protocols: [FULL_CONTROL_V2],
    objective: 'Win by the configured evaluation objective.',
    max_turns: 321,
    turns_remaining: null,
    v2_transport_available: true,
    health_url: `${prefix}/health`,
    state_url: `${prefix}/state`,
    legal_actions_url: `${prefix}/legal-actions`,
    batches_url: `${prefix}/batches`,
    receipts_url: `${prefix}/receipts/{batch_id}`,
    wait_url: `${prefix}/wait`,
    openapi_url: `${BASE}/v2/openapi.json`,
    state: 'running',
    timing_mode: 'default',
    action_timeout_s: 600,
    ...overrides,
  };
};

const args = (overrides: Partial<JoinArgs> = {}): JoinArgs => ({
  gameId: GAME_ID,
  name: CONTROLLER,
  place: '',
  invite: '',
  joinToken: '',
  json: false,
  ...overrides,
});

/** Run one join with the supervisor answering `health`, `status` and `join`. */
const join = async (
  fixture: Bench,
  plan: {
    readonly status?: FakeRoute;
    readonly join: FakeRoute;
  },
  overrides: Partial<JoinArgs> = {}
): Promise<{ readonly result: Either.Either<void, { readonly message: string }>; readonly captured: Captured }> => {
  const recorder = recordingFetch(
    new Map<string, FakeRoute>([
      ['/join', plan.join],
      ['/status', plan.status ?? { body: {} }],
      ['/health', { body: {} }],
    ])
  );
  const out: string[] = [];
  const err: string[] = [];
  const originalLog = console.log;
  const originalError = console.error;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
  console.error = (...parts: ReadonlyArray<unknown>) => err.push(parts.join(' '));
  try {
    const result = await Effect.runPromise(
      Effect.either(
        Effect.provide(commandJoin(args(overrides), {}), fixture.layer(recorder.fetch))
      )
    );
    return {
      result,
      captured: { out: out.join('\n'), err: err.join('\n'), requests: recorder.requests },
    };
  } finally {
    console.log = originalLog;
    console.error = originalError;
  }
};

const failure = <A>(either: Either.Either<A, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

const ok = <A, E>(either: Either.Either<A, E>): void => {
  if (Either.isLeft(either)) {
    throw new Error(`expected success, got ${JSON.stringify(either.left)}`);
  }
};

const sessionFilesOf = (fixture: Bench, gameId: string): ReadonlyArray<string> => {
  const directory = path.join(fixture.workspace.stateRoot, gameId);
  try {
    return fs
      .readdirSync(directory)
      .filter((name) => name.endsWith('.json'))
      .map((name) => path.join(directory, name));
  } catch {
    return [];
  }
};

const readJson = (target: string): JsonObject =>
  JSON.parse(fs.readFileSync(target, 'utf8')) as JsonObject;

const binding = (gameId: string): Option.Option<SeatBinding> =>
  Option.some({ gameId, session: '/x', relative: 'x', boundAt: '' });

// ---------------------------------------------------------------------------
// The protocol card
// ---------------------------------------------------------------------------

describe('the one protocol card', () => {
  test('it opens on the alias dialect and the wire contract', () => {
    expect(V2_PROTOCOL_CARD[0]?.startsWith('ALIASES')).toBe(true);
    expect(V2_PROTOCOL_CARD[0]).toContain('dies with its revision');
    expect(V2_PROTOCOL_CARD[0]).toContain("the wire carries the server's opaque ID");
    expect(V2_PROTOCOL_CARD[1]?.startsWith('ERRORS carry their own remedy')).toBe(true);
  });

  test('it never leaks a state token', () => {
    expect(V2_PROTOCOL_CARD.join('\n')).not.toContain('state_token');
  });

  test('it is exactly the twenty lines join and state/header.txt share', () => {
    expect(V2_PROTOCOL_CARD).toHaveLength(20);
  });
});

// ---------------------------------------------------------------------------
// _render_join
// ---------------------------------------------------------------------------

const card = (overrides: JsonObject = {}, result: JsonObject = { state: 'running' }) =>
  renderJoin(
    {
      game_id: GAME_ID,
      controller_label: 'codex-test-model',
      place: 1,
      player_name: 'AgentPlace1',
      control_protocol: FULL_CONTROL_V2,
      timing_mode: 'default',
      action_timeout_s: 180,
      ...overrides,
    },
    result
  );

describe('the join card', () => {
  test('the head line names the seat, the protocol, the state and the timing', () => {
    const lines = card({
      objective: 'Maximize final score',
      max_turns: 5000,
      turns_remaining: null,
    });
    expect(lines[0]).toBe(
      `joined ${GAME_ID} as codex-test-model | seat 1 AgentPlace1 | ` +
        'proto full-control-v2 | state running | timing default 180s per turn'
    );
  });

  test('the second line is the binding, and no session path is ever printed', () => {
    const lines = card();
    expect(lines[1]).toBe(
      `this workspace is now playing ${GAME_ID} — commands need no --session`
    );
    expect(lines.join('\n')).not.toContain('codex-test.json');
    expect(lines.join('\n')).not.toContain('.sessions');
  });

  test('the evaluation frame prints only when the seat carries one', () => {
    expect(
      card({ objective: 'Maximize final score', max_turns: 5000, turns_remaining: null })[2]
    ).toBe('objective Maximize final score | max_turns 5000 | turns_remaining -');
    expect(card()[2]?.startsWith('objective ')).toBe(false);
  });

  test('a timeout-free seat says so instead of printing a null', () => {
    expect(card({ timing_mode: 'infinite', action_timeout_s: null })[0]).toContain(
      'timing infinite no deadline'
    );
  });

  test('a full-control-v2 seat gets the whole card and the header pointer', () => {
    const lines = card();
    for (const line of V2_PROTOCOL_CARD) expect(lines).toContain(line);
    expect(lines.some((line) => line.includes('state/header.txt'))).toBe(true);
    expect(lines.some((line) => line.includes('just turn'))).toBe(true);
    expect(lines.some((line) => line.includes('--json'))).toBe(true);
    expect(lines.join('\n')).not.toContain('agent_token');
  });

  test('a strategic-v1 seat never sees the v2 card', () => {
    const lines = card({ control_protocol: 'strategic-v1' }, { state: 'lobby' });
    expect(lines).not.toContain(V2_PROTOCOL_CARD[0] as string);
    expect(lines).toContain('PROTOCOL strategic-v1 — poll a turn, submit one action.');
    expect(lines).toContain('  just next --after_turn LAST_TURN');
    expect(lines).toContain('  just act --turn TURN --observation_id ID --action JSON');
  });

  test('a rebind says which game it left, once', () => {
    expect(card({}, { state: 'running' })[1]).not.toContain('rebound');
    expect(
      renderJoin(
        { game_id: GAME_ID, controller_label: 'x', control_protocol: 'strategic-v1' },
        {},
        binding(SECOND_ID)
      )[1]
    ).toBe(
      `this workspace is now playing ${GAME_ID}, rebound from ${SECOND_ID} — ` +
        'commands need no --session'
    );
  });
});

describe('_seat_binding_line', () => {
  test('a fresh binding, a cross-game rebind and a same-game rebind read differently', () => {
    expect(seatBindingLine(GAME_ID)).toBe(
      `this workspace is now playing ${GAME_ID} — commands need no --session`
    );
    expect(seatBindingLine(GAME_ID, binding(SECOND_ID))).toContain(
      `rebound from ${SECOND_ID}`
    );
    expect(seatBindingLine(GAME_ID, binding(GAME_ID))).toContain(
      'rebound to another seat in the same game'
    );
  });
});

describe('the deadline sentence', () => {
  test('None, a number and a drifted value each have their own words', () => {
    expect(deadlineText(null)).toBe('no agent deadline');
    expect(deadlineText(600)).toBe('600 seconds per agent turn');
    expect(deadlineText(1.5)).toBe('1.5 seconds per agent turn');
    expect(deadlineText('soon')).toBe('deadline unavailable');
  });
});

// ---------------------------------------------------------------------------
// .playconfig.json
// ---------------------------------------------------------------------------

describe('_apply_play_defaults', () => {
  const identity = { gameId: '', name: '', place: '' };

  const write = (fixture: Bench, value: unknown): void => {
    fs.writeFileSync(
      path.join(fixture.root, '.playconfig.json'),
      typeof value === 'string' ? value : JSON.stringify(value),
      'utf8'
    );
  };

  const run = (fixture: Bench, given = identity) =>
    Effect.runSync(Effect.either(applyPlayDefaults(fixture.workspace, given)));

  test('a pre-configured workspace fills game, name and place', () => {
    const fixture = bench();
    write(fixture, { schema_version: 1, game_id: GAME_ID, name: CONTROLLER, place: 2 });
    expect(run(fixture)).toEqual(
      Either.right({ gameId: GAME_ID, name: CONTROLLER, place: '2' })
    );
  });

  test('explicit arguments always win', () => {
    const fixture = bench();
    write(fixture, { schema_version: 1, game_id: GAME_ID, name: CONTROLLER, place: 2 });
    expect(
      run(fixture, { gameId: SECOND_ID, name: 'pi-gpt-5.5', place: '' })
    ).toEqual(Either.right({ gameId: SECOND_ID, name: 'pi-gpt-5.5', place: '2' }));
  });

  test('a missing config leaves every argument untouched', () => {
    expect(run(bench())).toEqual(Either.right(identity));
  });

  test('a malformed config fails closed rather than guessing', () => {
    const fixture = bench();
    write(fixture, { schema_version: 1, game_id: 'nope', name: 'x', place: null });
    expect(failure(run(fixture))).toContain('.playconfig.json');
  });

  test('unparseable JSON names the file too', () => {
    const fixture = bench();
    write(fixture, '{');
    expect(failure(run(fixture))).toContain('invalid .playconfig.json:');
  });

  test('a name that is only CPython whitespace is a malformed config', () => {
    // `not raw["name"].strip()`.  `.trim()` calls `"\x1f"` non-blank and would
    // accept it as a controller name.
    for (const name of ['', '', '', ' \t\n']) {
      const fixture = bench();
      write(fixture, { schema_version: 1, game_id: GAME_ID, name, place: null });
      expect(failure(run(fixture))).toContain('.playconfig.json');
    }
  });

  test('an argument that is only CPython whitespace is still omitted', () => {
    const fixture = bench();
    write(fixture, { schema_version: 1, game_id: GAME_ID, name: CONTROLLER, place: 2 });
    expect(run(fixture, { gameId: '', name: '', place: '' })).toEqual(
      Either.right({ gameId: GAME_ID, name: CONTROLLER, place: '2' })
    );
  });

  test('invalid UTF-8 is invalid .playconfig.json, not U+FFFD', () => {
    // `read_text(encoding="utf-8")` raises `UnicodeDecodeError`, which
    // `except ValueError` turns into the refusal.  Node's `'utf8'` reader
    // would substitute and accept a config CPython refuses.
    const fixture = bench();
    fs.writeFileSync(
      path.join(fixture.root, '.playconfig.json'),
      Buffer.concat([
        Buffer.from(`{"schema_version":1,"game_id":"${GAME_ID}","name":"co`, 'utf8'),
        Buffer.from([0xff]),
        Buffer.from('dex","place":null}', 'utf8'),
      ])
    );
    expect(failure(run(fixture))).toContain('invalid .playconfig.json:');
  });

  test('place 0 and a boolean place are both refused', () => {
    for (const place of [0, true, 'two']) {
      const fixture = bench();
      write(fixture, { schema_version: 1, game_id: GAME_ID, name: CONTROLLER, place });
      expect(failure(run(fixture))).toContain('.playconfig.json');
    }
  });
});

// ---------------------------------------------------------------------------
// command_join, end to end
// ---------------------------------------------------------------------------

describe('a strategic-v1 join', () => {
  test('it reports and saves the exact timing contract', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const joined: JsonObject = {
      schema_version: 1,
      game_id: GAME_ID,
      agent_id: 'agent-test',
      agent_token: 'agent-private-secret',
      place: 1,
      seat_id: 'place-1',
      player_name: 'AgentPlace1',
      controller_label: CONTROLLER,
      controller_metadata: {},
      controller_fingerprint: 'f'.repeat(64),
      timing_mode: 'infinite',
      action_timeout_s: null,
    };
    const { result, captured } = await join(fixture, { join: { body: joined } });
    ok(result);
    expect(captured.out).not.toContain('agent-private-secret');
    expect(captured.err).not.toContain('agent-private-secret');
    expect(captured.err).toContain('Joined in infinite timing mode: no agent deadline');
    expect(captured.err).toContain('choose its action directly');

    const files = sessionFilesOf(fixture, GAME_ID);
    expect(files).toHaveLength(1);
    const session = readJson(files[0] as string);
    expect(session['timing_mode']).toBe('infinite');
    expect(session['action_timeout_s']).toBeNull();
    expect(session['control_protocol']).toBe('strategic-v1');
    expect(session['supported_control_protocols']).toEqual([]);

    const post = captured.requests.find((request) => request.method === 'POST');
    expect(post?.body).not.toContain('supported_control_protocols');
  });

  test('the session file is named for the controller and written 0600', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const { result } = await join(fixture, {
      join: {
        body: {
          game_id: GAME_ID,
          agent_id: 'agent-test',
          agent_token: 'agent-private-secret',
          controller_label: CONTROLLER,
        },
      },
    });
    ok(result);
    const expected = path.join(
      fixture.workspace.stateRoot,
      GAME_ID,
      `${sessionKey(CONTROLLER)}.json`
    );
    expect(fs.statSync(expected).mode & 0o777).toBe(0o600);
  });
});

describe('a join the supervisor answered wrongly', () => {
  const stagedJoin = async (
    body: JsonObject,
    status: FakeRoute = { body: {} }
  ): Promise<{ readonly fixture: Bench; readonly message: string }> => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const { result } = await join(fixture, { status, join: { body } });
    return { fixture, message: failure(result) };
  };

  test('a different controller label is refused, and nothing is written', async () => {
    const { fixture, message } = await stagedJoin({
      game_id: GAME_ID,
      agent_id: 'agent-test',
      agent_token: 'agent-private-secret',
      controller_label: 'claude-returned-model',
    });
    expect(message).toBe(
      'the join response controller label does not match the requested ' +
        'harness-model identity'
    );
    expect(fs.existsSync(fixture.workspace.stateRoot)).toBe(false);
  });

  test('an incomplete response is refused before anything is written', async () => {
    const { fixture, message } = await stagedJoin({ game_id: GAME_ID });
    expect(message).toBe('the supervisor returned an incomplete join response');
    expect(fs.existsSync(fixture.workspace.stateRoot)).toBe(false);
  });

  test('a response for another game is refused', async () => {
    const { message } = await stagedJoin({
      game_id: SECOND_ID,
      agent_id: 'a',
      agent_token: 'b',
      controller_label: CONTROLLER,
    });
    expect(message).toBe('the join response belongs to a different game');
  });

  test('a protocol switch between preflight and join is refused', async () => {
    const { message } = await stagedJoin(
      {
        game_id: GAME_ID,
        agent_id: 'a',
        agent_token: 'b',
        controller_label: CONTROLLER,
        control_protocol: 'strategic-v1',
      },
      { body: { control_protocol: FULL_CONTROL_V2 } }
    );
    expect(message).toBe('the join result changed the preflight control protocol');
  });

  test('an unsupported preflight protocol names the value it saw', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const { result } = await join(fixture, {
      status: { body: { control_protocol: 'full-control-v3' } },
      join: { body: {} },
    });
    expect(failure(result)).toBe(
      "game requires unsupported control protocol 'full-control-v3'"
    );
  });
});

describe('a full-control-v2 join', () => {
  const v2Plan = (overrides: JsonObject = {}) => ({
    status: { body: { control_protocol: FULL_CONTROL_V2 } },
    join: { body: v2Result(GAME_ID, CONTROLLER, overrides) },
  });

  test('it advertises the capability and never prints the v1 loop', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const { result, captured } = await join(fixture, v2Plan());
    ok(result);

    const post = captured.requests.find((request) => request.method === 'POST');
    expect(JSON.parse(post?.body ?? '{}')['supported_control_protocols']).toEqual([
      FULL_CONTROL_V2,
    ]);
    expect(captured.err).toContain('Do not use strategic');
    expect(captured.err).not.toContain('just next --session');
    expect(captured.err).toContain('LOBBY FIRST');
    expect(captured.err).toContain('pregame_nations');
    expect(captured.err).toContain('pregame_styles');
    expect(captured.err).toContain('pregame.configure');
    expect(captured.err).toContain('pregame.set_ready');
    expect(captured.err).toContain('Objective: Win by');
    expect(captured.err).toContain('321 maximum');
    expect(captured.err).toContain('remaining turns unavailable until native play starts.');
    expect(captured.err).not.toContain(TOKEN);

    const saved = readJson(sessionFilesOf(fixture, GAME_ID)[0] as string);
    expect(saved['control_protocol']).toBe(FULL_CONTROL_V2);
    expect(saved['objective']).toBe('Win by the configured evaluation objective.');
    expect(saved['max_turns']).toBe(321);
    expect(saved['turns_remaining']).toBeNull();
  });

  test('the conduct block leads with the capitalized binding sentence', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const { captured } = await join(fixture, v2Plan());
    expect(captured.err).toContain(
      `This workspace is now playing ${GAME_ID} — commands need no --session.`
    );
    expect(captured.err).toContain('Timing mode: default; 600 seconds per agent turn.');
  });

  for (const [name, override, expected] of [
    [
      'transport',
      { v2_transport_available: false, state: 'starting' },
      'the full-control-v2 transport did not become playable; stop and tell the game owner',
    ],
    [
      'terminal',
      { v2_transport_available: true, state: 'failed' },
      'the full-control-v2 transport did not become playable; stop and tell the game owner',
    ],
    [
      'error',
      { v2_transport_available: true, state: 'running', error: 'sidecar startup failed' },
      'the full-control-v2 transport did not become playable; stop and tell the game owner',
    ],
    [
      'availability',
      { v2_transport_available: 'yes' },
      'the v2 join result omitted transport availability',
    ],
    [
      'protocol',
      { supported_control_protocols: [] },
      'the v2 join result omitted the negotiated protocol',
    ],
  ] as ReadonlyArray<readonly [string, JsonObject, string]>) {
    test(`an unplayable result (${name}) is refused and writes nothing`, async () => {
      const fixture = bench();
      fixture.stage(GAME_ID);
      const { result } = await join(fixture, v2Plan(override));
      expect(failure(result)).toBe(expected);
      expect(fs.existsSync(fixture.workspace.stateRoot)).toBe(false);
    });
  }

  test('a cross-origin endpoint is refused by name', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const { result } = await join(
      fixture,
      v2Plan({ state_url: 'http://evil.test/v2/state' })
    );
    expect(failure(result)).toBe('the v2 join result has an invalid same-origin state_url');
  });

  test('a malformed evaluation frame is refused before the transport checks', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const { result } = await join(fixture, v2Plan({ max_turns: 0 }));
    expect(failure(result)).toBe('invalid v2 join result: evaluation context is malformed');
  });
});

describe('the workspace binding', () => {
  test('a join binds this workspace and a second join rebinds it', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    fixture.stage(SECOND_ID);

    const first = await join(fixture, {
      status: { body: { control_protocol: FULL_CONTROL_V2 } },
      join: { body: v2Result(GAME_ID, CONTROLLER) },
    });
    ok(first.result);
    expect(first.captured.out).toContain(
      `this workspace is now playing ${GAME_ID} — commands need no --session`
    );
    // The path an agent would otherwise re-type is not printed.
    expect(first.captured.out).not.toContain('.sessions');
    expect(first.captured.out).not.toContain(fixture.root);
    expect(first.captured.err).not.toContain('Session file:');

    const bindingPath = path.join(fixture.workspace.stateRoot, SEAT_BINDING_NAME);
    expect(fs.statSync(bindingPath).mode & 0o777).toBe(0o600);
    const text = fs.readFileSync(bindingPath, 'utf8');
    expect(text).not.toContain(TOKEN);
    const saved = JSON.parse(text) as JsonObject;
    const sessionName = path.basename(sessionFilesOf(fixture, GAME_ID)[0] as string);
    expect(saved['game_id']).toBe(GAME_ID);
    expect(saved['session']).toBe(`${GAME_ID}/${sessionName}`);
    expect(saved['bound_at']).toMatch(/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$/);

    const second = await join(
      fixture,
      {
        status: { body: { control_protocol: FULL_CONTROL_V2 } },
        join: { body: v2Result(SECOND_ID, CONTROLLER) },
      },
      { gameId: SECOND_ID }
    );
    ok(second.result);
    expect(second.captured.out).toContain(
      `this workspace is now playing ${SECOND_ID}, rebound from ${GAME_ID} — ` +
        'commands need no --session'
    );
  });
});

describe('a stale invitation', () => {
  test('a 401 from the join POST names the owner recovery command', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const { result } = await join(fixture, {
      join: { status: 401, body: { error: { code: 'unauthorized', message: 'unauthorized' } } },
    });
    const message = failure(result);
    expect(message).toContain('HTTP 401: unauthorized');
    expect(message).toContain(`just invite ${GAME_ID}`);
    expect(message).toContain('The game invitation may be stale.');
  });

  test('a 500 is passed through without the stale-invite advice', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const { result } = await join(fixture, {
      join: { status: 500, body: { error: { code: 'internal_error', message: 'boom' } } },
    });
    expect(failure(result)).toBe('HTTP 500: boom');
  });

  test('an unreachable supervisor tells the user to stop', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const failing = (() =>
      Promise.reject(new Error('connection refused'))) as unknown as typeof fetch;
    const captured = await Effect.runPromise(
      Effect.either(Effect.provide(commandJoin(args(), {}), fixture.layer(failing)))
    );
    expect(failure(captured)).toContain(
      'The assigned game cannot be joined. Stop and tell the user.'
    );
  });
});

describe('--json', () => {
  test('it prints the raw payload, minus the bearer, plus where it was saved', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const { result, captured } = await join(
      fixture,
      {
        status: { body: { control_protocol: FULL_CONTROL_V2 } },
        join: { body: v2Result(GAME_ID, CONTROLLER) },
      },
      { json: true }
    );
    ok(result);
    const payload = JSON.parse(captured.out) as JsonObject;
    expect(payload['agent_token']).toBeUndefined();
    expect(payload['session_saved']).toBe(true);
    expect(payload['session_file']).toBe(
      path.join(fixture.workspace.stateRoot, GAME_ID, `${sessionKey(CONTROLLER)}.json`)
    );
    expect(payload['game_id']).toBe(GAME_ID);
    // The JSON rendering is the wire payload, so the card never appears in it.
    expect(captured.out).not.toContain('ALIASES');
    // The conduct block is stderr and stays byte-identical under --json.
    expect(captured.err).toContain('Joined a full-control-v2 session.');
  });

  test('PLAY_JSON=1 is exactly --json', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const recorder = recordingFetch(
      new Map<string, FakeRoute>([
        ['/join', { body: v2Result(GAME_ID, CONTROLLER) }],
        ['/status', { body: { control_protocol: FULL_CONTROL_V2 } }],
        ['/health', { body: {} }],
      ])
    );
    const out: string[] = [];
    const originalLog = console.log;
    const originalError = console.error;
    console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
    console.error = () => undefined;
    try {
      const result = await Effect.runPromise(
        Effect.either(
          Effect.provide(
            commandJoin(args(), { PLAY_JSON: '1' }),
            fixture.layer(recorder.fetch)
          )
        )
      );
      ok(result);
    } finally {
      console.log = originalLog;
      console.error = originalError;
    }
    expect(out.join('\n')).not.toContain('ALIASES');
    expect(JSON.parse(out.join('\n'))['session_saved']).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// The flag surface `just join` folds in
// ---------------------------------------------------------------------------

describe('the flag surface', () => {
  const cli = async (
    fixture: Bench,
    argv: ReadonlyArray<string>,
    plan: FakeRoute = { body: v2Result(GAME_ID, CONTROLLER) }
  ): Promise<{ readonly failure: string | null; readonly out: string }> => {
    const recorder = recordingFetch(
      new Map<string, FakeRoute>([
        ['/join', plan],
        ['/status', { body: { control_protocol: FULL_CONTROL_V2 } }],
        ['/health', { body: {} }],
      ])
    );
    const root = Command.make('play', {}, () => Effect.void).pipe(
      Command.withSubcommands([joinCommand])
    );
    const out: string[] = [];
    const originalLog = console.log;
    const originalError = console.error;
    console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
    console.error = () => undefined;
    try {
      const outcome = await Effect.runPromise(
        Command.run(root, { name: 'play', version: '0.1.0' })(['bun', 'play', ...argv]).pipe(
          Effect.provide(Layer.merge(fixture.layer(recorder.fetch), BunContext.layer)),
          Effect.either
        )
      );
      return {
        failure:
          outcome._tag === 'Left'
            ? ((outcome.left as { readonly message?: string }).message ?? String(outcome.left))
            : null,
        out: out.join('\n'),
      };
    } finally {
      console.log = originalLog;
      console.error = originalError;
    }
  };

  test('the underscored spellings the protocol card prints are accepted', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const run = await cli(fixture, ['join', '--game_id', GAME_ID, '--name', CONTROLLER]);
    expect(run.failure).toBeNull();
    expect(run.out).toContain(`this workspace is now playing ${GAME_ID}`);
  });

  test('both spellings at once is a refusal, not a silent pick', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    const run = await cli(fixture, [
      'join',
      '--game_id',
      GAME_ID,
      '--game-id',
      GAME_ID,
      '--name',
      CONTROLLER,
    ]);
    expect(run.failure).toBe('pass only one of --game-id or --game_id');
  });

  test('a pre-configured workspace joins with no arguments at all', async () => {
    const fixture = bench();
    fixture.stage(GAME_ID);
    fs.writeFileSync(
      path.join(fixture.root, '.playconfig.json'),
      JSON.stringify({ schema_version: 1, game_id: GAME_ID, name: CONTROLLER, place: null }),
      'utf8'
    );
    const run = await cli(fixture, ['join']);
    expect(run.failure).toBeNull();
    expect(run.out).toContain(`this workspace is now playing ${GAME_ID}`);
  });

  test('a bare workspace with no arguments refuses on the game ID', async () => {
    const run = await cli(bench(), ['join']);
    expect(run.failure).toBe('a valid assigned game ID is required');
  });
});

describe('the conduct block', () => {
  test('a strategic-v1 seat is told to read the gameplay doc, not the v2 card', () => {
    const guidance = joinGuidance(
      { control_protocol: 'strategic-v1' },
      { timing_mode: 'blitz', action_timeout_s: 60 },
      seatBindingLine(GAME_ID)
    );
    expect(guidance).toContain('Joined in blitz timing mode: 60 seconds per agent turn.');
    expect(guidance).toContain('You—the assigned harness/model—must inspect each observation');
    expect(guidance).toContain('Read docs/gameplay.md');
    expect(guidance).toContain(`This workspace is now playing ${GAME_ID}`);
    expect(guidance).not.toContain('LOBBY FIRST');
  });

  test('a seat with a turn budget prints the remaining turns when it has them', () => {
    const guidance = joinGuidance(
      {
        control_protocol: FULL_CONTROL_V2,
        objective: 'Win',
        max_turns: 321,
        turns_remaining: 300,
      },
      { timing_mode: 'default', action_timeout_s: null },
      seatBindingLine(GAME_ID)
    );
    expect(guidance).toContain('Turn budget: 321 maximum; 300 remaining.');
    expect(guidance).toContain('Timing mode: default; no agent deadline.');
    expect(guidance).toContain('An ambiguous receipt is terminal and must never be replayed');
  });

  test('a missing timing mode reads "unknown" rather than "None"', () => {
    expect(
      joinGuidance({ control_protocol: 'strategic-v1' }, {}, seatBindingLine(GAME_ID))
    ).toContain('Joined in unknown timing mode: no agent deadline.');
  });

  /**
   * `str(result.get("timing_mode") or "unknown")` (client.py:6350).
   *
   * `timing_mode` is a v1 join field with no schema in
   * `full-control-v2.openapi.json`, so CPython's `or` is the drift absorber and
   * this table is what it absorbs.  Every expectation was taken from
   * `python3 -c "str(v or 'unknown')"`, not from the port.
   */
  describe('the timing-mode drift table', () => {
    const rows: ReadonlyArray<readonly [string, JsonObject, string]> = [
      ['a missing key', {}, 'unknown'],
      ['null', { timing_mode: null }, 'unknown'],
      ['the empty string', { timing_mode: '' }, 'unknown'],
      ['zero', { timing_mode: 0 }, 'unknown'],
      ['false', { timing_mode: false }, 'unknown'],
      ['an empty list', { timing_mode: [] }, 'unknown'],
      ['an empty object', { timing_mode: {} }, 'unknown'],
      ['a name', { timing_mode: 'blitz' }, 'blitz'],
      ['true', { timing_mode: true }, 'True'],
      ['a number', { timing_mode: 5 }, '5'],
      ['a fraction', { timing_mode: 2.5 }, '2.5'],
      ['a list', { timing_mode: ['a', 1] }, "['a', 1]"],
      ['an object', { timing_mode: { a: 1, b: null } }, "{'a': 1, 'b': None}"],
    ];

    for (const [label, result, expected] of rows) {
      test(`${label} prints ${expected}`, () => {
        expect(timingModeText(result)).toBe(expected);
      });

      test(`${label} reaches both conduct blocks as ${expected}`, () => {
        expect(
          joinGuidance({ control_protocol: 'strategic-v1' }, result, seatBindingLine(GAME_ID))
        ).toContain(`Joined in ${expected} timing mode: no agent deadline.`);
        expect(
          joinGuidance({ control_protocol: FULL_CONTROL_V2 }, result, seatBindingLine(GAME_ID))
        ).toContain(`Timing mode: ${expected}; no agent deadline.`);
      });
    }
  });
});
