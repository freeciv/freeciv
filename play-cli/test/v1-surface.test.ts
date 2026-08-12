/**
 * The v1 surface: `prompt`, `next`, `act`, `result`.
 *
 * `prompt` is pure text and is asserted byte for byte, because it is the one
 * document an operator pastes verbatim into a fresh agent.  The other three are
 * in `V2_JSON_ONLY_COMMANDS`: they declare no `--json` flag and always print
 * `json.dumps(indent=2, sort_keys=True)`, so the printer shape is asserted once
 * and the rest of each test is about *which* refusal comes out and in what
 * order the checks run.
 *
 * The assertions ported from `play/tests/test_client.py` are named in the tests
 * that carry them.
 */
import * as path from 'node:path';
import { afterAll, afterEach, beforeAll, describe, expect, test } from 'bun:test';
import { Command } from '@effect/cli';
import { BunContext } from '@effect/platform-bun';
import { Effect, Layer } from 'effect';
import { actCommand } from 'src/commands/act.cmd';
import { DEFAULT_WAIT_S, nextCommand, pythonFloatText } from 'src/commands/next.cmd';
import { promptCommand } from 'src/commands/prompt.cmd';
import { resultCommand } from 'src/commands/result.cmd';
import { FULL_CONTROL_V2, STRATEGIC_V1 } from 'src/constants';
import { indentedJson } from 'src/services/json-output';
import { V1Json, pyIndentedJson, v1JsonLayer } from 'src/services/v1-json';
import { parsePython } from 'src/services/canonical-body';
import {
  SessionStore,
  emptyV2ClientState,
  sessionStoreFor,
  type V2StateSchemaApi,
} from 'src/services/session-store';
import { promptText } from 'src/render/prompt-text';
import { privateFsFor, type WorkspacePaths } from 'src/services/private-fs';
import type { JsonObject } from 'src/schema/primitives';
import { recordingFetch, scratchWorkspace, type RecordedRequest, type Scratch } from 'test/_fixtures';

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

const GAME_ID = 'game_12345678901234567890';
const SERVICE_URL = 'http://127.0.0.1:8765';

interface Run {
  readonly failure: string | null;
  readonly out: ReadonlyArray<string>;
  readonly err: ReadonlyArray<string>;
  /**
   * stdout as raw bytes. `Console.log` appends exactly one newline to its
   * joined arguments, so rebuilding the stream this way is what turns a
   * line-wise assertion into a byte-wise one — the trailing newline `print()`
   * emits is invisible to `out` alone.
   */
  readonly bytes: string;
}

/**
 * U03 owns the real `.v2-state` schema; nothing on the v1 surface reads it, so
 * the seam gets its core default shape and a no-op proof.
 */
const stateSchema: V2StateSchemaApi = {
  empty: emptyV2ClientState,
  validate: () => Effect.void,
  cursorExpired: () => false,
};

const environment: Readonly<Record<string, string | undefined>> = {};

/** `result` takes no session at all, so its tests need no workspace either. */
const NO_WORKSPACE: WorkspacePaths = { root: '/nonexistent', stateRoot: '/nonexistent/.sessions' };

const layerFor = (
  scratch: Scratch | null,
  fetchImpl: typeof fetch
): Layer.Layer<SessionStore | V1Json> =>
  Layer.mergeAll(
    v1JsonLayer(fetchImpl),
    Layer.succeed(
      SessionStore,
      sessionStoreFor(
        scratch?.workspace ?? NO_WORKSPACE,
        scratch?.files ?? privateFsFor(NO_WORKSPACE),
        stateSchema,
        environment
      )
    )
  );

/** The user-facing sentence `cli-main` would print, from any mapped failure. */
const messageOf = (error: unknown): string =>
  typeof error === 'object' &&
  error !== null &&
  'message' in error &&
  typeof (error as { readonly message: unknown }).message === 'string'
    ? (error as { readonly message: string }).message
    : String(error);

/** Parse and run one subcommand, collecting stdout, stderr and the refusal. */
const run = async <Name extends string, E, A>(
  command: Command.Command<Name, SessionStore | V1Json, E, A>,
  argv: ReadonlyArray<string>,
  layer: Layer.Layer<SessionStore | V1Json>
): Promise<Run> => {
  const root = Command.make('play', {}, () => Effect.void).pipe(
    Command.withSubcommands([command])
  );
  const out: string[] = [];
  const err: string[] = [];
  let bytes = '';
  const originalLog = console.log;
  const originalError = console.error;
  console.log = (...parts: ReadonlyArray<unknown>) => {
    const line = parts.join(' ');
    out.push(line);
    bytes += `${line}\n`;
  };
  console.error = (...parts: ReadonlyArray<unknown>) => err.push(parts.join(' '));
  try {
    const outcome = await Effect.runPromise(
      Command.run(root, { name: 'play', version: '0.1.0' })(['bun', 'play', ...argv]).pipe(
        Effect.provide(Layer.merge(layer, BunContext.layer)),
        Effect.either
      )
    );
    const failure = outcome._tag === 'Left' ? messageOf(outcome.left) : null;
    return { failure, out, err, bytes };
  } finally {
    console.log = originalLog;
    console.error = originalError;
  }
};

const sessionBody = (overrides: JsonObject = {}): JsonObject => ({
  schema_version: 1,
  control_protocol: STRATEGIC_V1,
  service_url: SERVICE_URL,
  game_id: GAME_ID,
  agent_id: 'agent-one',
  agent_token: 'first-secret',
  controller_label: 'pi-gpt-5.6-sol',
  place: 1,
  seat_id: 'place-1',
  ...overrides,
});

interface Seat {
  readonly scratch: Scratch;
  readonly path: string;
}

const seats: Scratch[] = [];

/**
 * Write a session file whose key set is exactly `body` — no defaults merged in.
 *
 * `_load_session` requires only `game_id`/`agent_id`/`agent_token`/`service_url`,
 * so "the session simply has no `place`" is a shape the play loop really hits,
 * and it is the shape that separates CPython's `session.get(...)` → `None` from
 * a decoded field that was filled in with a placeholder.
 */
const rawSeat = async (body: JsonObject): Promise<Seat> => {
  const scratch = scratchWorkspace();
  seats.push(scratch);
  const file = path.join(scratch.workspace.stateRoot, GAME_ID, 'agent.json');
  await Effect.runPromise(scratch.files.writeJson(file, body));
  return { scratch, path: file };
};

const seat = (overrides: JsonObject = {}): Promise<Seat> => rawSeat(sessionBody(overrides));

afterEach(() => {
  while (seats.length > 0) seats.pop()?.cleanup();
});

const queryOf = (request: RecordedRequest): string => new URL(request.url).search;

/**
 * A fake supervisor that answers with **bytes**, not with a JavaScript value.
 *
 * `recordingFetch` builds its body with `JSON.stringify`, so `180.0` can never
 * reach the parser through it — which is exactly how the int/float divergence
 * on this surface stayed invisible to a whole test file.  Every assertion about
 * what `next`/`act`/`result` print has to start from the wire text.
 */
const textFetch = (
  body: string,
  status = 200
): { readonly fetch: typeof fetch; readonly requests: RecordedRequest[] } => {
  const requests: RecordedRequest[] = [];
  const impl = (async (input: unknown, init?: RequestInit): Promise<Response> => {
    const headers: Record<string, string> = {};
    new Headers(init?.headers).forEach((value, name) => {
      headers[name.toLowerCase()] = value;
    });
    requests.push({
      method: init?.method ?? 'GET',
      url: typeof input === 'string' ? input : String(input),
      headers,
      body: typeof init?.body === 'string' ? init.body : null,
    });
    return new Response(body, { status, headers: { 'content-type': 'application/json' } });
  }) as typeof fetch;
  return { fetch: impl, requests };
};

// ---------------------------------------------------------------------------
// prompt
// ---------------------------------------------------------------------------

const DEFAULT_PROMPT = `You are an autonomous Freeciv player in a player-only workspace.

Assigned game ID: GAME_ID

Before joining, identify yourself with a truthful public harness-model label,
such as codex-gpt-5.6-sol, pi-gpt-5.6-sol, or claude-code-claude-opus.

Timing is reported by the join response: default gives each agent 180 seconds
per turn on strategic-v1 and 10 minutes on full-control-v2,
blitz gives 60 seconds (strategic-v1 only),
and infinite has no agent deadline. You—the
assigned harness/model—must inspect each observation and choose its action
directly. Do not write, launch, or delegate to an automated bot solely to beat
the clock.

Read AGENTS.md, then run:

  just join --game_id GAME_ID --name HARNESS-MODEL

Join binds this workspace to the seat it joined, so no later command names a
session. If join reports \`strategic-v1\`, repeat:

  just next --after_turn LAST_TURN
  just act --turn TURN --observation_id OBSERVATION_ID --action '{"type":"set_traits","traits":{"aggressive":0,"builder":20,"expansionist":30,"trader":10}}'

Advance LAST_TURN only after \`act\` returns \`accepted: true\`. If \`act\` fails or
returns anything else, do not claim success and do not advance; poll again with
the same LAST_TURN so the server can redeliver the turn.

If join reports \`full-control-v2\`, the command contract is the protocol card
join prints; run \`just help\` for the play card. Errors carry their own remedy,
so read the refusal instead of the docs.

Use only the negotiated protocol's authenticated private state for decisions.
Never inspect parent directories or spectator data. Stop on completed, invalid,
failed, or cancelled. Keep this same conversation active and repeat the loop
until the game is terminal; do not give a final answer or stop merely because
one turn completed. If a command itself fails, fix that command and continue
rather than treating the game as finished. If GAME_ID is still a placeholder,
or join fails, stop and ask the user instead of inventing a game or retrying
blindly.`;

describe('play prompt', () => {
  const noNetwork = layerFor(null, recordingFetch([]).fetch);

  test('the bare card is byte-identical to the Python f-string', async () => {
    const { failure, out } = await run(promptCommand, ['prompt'], noNetwork);
    expect(failure).toBeNull();
    expect(out).toEqual([DEFAULT_PROMPT]);
  });

  test('--place contributes a flag fragment; nothing else moves', async () => {
    const { out } = await run(promptCommand, ['prompt', '--place', '3'], noNetwork);
    expect(out).toEqual([
      DEFAULT_PROMPT.replace(
        '--name HARNESS-MODEL\n',
        '--name HARNESS-MODEL --place 3\n'
      ),
    ]);
  });

  test('an empty --place contributes nothing at all', () => {
    expect(promptText('GAME_ID', 'HARNESS-MODEL', '')).toBe(DEFAULT_PROMPT);
  });

  test('--game-id and --name substitute in both places they appear', async () => {
    const { out } = await run(
      promptCommand,
      ['prompt', '--game-id', GAME_ID, '--name', 'claude-code-claude-opus'],
      noNetwork
    );
    const card = out[0] ?? '';
    expect(card).toContain(`Assigned game ID: ${GAME_ID}`);
    expect(card).toContain(`just join --game_id ${GAME_ID} --name claude-code-claude-opus`);
  });

  test('--game_id is the spelling the card itself teaches', async () => {
    const { failure, out } = await run(
      promptCommand,
      ['prompt', '--game_id', GAME_ID],
      noNetwork
    );
    expect(failure).toBeNull();
    expect(out[0]).toContain(`Assigned game ID: ${GAME_ID}`);
  });

  test('both spellings at once is a refusal', async () => {
    const { failure } = await run(
      promptCommand,
      ['prompt', '--game-id', GAME_ID, '--game_id', GAME_ID],
      noNetwork
    );
    expect(failure).toBe('pass only one of --game-id or --game_id');
  });

  test('test_prompt_names_timing_modes_and_requires_direct_model_choice', () => {
    const card = promptText(GAME_ID, 'claude-code-claude-opus', '');
    expect(card).toContain(
      'default gives each agent 180 seconds\nper turn on strategic-v1 and 10 minutes on full-control-v2'
    );
    expect(card).toContain('blitz gives 60 seconds');
    expect(card).toContain('infinite has no agent deadline');
    expect(card).toContain('choose its action\ndirectly');
    expect(card).toContain('Do not write, launch, or delegate');
    expect(card).toContain('Join binds this workspace to the seat it joined');
    expect(card).not.toContain('--session');
    expect(card).toContain('just next --after_turn LAST_TURN');
    expect(card).toContain('Advance LAST_TURN only after');
    expect(card).toContain('Keep this same conversation active');
    expect(card).toContain('do not give a final answer');
    expect(card).toContain('If a command itself fails');
  });

  test('test_prompt_teaches_one_v2_contract_and_not_the_old_ritual', () => {
    const card = promptText(GAME_ID, 'claude-code-claude-opus', '');
    expect(card).toContain('the command contract is the protocol card');
    expect(card).toContain('just help');
    expect(card).toContain('no later command names a\nsession');
    for (const retired of [
      '--session',
      'pregame_nations',
      'pregame_styles',
      'pregame_teams',
      'pregame.configure',
      'pregame.set_ready',
      'game_state: lobby',
      'Copy that exact path into every command',
      'diplomacy_clauses',
      'phase.end',
      'docs/gameplay.md',
    ]) {
      expect(card).not.toContain(retired);
    }
    // The card is read every time an agent is bootstrapped, so its size is a
    // budget, not an accident.
    expect(`${card}\n`.length).toBeLessThan(2400);
  });
});

// ---------------------------------------------------------------------------
// next
// ---------------------------------------------------------------------------

describe('play next', () => {
  const observation = (overrides: JsonObject = {}): JsonObject => ({
    turn: 4,
    observation_id: 'obs_one',
    agent_id: 'agent-one',
    game_id: GAME_ID,
    state: 'running',
    ...overrides,
  });

  test('the payload prints as indent=2, sort_keys=True, one trailing newline', async () => {
    const { scratch, path: file } = await seat();
    const body = observation({ zebra: 1, alpha: 2 });
    const fake = recordingFetch([{ body }]);
    const { failure, out } = await run(
      nextCommand,
      ['next', '--session', file],
      layerFor(scratch, fake.fetch)
    );
    expect(failure).toBeNull();
    // One `Console.log` call, so exactly one trailing newline reaches stdout.
    expect(out).toHaveLength(1);
    expect(out[0]).toBe(indentedJson(body));
    const keys = [...(out[0] ?? '').matchAll(/^ {2}"([^"]+)"/gm)].map((match) => match[1] ?? '');
    expect(keys).toEqual([...keys].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0)));
  });

  test('the printed payload ends in exactly one newline, like print() does', async () => {
    const { scratch, path: file } = await seat();
    const body = observation();
    const fake = recordingFetch([{ body }]);
    const { bytes } = await run(
      nextCommand,
      ['next', '--session', file],
      layerFor(scratch, fake.fetch)
    );
    expect(bytes).toBe(`${indentedJson(body)}\n`);
    expect(bytes.endsWith('}\n')).toBe(true);
    expect(bytes.endsWith('}\n\n')).toBe(false);
  });

  // Every `next` response carries `action_timeout_s`, which is
  // `TIMING_MODE_TIMEOUTS["default"] = 180.0` published verbatim
  // (agent_eval/supervisor.py:117, 8422), plus `deadline_at` (a `time.time()`
  // sum) and `pending_duration_s` (`round(max(0.0, …), 3)`, supervisor.py:8290).
  // All three are integral in an ordinary poll, so before the transport read
  // the wire lexeme this command byte-diverged on 100% of the poll loop.
  const NEXT_WIRE = [
    '{"schema_version": 1, "action_timeout_s": 180.0, "turn": 4, "year": -3950,',
    ' "deadline_at": 1770000000.5, "pending_duration_s": 0.0, "seats_remaining": 0,',
    ' "observation": {"score": 12.0, "units": [1, 2.5], "ratio": 1e-05, "big": 1e+16},',
    ' "state": "running", "game_id": "game_12345678901234567890", "agent_id": "agent-one"}',
  ].join('\n');

  /** `python3 -c 'json.dumps(json.loads(NEXT_WIRE), indent=2, sort_keys=True)'`. */
  const NEXT_GOLDEN =
    '{\n  "action_timeout_s": 180.0,\n  "agent_id": "agent-one",\n  "deadline_at": 1770000000.5,\n  "game_id": "game_12345678901234567890",\n  "observation": {\n    "big": 1e+16,\n    "ratio": 1e-05,\n    "score": 12.0,\n    "units": [\n      1,\n      2.5\n    ]\n  },\n  "pending_duration_s": 0.0,\n  "schema_version": 1,\n  "seats_remaining": 0,\n  "state": "running",\n  "turn": 4,\n  "year": -3950\n}';

  test('an integral float on the wire prints as a float, byte for byte', async () => {
    const { scratch, path: file } = await seat();
    const fake = textFetch(NEXT_WIRE);
    const { failure, bytes } = await run(
      nextCommand,
      ['next', '--session', file],
      layerFor(scratch, fake.fetch)
    );
    expect(failure).toBeNull();
    expect(bytes).toBe(`${NEXT_GOLDEN}\n`);
    // The regression this pins: `JSON.parse` + `String(180)` prints `180`.
    expect(bytes).toContain('"action_timeout_s": 180.0,');
    expect(indentedJson(JSON.parse(NEXT_WIRE) as unknown)).toContain('"action_timeout_s": 180,');
  });

  test('the identity guard still reads 180 and 180.0 as the same number', async () => {
    // CPython's `!=` is not the printer: `session["place"] == 1` matches an
    // echoed `1.0`.  The float spelling must survive to stdout without
    // inventing a mismatch on the way.
    const { scratch, path: file } = await seat({ agent_id: 11 });
    const fake = textFetch('{"turn": 4, "agent_id": 11.0, "game_id": "game_12345678901234567890"}');
    const { failure, bytes } = await run(
      nextCommand,
      ['next', '--session', file],
      layerFor(scratch, fake.fetch)
    );
    expect(failure).toBeNull();
    expect(bytes).toContain('"agent_id": 11.0');
  });

  test('an omitted --wait-s sends the int default argparse never coerced', async () => {
    const { scratch, path: file } = await seat();
    const fake = recordingFetch([{ body: observation() }]);
    await run(nextCommand, ['next', '--session', file], layerFor(scratch, fake.fetch));
    expect(queryOf(fake.requests[0] as RecordedRequest)).toBe('?after_turn=0&wait_s=120');
    expect(DEFAULT_WAIT_S).toBe(120);
  });

  test('a supplied --wait-s sends the float repr, as type=float produces', async () => {
    const { scratch, path: file } = await seat();
    const fake = recordingFetch([{ body: observation() }]);
    await run(
      nextCommand,
      ['next', '--session', file, '--after-turn', '7', '--wait-s', '120'],
      layerFor(scratch, fake.fetch)
    );
    expect(queryOf(fake.requests[0] as RecordedRequest)).toBe('?after_turn=7&wait_s=120.0');
  });

  test('pythonFloatText is CPython repr, not String()', () => {
    expect(pythonFloatText(120)).toBe('120.0');
    expect(pythonFloatText(0)).toBe('0.0');
    expect(pythonFloatText(12.5)).toBe('12.5');
    expect(pythonFloatText(0.25)).toBe('0.25');
  });

  test('the request is bearer-authenticated for this seat only', async () => {
    const { scratch, path: file } = await seat();
    const fake = recordingFetch([{ body: observation() }]);
    await run(nextCommand, ['next', '--session', file], layerFor(scratch, fake.fetch));
    const request = fake.requests[0] as RecordedRequest;
    expect(request.method).toBe('GET');
    expect(request.headers['authorization']).toBe('Bearer first-secret');
    expect(request.url.startsWith(`${SERVICE_URL}/v1/games/${GAME_ID}/me/next?`)).toBe(true);
  });

  test('both --wait-s spellings are accepted, one at a time', async () => {
    const { scratch, path: file } = await seat();
    const underscored = recordingFetch([{ body: observation() }]);
    const accepted = await run(
      nextCommand,
      ['next', '--session', file, '--wait_s', '30'],
      layerFor(scratch, underscored.fetch)
    );
    expect(accepted.failure).toBeNull();
    expect(queryOf(underscored.requests[0] as RecordedRequest)).toContain('wait_s=30.0');

    const both = recordingFetch([{ body: observation() }]);
    const refused = await run(
      nextCommand,
      ['next', '--session', file, '--wait-s', '30', '--wait_s', '30'],
      layerFor(scratch, both.fetch)
    );
    expect(refused.failure).toBe('pass only one of --wait-s or --wait_s');
    expect(both.requests).toHaveLength(0);
  });

  test('--after_turn is accepted too, because the bootstrap card teaches it', async () => {
    const { scratch, path: file } = await seat();
    const fake = recordingFetch([{ body: observation() }]);
    const { failure } = await run(
      nextCommand,
      ['next', '--session', file, '--after_turn', '3'],
      layerFor(scratch, fake.fetch)
    );
    expect(failure).toBeNull();
    expect(queryOf(fake.requests[0] as RecordedRequest)).toContain('after_turn=3');
  });

  test('a full-control-v2 session is refused with the v2 commands named', async () => {
    const { scratch, path: file } = await seat({ control_protocol: FULL_CONTROL_V2 });
    const fake = recordingFetch([{ body: observation() }]);
    const { failure } = await run(
      nextCommand,
      ['next', '--session', file],
      layerFor(scratch, fake.fetch)
    );
    expect(failure).toBe(
      'just next is strategic-v1 only; this full-control-v2 session ' +
        'uses `just health`, `just state`, and `just legal`'
    );
    // The protocol check precedes the request, so nothing was sent.
    expect(fake.requests).toHaveLength(0);
  });

  test('the protocol refusal wins over an out-of-range argument', async () => {
    // Ordering matters: the useful refusal for a v2 workspace names `just
    // health`, not `--wait-s`.
    const { scratch, path: file } = await seat({ control_protocol: FULL_CONTROL_V2 });
    const fake = recordingFetch([]);
    const { failure } = await run(
      nextCommand,
      ['next', '--session', file, '--wait-s', '900'],
      layerFor(scratch, fake.fetch)
    );
    expect(failure).toContain('strategic-v1 only');
  });

  test('the argument bounds are [0, 300] and >= 0, checked before the request', async () => {
    const { scratch, path: file } = await seat();
    for (const argv of [
      ['next', '--session', file, '--after-turn', '-1'],
      ['next', '--session', file, '--wait-s', '301'],
      ['next', '--session', file, '--wait-s', '-1'],
    ]) {
      const fake = recordingFetch([]);
      const { failure } = await run(nextCommand, argv, layerFor(scratch, fake.fetch));
      expect(failure).toBe('after-turn must be >= 0 and wait-s must be in [0, 300]');
      expect(fake.requests).toHaveLength(0);
    }
  });

  test('a response for another game or another seat is refused', async () => {
    const { scratch, path: file } = await seat();
    const wrongGame = recordingFetch([
      { body: observation({ game_id: 'game_99999999999999999999' }) },
    ]);
    expect(
      (await run(nextCommand, ['next', '--session', file], layerFor(scratch, wrongGame.fetch)))
        .failure
    ).toBe('the next response belongs to a different game');

    const wrongSeat = recordingFetch([{ body: observation({ agent_id: 'agent-two' }) }]);
    expect(
      (await run(nextCommand, ['next', '--session', file], layerFor(scratch, wrongSeat.fetch)))
        .failure
    ).toBe('the next response belongs to a different agent seat');
  });

  test('an omitted or null identity in the response is not a mismatch', async () => {
    const { scratch, path: file } = await seat();
    const fake = recordingFetch([{ body: { turn: 4, agent_id: null } }]);
    const { failure, out } = await run(
      nextCommand,
      ['next', '--session', file],
      layerFor(scratch, fake.fetch)
    );
    expect(failure).toBeNull();
    expect(out).toHaveLength(1);
  });

  test('the identity guard compares the session dict, not a decoded seat', async () => {
    // Both cases were run against CPython's `command_next` with a stubbed
    // transport; `session["agent_id"]` is the raw value, so a non-string one
    // still matches itself, and a null one leaves the set as `{None}` — which
    // makes *any* echoed seat a mismatch.
    const echoedBack = await seat({ agent_id: 11 });
    const same = recordingFetch([{ body: { turn: 4, agent_id: 11 } }]);
    expect(
      (
        await run(
          nextCommand,
          ['next', '--session', echoedBack.path],
          layerFor(echoedBack.scratch, same.fetch)
        )
      ).failure
    ).toBeNull();

    const seatless = await seat({ agent_id: null });
    const claimed = recordingFetch([{ body: { turn: 4, agent_id: 'agent-one' } }]);
    expect(
      (
        await run(
          nextCommand,
          ['next', '--session', seatless.path],
          layerFor(seatless.scratch, claimed.fetch)
        )
      ).failure
    ).toBe('the next response belongs to a different agent seat');
  });

  test('a terminal state prints the stop hint on stderr, stdout stays JSON', async () => {
    const { scratch, path: file } = await seat();
    const fake = recordingFetch([{ body: observation({ state: 'completed' }) }]);
    const { failure, out, err } = await run(
      nextCommand,
      ['next', '--session', file],
      layerFor(scratch, fake.fetch)
    );
    expect(failure).toBeNull();
    expect(out).toHaveLength(1);
    expect(err).toEqual(['\nGame is completed; stop the play loop.']);
  });

  test('a running state prints no hint', async () => {
    const { scratch, path: file } = await seat();
    const fake = recordingFetch([{ body: observation() }]);
    const { err } = await run(
      nextCommand,
      ['next', '--session', file],
      layerFor(scratch, fake.fetch)
    );
    expect(err).toEqual([]);
  });

  test('a non-2xx response is the v1 error message, not a rendered refusal', async () => {
    const { scratch, path: file } = await seat();
    const fake = recordingFetch([
      { status: 409, body: { error: { code: 'conflict', message: 'no such turn' } } },
    ]);
    const { failure, out } = await run(
      nextCommand,
      ['next', '--session', file],
      layerFor(scratch, fake.fetch)
    );
    expect(failure).toBe('HTTP 409: no such turn');
    expect(out).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// act
// ---------------------------------------------------------------------------

describe('play act', () => {
  const ACTION = JSON.stringify({
    type: 'set_traits',
    traits: { aggressive: 1, builder: 2, expansionist: 3, trader: 4 },
  });

  const acknowledgement = (overrides: JsonObject = {}): JsonObject => ({
    accepted: true,
    game_id: GAME_ID,
    agent_id: 'agent-one',
    place: 1,
    seat_id: 'place-1',
    controller_label: 'pi-gpt-5.6-sol',
    turn: 1,
    ...overrides,
  });

  const argv = (file: string, extra: ReadonlyArray<string> = []): ReadonlyArray<string> => [
    'act',
    '--session',
    file,
    '--turn',
    '1',
    '--observation-id',
    'obs_first',
    '--action',
    ACTION,
    ...extra,
  ];

  test('an accepted acknowledgement prints as the v1 JSON shape', async () => {
    const { scratch, path: file } = await seat();
    const body = acknowledgement();
    const fake = recordingFetch([{ body }]);
    const { failure, out } = await run(actCommand, argv(file), layerFor(scratch, fake.fetch));
    expect(failure).toBeNull();
    expect(out).toEqual([indentedJson(body)]);
  });

  test('the POST carries this seat token and the documented body', async () => {
    const { scratch, path: file } = await seat();
    const fake = recordingFetch([{ body: acknowledgement() }]);
    await run(actCommand, argv(file), layerFor(scratch, fake.fetch));
    const request = fake.requests[0] as RecordedRequest;
    expect(request.method).toBe('POST');
    expect(request.url).toBe(`${SERVICE_URL}/v1/games/${GAME_ID}/me/actions`);
    expect(request.headers['authorization']).toBe('Bearer first-secret');
    expect(JSON.parse(request.body ?? 'null')).toEqual({
      turn: 1,
      observation_id: 'obs_first',
      action: JSON.parse(ACTION),
    });
  });

  test('the acknowledgement prints its floats as floats', async () => {
    // `pending_duration_s` is `round(max(0.0, …), 3)` (supervisor.py:8290) and
    // is integral whenever the round lands on a whole second.
    const { scratch, path: file } = await seat();
    const wire =
      '{"accepted": true, "game_id": "game_12345678901234567890", "agent_id": "agent-one",' +
      ' "turn": 1, "place": 1, "seat_id": "place-1", "controller_label": "pi-gpt-5.6-sol",' +
      ' "pending_duration_s": 0.0, "seats_remaining": 0}';
    const fake = textFetch(wire);
    const { failure, bytes } = await run(actCommand, argv(file), layerFor(scratch, fake.fetch));
    expect(failure).toBeNull();
    expect(bytes).toContain('"pending_duration_s": 0.0,');
    expect(bytes).toContain('"seats_remaining": 0,');
  });

  test('a float in --action stays a float on the wire', async () => {
    // CPython: `json.dumps(json.loads('{"weight": 1.0}'), sort_keys=True,
    // separators=(",", ":"))` is `{"weight":1.0}`.  Re-encoding through
    // `JSON.parse` would send `{"weight":1}` and change the request bytes.
    const { scratch, path: file } = await seat();
    const fake = textFetch('{"accepted": true}');
    const action = '{"type":"set_traits","weight":1.0,"count":2}';
    const { failure } = await run(
      actCommand,
      ['act', '--session', file, '--turn', '1', '--observation-id', 'o', '--action', action],
      layerFor(scratch, fake.fetch)
    );
    expect(failure).toBeNull();
    expect(fake.requests[0]?.body).toBe(
      '{"action":{"count":2,"type":"set_traits","weight":1.0},"observation_id":"o","turn":1}'
    );
  });

  test('test_act_requires_explicit_accepted_acknowledgement', async () => {
    const { scratch, path: file } = await seat();
    for (const accepted of [false, null, 'true', 1]) {
      const fake = recordingFetch([{ body: { accepted } }]);
      const { failure, out } = await run(actCommand, argv(file), layerFor(scratch, fake.fetch));
      expect(failure).toBe(
        'the supervisor did not acknowledge the action as accepted; do not advance LAST_TURN'
      );
      expect(out).toEqual([]);
    }
  });

  test('an acknowledgement echoing the wrong seat field is refused by name', async () => {
    const { scratch, path: file } = await seat();
    const cases: ReadonlyArray<readonly [string, JsonObject]> = [
      ['game_id', { game_id: 'game_99999999999999999999' }],
      ['agent_id', { agent_id: 'agent-two' }],
      ['turn', { turn: 2 }],
      ['place', { place: 2 }],
      ['seat_id', { seat_id: 'place-2' }],
      ['controller_label', { controller_label: 'pi-claude-opus' }],
    ];
    for (const [field, override] of cases) {
      const fake = recordingFetch([{ body: acknowledgement(override) }]);
      const { failure } = await run(actCommand, argv(file), layerFor(scratch, fake.fetch));
      expect(failure).toBe(
        `the accepted action acknowledgement has the wrong ${field}; do not advance LAST_TURN`
      );
    }
  });

  test('a field the acknowledgement omits is not a mismatch', async () => {
    const { scratch, path: file } = await seat();
    const fake = recordingFetch([{ body: { accepted: true } }]);
    const { failure, out } = await run(actCommand, argv(file), layerFor(scratch, fake.fetch));
    expect(failure).toBeNull();
    expect(out).toHaveLength(1);
  });

  test('a session field that is null is never cross-checked', async () => {
    // `expected_value is not None` — a seatless session cannot contradict an
    // acknowledgement about a seat.
    const { scratch, path: file } = await seat({ place: null, seat_id: null });
    const fake = recordingFetch([{ body: acknowledgement({ place: 9, seat_id: 'place-9' }) }]);
    const { failure } = await run(actCommand, argv(file), layerFor(scratch, fake.fetch));
    expect(failure).toBeNull();
  });

  // The cross-check reads the *raw* session object, never the decoded one.
  // Every case below was run against CPython's `command_act` with a stubbed
  // transport; the expectation is that run's observable result.
  describe('the cross-check is against the session dict, not a decoded seat', () => {
    /** The exact key set `_load_session` requires and nothing more. */
    const MINIMAL: JsonObject = {
      game_id: GAME_ID,
      agent_id: 'agent-one',
      agent_token: 'first-secret',
      service_url: SERVICE_URL,
    };

    test('a session with no controller_label does not refuse an acknowledgement that has one', async () => {
      // CPython: `session.get("controller_label")` -> None -> check skipped, exit 0.
      // A decoded `controllerLabel` of `''` would compare and refuse here, and
      // because the refusal says "do not advance LAST_TURN" the play loop would
      // wedge on this turn forever.
      const { scratch, path: file } = await rawSeat(MINIMAL);
      const body: JsonObject = {
        accepted: true,
        game_id: GAME_ID,
        agent_id: 'agent-one',
        turn: 1,
        controller_label: 'pi-gpt-5.6-sol',
      };
      const fake = recordingFetch([{ body }]);
      const { failure, out } = await run(actCommand, argv(file), layerFor(scratch, fake.fetch));
      expect(failure).toBeNull();
      expect(out).toEqual([indentedJson(body)]);
    });

    test('a non-numeric place still guards the seat', async () => {
      // `just join --place north` writes a non-digit place verbatim
      // (client.py:6246). Narrowing it to a number would drop the guard.
      const { scratch, path: file } = await seat({ place: 'north', seat_id: 'place-north' });
      const wrong = recordingFetch([
        { body: acknowledgement({ place: 'south', seat_id: 'place-north' }) },
      ]);
      expect((await run(actCommand, argv(file), layerFor(scratch, wrong.fetch))).failure).toBe(
        'the accepted action acknowledgement has the wrong place; do not advance LAST_TURN'
      );

      const right = recordingFetch([
        { body: acknowledgement({ place: 'north', seat_id: 'place-north' }) },
      ]);
      expect((await run(actCommand, argv(file), layerFor(scratch, right.fetch))).failure).toBeNull();
    });

    test('a non-string seat_id still guards the seat', async () => {
      const { scratch, path: file } = await seat({ seat_id: 7 });
      const fake = recordingFetch([{ body: acknowledgement({ seat_id: 9 }) }]);
      expect((await run(actCommand, argv(file), layerFor(scratch, fake.fetch))).failure).toBe(
        'the accepted action acknowledgement has the wrong seat_id; do not advance LAST_TURN'
      );
    });

    test('a non-string game_id or agent_id still guards the identity', async () => {
      for (const [key, mine, theirs] of [
        ['game_id', 11, 12],
        ['agent_id', 11, 12],
      ] as const) {
        const { scratch, path: file } = await rawSeat({ ...MINIMAL, [key]: mine });
        const fake = recordingFetch([
          { body: { accepted: true, game_id: GAME_ID, agent_id: 'agent-one', turn: 1, [key]: theirs } },
        ]);
        expect((await run(actCommand, argv(file), layerFor(scratch, fake.fetch))).failure).toBe(
          `the accepted action acknowledgement has the wrong ${key}; do not advance LAST_TURN`
        );
      }
    });
  });

  test('a positive turn and a nonempty observation ID are required', async () => {
    const { scratch, path: file } = await seat();
    const zeroTurn = recordingFetch([]);
    expect(
      (
        await run(
          actCommand,
          ['act', '--session', file, '--turn', '0', '--observation-id', 'o', '--action', ACTION],
          layerFor(scratch, zeroTurn.fetch)
        )
      ).failure
    ).toBe('a positive turn and nonempty observation ID are required');
    expect(zeroTurn.requests).toHaveLength(0);

    const emptyObservation = recordingFetch([]);
    expect(
      (
        await run(
          actCommand,
          ['act', '--session', file, '--turn', '1', '--observation-id', '', '--action', ACTION],
          layerFor(scratch, emptyObservation.fetch)
        )
      ).failure
    ).toBe('a positive turn and nonempty observation ID are required');
  });

  test('--observation_id is accepted, and omitting both spellings is refused', async () => {
    const { scratch, path: file } = await seat();
    const fake = recordingFetch([{ body: acknowledgement() }]);
    const accepted = await run(
      actCommand,
      ['act', '--session', file, '--turn', '1', '--observation_id', 'obs_first', '--action', ACTION],
      layerFor(scratch, fake.fetch)
    );
    expect(accepted.failure).toBeNull();

    const missing = recordingFetch([]);
    const refused = await run(
      actCommand,
      ['act', '--session', file, '--turn', '1', '--action', ACTION],
      layerFor(scratch, missing.fetch)
    );
    expect(refused.failure).toBe('the following arguments are required: --observation-id');
    expect(missing.requests).toHaveLength(0);
  });

  test('--action must be valid JSON, and must be an object', async () => {
    const { scratch, path: file } = await seat();
    const invalid = recordingFetch([]);
    const notJson = await run(
      actCommand,
      ['act', '--session', file, '--turn', '1', '--observation-id', 'o', '--action', '{'],
      layerFor(scratch, invalid.fetch)
    );
    // CPython: `str(json.JSONDecodeError)`, not V8's own sentence.
    expect(notJson.failure).toBe(
      '--action must be valid JSON: Expecting property name enclosed in double quotes: ' +
        'line 1 column 2 (char 1)'
    );
    expect(invalid.requests).toHaveLength(0);

    const missingValue = await run(
      actCommand,
      ['act', '--session', file, '--turn', '1', '--observation-id', 'o', '--action', '{"a":}'],
      layerFor(scratch, recordingFetch([]).fetch)
    );
    expect(missingValue.failure).toBe(
      '--action must be valid JSON: Expecting value: line 1 column 6 (char 5)'
    );

    const notObject = await run(
      actCommand,
      ['act', '--session', file, '--turn', '1', '--observation-id', 'o', '--action', '[1]'],
      layerFor(scratch, recordingFetch([]).fetch)
    );
    expect(notObject.failure).toBe('--action must be a JSON object');
  });

  test('a full-control-v2 session is refused with the v2 commands named', async () => {
    const { scratch, path: file } = await seat({ control_protocol: FULL_CONTROL_V2 });
    const fake = recordingFetch([]);
    const { failure } = await run(actCommand, argv(file), layerFor(scratch, fake.fetch));
    expect(failure).toBe(
      'just act is strategic-v1 only; this full-control-v2 session ' +
        'uses `just batch` and durable receipts'
    );
    expect(fake.requests).toHaveLength(0);
  });

  test('test_wrong_current_session_act_fails_before_request', async () => {
    // Two seats in one workspace and no binding: the refusal names the command
    // that makes the workspace unambiguous, and nothing is sent.
    const scratch = scratchWorkspace();
    seats.push(scratch);
    const first = path.join(scratch.workspace.stateRoot, GAME_ID, 'pi-gpt.json');
    const second = path.join(scratch.workspace.stateRoot, GAME_ID, 'pi-claude.json');
    await Effect.runPromise(scratch.files.writeJson(first, sessionBody()));
    await Effect.runPromise(
      scratch.files.writeJson(
        second,
        sessionBody({
          agent_id: 'agent-second',
          agent_token: 'second-secret',
          place: 2,
          seat_id: 'place-2',
          controller_label: 'pi-claude-opus',
        })
      )
    );
    const ambiguous = recordingFetch([]);
    const { failure } = await run(
      actCommand,
      ['act', '--turn', '1', '--observation-id', 'obs_first', '--action', ACTION],
      layerFor(scratch, ambiguous.fetch)
    );
    expect(failure).toContain('multiple private sessions');
    expect(ambiguous.requests).toHaveLength(0);

    // Naming the seat explicitly sends that seat's token, not the other one's.
    const resolved = recordingFetch([{ body: acknowledgement() }]);
    const accepted = await run(actCommand, argv(first), layerFor(scratch, resolved.fetch));
    expect(accepted.failure).toBeNull();
    expect((resolved.requests[0] as RecordedRequest).headers['authorization']).toBe(
      'Bearer first-secret'
    );
  });
});

// ---------------------------------------------------------------------------
// result
// ---------------------------------------------------------------------------

describe('play result', () => {
  const RESULT = { game_id: GAME_ID, state: 'completed', winner: 'Alice' };

  // `result` reaches `service_url()` with no session to read it from, so the
  // origin comes from the environment. Pin it rather than inheriting whatever
  // the developer's shell has exported.
  const inherited = process.env['AGENT_EVAL_SERVICE_URL'];
  beforeAll(() => {
    process.env['AGENT_EVAL_SERVICE_URL'] = SERVICE_URL;
  });
  afterAll(() => {
    if (inherited === undefined) delete process.env['AGENT_EVAL_SERVICE_URL'];
    else process.env['AGENT_EVAL_SERVICE_URL'] = inherited;
  });

  test('the positional game ID is accepted', async () => {
    const fake = recordingFetch([{ body: RESULT }]);
    const { failure, out } = await run(
      resultCommand,
      ['result', GAME_ID],
      layerFor(null, fake.fetch)
    );
    expect(failure).toBeNull();
    expect(out).toEqual([indentedJson(RESULT)]);
    expect((fake.requests[0] as RecordedRequest).url).toBe(
      `${SERVICE_URL}/v1/games/${GAME_ID}/result`
    );
  });

  test('test_v2_result_accepts_positional_or_named_id_and_state_hints', async () => {
    for (const argv of [
      ['result', GAME_ID],
      ['result', '--game-id', GAME_ID],
      ['result', '--game_id', GAME_ID],
      ['result', GAME_ID, '--game-id', GAME_ID],
    ]) {
      const fake = recordingFetch([{ body: RESULT }]);
      const { failure } = await run(resultCommand, argv, layerFor(null, fake.fetch));
      expect(failure).toBeNull();
      expect(fake.requests).toHaveLength(1);
    }
  });

  test('two different game IDs is a refusal, not a precedence rule', async () => {
    const fake = recordingFetch([]);
    const { failure } = await run(
      resultCommand,
      ['result', GAME_ID, '--game-id', 'game_99999999999999999999'],
      layerFor(null, fake.fetch)
    );
    expect(failure).toBe('result received two different game IDs');
    expect(fake.requests).toHaveLength(0);
  });

  test('a missing or malformed ID names what is required', async () => {
    for (const argv of [['result'], ['result', 'nonsense'], ['result', '--game-id', 'game_x']]) {
      const fake = recordingFetch([]);
      const { failure } = await run(resultCommand, argv, layerFor(null, fake.fetch));
      expect(failure).toBe('a valid assigned game ID is required');
      expect(fake.requests).toHaveLength(0);
    }
  });

  test('the result is public: the request carries no bearer credential', async () => {
    const fake = recordingFetch([{ body: RESULT }]);
    await run(resultCommand, ['result', GAME_ID], layerFor(null, fake.fetch));
    const request = fake.requests[0] as RecordedRequest;
    expect(request.method).toBe('GET');
    expect(request.headers['authorization']).toBeUndefined();
  });

  test('the run report keeps every float it was written with', async () => {
    // `result` returns the whole run report, so this is the surface a field map
    // could never have covered.  The floats below are the shapes really on
    // disk: `.agent-eval/runs/game_*/report.json` carries
    // `"action_timeout_s": 600.0`, `"lobby_timeout_s": 0.0`, `"latency_ms": 0.0`.
    const wire = [
      '{"game_id": "game_12345678901234567890", "state": "completed",',
      ' "manifest": {"config": {"action_timeout_s": 600.0, "lobby_timeout_s": 0.0, "places": 2},',
      '              "checkpoints": 52},',
      ' "seats": [{"latency_ms": 0.0, "score": 39, "place": 1},',
      '           {"latency_ms": 12.75, "score": 0, "place": 2}],',
      ' "empty_map": {}, "empty_list": []}',
    ].join('\n');
    /** `python3 -c 'json.dumps(json.loads(wire), indent=2, sort_keys=True)'`. */
    const golden =
      '{\n  "empty_list": [],\n  "empty_map": {},\n  "game_id": "game_12345678901234567890",\n  "manifest": {\n    "checkpoints": 52,\n    "config": {\n      "action_timeout_s": 600.0,\n      "lobby_timeout_s": 0.0,\n      "places": 2\n    }\n  },\n  "seats": [\n    {\n      "latency_ms": 0.0,\n      "place": 1,\n      "score": 39\n    },\n    {\n      "latency_ms": 12.75,\n      "place": 2,\n      "score": 0\n    }\n  ],\n  "state": "completed"\n}';
    const fake = textFetch(wire);
    const { failure, bytes } = await run(
      resultCommand,
      ['result', GAME_ID],
      layerFor(null, fake.fetch)
    );
    expect(failure).toBeNull();
    expect(bytes).toBe(`${golden}\n`);
  });

  test('the payload prints sorted and indented, like next and act', async () => {
    const fake = recordingFetch([{ body: { zebra: 1, alpha: { nested: true } } }]);
    const { out } = await run(resultCommand, ['result', GAME_ID], layerFor(null, fake.fetch));
    expect(out).toEqual([
      ['{', '  "alpha": {', '    "nested": true', '  },', '  "zebra": 1', '}'].join('\n'),
    ]);
  });
});

// ---------------------------------------------------------------------------
// the printer itself
// ---------------------------------------------------------------------------

describe('pyIndentedJson', () => {
  /** `json.dumps(json.loads(wire), indent=2, sort_keys=True)`, run for real. */
  const cpython = (wire: string, golden: string): void => {
    expect(pyIndentedJson(parsePython(wire).value)).toBe(golden);
  };

  test('an integral float keeps its `.0`, an int keeps its bare digits', () => {
    cpython(
      '{"n": 1.0, "i": 1, "neg": -0.0, "big": 1e+16, "small": 1e-05}',
      '{\n  "big": 1e+16,\n  "i": 1,\n  "n": 1.0,\n  "neg": -0.0,\n  "small": 1e-05\n}'
    );
  });

  test('strings are escaped the way ensure_ascii=True escapes them', () => {
    cpython(
      '{"city": "Orl\\u00e9ans", "flag": "\\ud83c\\udff3", "n": 1.0, "i": 1, "neg": -0.0}',
      '{\n  "city": "Orl\\u00e9ans",\n  "flag": "\\ud83c\\udff3",\n  "i": 1,\n  "n": 1.0,\n  "neg": -0.0\n}'
    );
  });

  test('empty containers stay on one line, as CPython leaves them', () => {
    cpython('{"a": [], "b": {}}', '{\n  "a": [],\n  "b": {}\n}');
  });

  test('it agrees with core indentedJson wherever no float is integral', () => {
    // The two printers may differ on exactly one thing.  Anywhere a payload
    // carries no integral float, `indentedJson` stays the cheaper answer and
    // the older assertions in this file remain valid.
    const wire = '{"zebra": 1, "alpha": {"nested": true, "list": [1, "two", null]}}';
    expect(pyIndentedJson(parsePython(wire).value)).toBe(
      indentedJson(JSON.parse(wire) as unknown)
    );
  });
});
