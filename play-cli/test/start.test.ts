/**
 * `play start` end to end.
 *
 * Ports `test_v2_start_resolves_names_then_configures_then_readies`
 * (test_client.py:6158), `…with_no_arguments_resolves_every_choice_itself`
 * (8643), `…flags_each_override_exactly_what_they_name` (8692),
 * `…falls_back_to_the_lobby_leader_when_the_label_is_unusable` (8725),
 * `…picks_the_same_sex_twice_when_the_lobby_names_none` (8775) and
 * `…fails_closed_when_the_lobby_offers_no_nation` (8816).  The seventh,
 * `…sanitizes_a_label_and_picks_a_sex_deterministically` (8762), is a pure
 * assertion over `_sanitized_leader` and lives in `test/pregame.test.ts`.
 *
 * The cross-unit seams (U11's enumeration, U13's persist/submit, U14's
 * disposition render) are supplied here as test hooks that talk to the same
 * fake supervisor, so the request *order* — the property the Python test
 * actually guards, because the re-enumeration between configure and set_ready
 * is mandatory — is asserted for real rather than stubbed away.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import { FULL_CONTROL_V2 } from 'src/constants';
import { playerError, type PlayError } from 'src/errors';
import type { ExitCodeSignal } from 'src/exit';
import { liveStartHooks, runStart, type StartHooksFor, type StartOptions } from 'src/commands/start.cmd';
import { isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import { httpFor } from 'src/services/http';
import { V2_PROTOCOL_CARD } from 'src/render/join';
import { NOT_READIED_LINE } from 'src/render/pregame';
import { DEFAULT_COMMAND_CARD, mirrorDir } from 'src/services/mirror';
import { orderReceiptOk, type PregameAction, type PregameItem, type StartHooks } from 'src/services/pregame';
import { PrivateFs } from 'src/services/private-fs';
import { batchDisposition } from 'src/services/batch';
import {
  SessionStore,
  credentialsOf,
  sessionStoreFor,
  type Session,
} from 'src/services/session-store';
import { V2Client, v2ClientFor, type V2ClientApi, type V2Credentials } from 'src/services/v2-client';
import { v2StateSchema } from 'src/services/aliases';
import { FIXTURE_AGENT_ID, FIXTURE_GAME_ID, scratchWorkspace, sessionFile, type Scratch } from 'test/_fixtures';

const scratches: Scratch[] = [];

afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

// ---------------------------------------------------------------------------
// Wire payloads (the Python test class's helpers, ported)
// ---------------------------------------------------------------------------

const CONTROLLER = 'codex-test-model';
const NATION_ENGLISH = `nation_${'a'.repeat(32)}`;
const NATION_ZULU = `nation_${'c'.repeat(32)}`;
const STYLE_EUROPEAN = `style_${'b'.repeat(32)}`;
const PLAYER = `player_${'f'.repeat(32)}`;

const revision = (number: number): JsonObject => ({
  turn: 0,
  revision: number,
  state_token: `state_${String(number).padStart(32, '0')}`,
});

const LOBBY = revision(4);
const CONFIGURED = revision(5);
const READIED = revision(6);

const envelope = (body: JsonObject): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  ...body,
});

const sectionPage = (
  section: string,
  stateRevision: JsonObject,
  items: ReadonlyArray<JsonValue>
): JsonObject =>
  envelope({
    state_revision: stateRevision,
    page: { section, items: [...items], total_items: items.length, next_cursor: null },
  });

const legalPage = (stateRevision: JsonObject, items: ReadonlyArray<JsonValue>): JsonObject =>
  envelope({
    state_revision: stateRevision,
    page: {
      section: 'legal_actions',
      items: [...items],
      total_items: items.length,
      next_cursor: null,
    },
  });

const pregameAction = (
  stateRevision: JsonObject,
  actionId: string,
  kind: string,
  operation: string,
  label: string,
  schema: JsonObject,
  target: JsonObject
): JsonObject => ({
  action_id: actionId,
  kind,
  label,
  subject: {
    operation,
    actor: { id: PLAYER, type: 'player', name: 'AgentPlace1' },
    target,
    variant: null,
    consuming: false,
    legality: 'legal',
    probability: { kind: 'exact', minimum_percent: 100, maximum_percent: 100 },
  },
  arguments_schema: schema,
  state_revision: stateRevision,
});

const CONFIGURE = pregameAction(
  LOBBY,
  `action_${'1'.repeat(26)}`,
  'pregame.configure',
  'configure',
  'Choose nation, leader, sex, and style',
  {
    type: 'object',
    properties: {
      nation_id: { type: 'string' },
      leader_name: { type: 'string' },
      is_male: { type: 'boolean' },
      style_id: { type: 'string' },
    },
    required: ['nation_id', 'leader_name', 'is_male', 'style_id'],
  },
  { type: 'pregame_configuration' }
);

const SET_READY = pregameAction(
  CONFIGURED,
  `action_${'2'.repeat(26)}`,
  'pregame.set_ready',
  'set_ready',
  'Mark ready',
  {
    type: 'object',
    properties: { ready: { type: 'boolean', enum: [true] } },
    required: ['ready'],
  },
  { type: 'pregame_readiness', desired_ready: true }
);

const lobbyHealth = (gameState = 'lobby', controller = CONTROLLER): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent: { agent_id: FIXTURE_AGENT_ID, controller_label: controller },
  game_state: gameState,
  seat: { place: 1, seat_id: 'seat_one', player_name: 'Alice' },
  sidecar: { state: 'ready', generation: 1 },
  observation_available: false,
  legal_actions_available: false,
  phase: null,
  last_phase_end: null,
});

const OFFERED: ReadonlyArray<JsonObject> = [
  { id: NATION_ENGLISH, name: 'English', default_style_id: STYLE_EUROPEAN },
  { id: NATION_ZULU, name: 'Zulu', default_style_id: STYLE_EUROPEAN },
];

// ---------------------------------------------------------------------------
// The fake supervisor
// ---------------------------------------------------------------------------

type FetchInput = Parameters<typeof fetch>[0];

const urlOf = (input: FetchInput): string =>
  typeof input === 'string' ? input : input instanceof URL ? input.href : input.url;

const json = (body: JsonValue, status = 200): Response =>
  new Response(JSON.stringify(body), {
    status,
    headers: { 'content-type': 'application/json' },
  });

interface ResponderOptions {
  readonly nations?: ReadonlyArray<JsonObject>;
  readonly leader?: string;
  readonly sex?: string;
  readonly gameState?: string;
  readonly controller?: string;
}

interface Recorded {
  readonly steps: ReadonlyArray<string>;
  readonly bodies: ReadonlyArray<JsonObject>;
  readonly fetch: typeof fetch;
}

/** `start_responder` (test_client.py:8534), as a `fetch`. */
const startResponder = (options: ResponderOptions = {}): Recorded => {
  const steps: string[] = [];
  const bodies: JsonObject[] = [];
  const nations = options.nations ?? OFFERED;
  const catalogs = [legalPage(LOBBY, [CONFIGURE]), legalPage(CONFIGURED, [SET_READY])];
  const impl = async (input: FetchInput, init?: RequestInit): Promise<Response> => {
    const url = urlOf(input);
    if (url.includes('/health')) {
      steps.push('health');
      return json(lobbyHealth(options.gameState ?? 'lobby', options.controller ?? CONTROLLER));
    }
    if (url.includes('section=pregame_nations')) {
      steps.push('nations');
      return json(sectionPage('pregame_nations', LOBBY, nations));
    }
    if (url.includes('section=pregame_styles')) {
      steps.push('styles');
      return json(
        sectionPage('pregame_styles', LOBBY, [{ id: STYLE_EUROPEAN, name: 'European' }])
      );
    }
    if (url.includes('section=overview')) {
      steps.push('overview');
      return json(
        sectionPage('overview', LOBBY, [
          {
            client_state: 'preparing',
            turn: 0,
            phase: null,
            player: {
              id: PLAYER,
              leader_name: options.leader ?? 'Boudica',
              nation: null,
              sex: options.sex ?? 'female',
              style: null,
              ready: false,
            },
          },
        ])
      );
    }
    if (url.includes('legal-actions')) {
      steps.push('legal');
      const seen = steps.filter((step) => step === 'legal').length;
      return json(catalogs[Math.min(seen, catalogs.length) - 1] ?? null);
    }
    steps.push('batch');
    const raw: unknown = JSON.parse(typeof init?.body === 'string' ? init.body : '{}');
    if (!isJsonObject(raw)) throw new Error('the batch body was not an object');
    bodies.push(raw);
    // Compared field by field, not by `JSON.stringify`: `_persist_batch_for_action`
    // writes the *canonical* body, whose keys are sorted, so a whole-object
    // string compare against this file's literal would answer "different" for
    // the same revision and hand the configure batch the readied revision.
    const sent = raw['state_revision'];
    const sameRevision = isJsonObject(sent) && sent['revision'] === LOBBY['revision'];
    return json({
      schema_version: 2,
      control_protocol: FULL_CONTROL_V2,
      game_id: FIXTURE_GAME_ID,
      agent_id: FIXTURE_AGENT_ID,
      batch_id: String(raw['batch_id']),
      receipt_state: 'applied',
      idempotent: true,
      state_revision: sameRevision ? CONFIGURED : READIED,
      error: null,
      observation: null,
    });
  };
  return { steps, bodies, fetch: impl as typeof fetch };
};

// ---------------------------------------------------------------------------
// The cross-unit seams, as test hooks
// ---------------------------------------------------------------------------

const objectsAt = (page: JsonObject): ReadonlyArray<JsonObject> => {
  const body = page['page'];
  const items = isJsonObject(body) ? body['items'] : undefined;
  return Array.isArray(items) ? items.filter(isJsonObject) : [];
};

const asAction = (descriptor: JsonObject): PregameAction => ({
  action_id: String(descriptor['action_id']),
  kind: String(descriptor['kind']),
  argument_schema: (descriptor['arguments_schema'] ?? null) as JsonValue,
});

/**
 * `_drain_legal_unlocked` + `_resolve_kind_action` + `_persist_batch_for_action`
 * + `_submit_persisted_batch` + `_render_disposition`, reduced to exactly the
 * behaviour `start` depends on: enumerate once per cache miss, persist the
 * descriptor's own revision, POST it, and name the receipt.
 */
const hooksFor = (
  client: V2ClientApi,
  session: Session,
  choose: (items: ReadonlyArray<PregameItem>) => PregameItem
): StartHooks => {
  const credentials: V2Credentials = credentialsOf(session);
  let cached: ReadonlyArray<JsonObject> = [];
  let issued = 0;
  const persisted = new Map<string, JsonObject>();
  const drain = Effect.gen(function* () {
    const page = yield* client.get(credentials, '/legal-actions');
    cached = objectsAt(page);
  });
  const cachedOf = (kind: string): JsonObject | null => {
    const matches = cached.filter((descriptor) => descriptor['kind'] === kind);
    return matches.length === 1 ? (matches[0] ?? null) : null;
  };
  return {
    mirrorPage: () => Effect.void,
    choose,
    receiptOk: orderReceiptOk,
    drainLegal: () => drain,
    resolveKindAction: (kind, remedy) =>
      Effect.gen(function* () {
        if (cachedOf(kind) === null) yield* drain;
        const found = cachedOf(kind);
        if (found === null) {
          return yield* Effect.fail(
            playerError(`no ${kind} action is enumerable for this seat right now; ${remedy}`)
          );
        }
        return asAction(found);
      }),
    persistBatchForAction: (actionId, argumentValues) =>
      Effect.sync(() => {
        issued += 1;
        const batchId = `batch_${String(issued).padStart(24, '0')}`;
        const descriptor = cached.find((entry) => entry['action_id'] === actionId);
        persisted.set(batchId, {
          schema_version: 2,
          control_protocol: FULL_CONTROL_V2,
          game_id: credentials.gameId,
          agent_id: FIXTURE_AGENT_ID,
          batch_id: batchId,
          state_revision: (descriptor?.['state_revision'] ?? null) as JsonValue,
          commands: [{ action_id: actionId, arguments: argumentValues }],
        });
        return batchId;
      }),
    submitPersistedBatch: (batchId) =>
      Effect.gen(function* () {
        const body = persisted.get(batchId);
        if (body === undefined) {
          return yield* Effect.die(new Error(`no persisted batch ${batchId}`));
        }
        const wire = yield* client.post(credentials, '/batches', body);
        return {
          disposition: yield* batchDisposition(session, batchId, 'receipt_terminal', {
            receipt: wire,
          }),
          warning: null,
          exitCode: 0,
        };
      }),
    renderDisposition: (disposition, intent) =>
      Effect.succeed([
        `${intent} → ${disposition.receipt?.receipt_state ?? 'unknown'} rev5/t0`,
      ]),
  };
};

// ---------------------------------------------------------------------------
// The fixture
// ---------------------------------------------------------------------------

interface Fixture {
  readonly layer: Layer.Layer<SessionStore | V2Client | PrivateFs>;
  readonly sessionPath: string;
  readonly hooks: StartHooksFor;
}

const fixture = (
  recorded: Recorded,
  options: {
    readonly controller?: string;
    readonly choose?: (items: ReadonlyArray<PregameItem>) => PregameItem;
  } = {}
): Fixture => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const sessionPath = path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID, 'codex-test.json');
  Effect.runSync(
    scratch.files.writeJson(
      sessionPath,
      sessionFile({ controller_label: options.controller ?? CONTROLLER })
    )
  );
  const store = sessionStoreFor(scratch.workspace, scratch.files, v2StateSchema, {});
  const client = v2ClientFor(httpFor(recorded.fetch), () => Effect.void);
  const choose =
    options.choose ??
    ((items: ReadonlyArray<PregameItem>): PregameItem => {
      const first = items[0];
      if (first === undefined) throw new Error('the draw must not happen');
      return first;
    });
  return {
    sessionPath,
    layer: Layer.mergeAll(
      Layer.succeed(SessionStore, store),
      Layer.succeed(V2Client, client),
      Layer.succeed(PrivateFs, scratch.files)
    ),
    hooks: (_sessionPath, session) => Effect.succeed(hooksFor(client, session, choose)),
  };
};

const startOptions = (sessionPath: string, overrides: Partial<StartOptions> = {}): StartOptions => ({
  session: sessionPath,
  nation: '',
  leader: '',
  style: '',
  male: false,
  female: false,
  json: false,
  ...overrides,
});

const run = async (
  fix: Fixture,
  overrides: Partial<StartOptions> = {}
): Promise<ReadonlyArray<string>> => {
  const out: string[] = [];
  const original = console.log;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
  try {
    await Effect.runPromise(
      Effect.provide(runStart(startOptions(fix.sessionPath, overrides), fix.hooks), fix.layer)
    );
    return out;
  } finally {
    console.log = original;
  }
};

/** Rebuild a fixture's hooks with one or two seams replaced. */
const withHooks = (
  fix: Fixture,
  over: (hooks: StartHooks, session: Session) => StartHooks
): Fixture => ({
  ...fix,
  hooks: (sessionPath, session) =>
    Effect.map(fix.hooks(sessionPath, session), (hooks) => over(hooks, session)),
});

interface Captured {
  readonly out: ReadonlyArray<string>;
  readonly err: ReadonlyArray<string>;
  readonly outcome: Either.Either<void, PlayError | ExitCodeSignal>;
}

/** Both streams and the outcome, for the paths that use all three. */
const runRaw = async (
  fix: Fixture,
  overrides: Partial<StartOptions> = {}
): Promise<Captured> => {
  const out: string[] = [];
  const err: string[] = [];
  const originalLog = console.log;
  const originalError = console.error;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
  console.error = (...parts: ReadonlyArray<unknown>) => err.push(parts.join(' '));
  try {
    const outcome = await Effect.runPromise(
      Effect.either(
        Effect.provide(runStart(startOptions(fix.sessionPath, overrides), fix.hooks), fix.layer)
      )
    );
    return { out, err, outcome };
  } finally {
    console.log = originalLog;
    console.error = originalError;
  }
};

const refuse = async (
  fix: Fixture,
  overrides: Partial<StartOptions> = {}
): Promise<string> => {
  const outcome = await Effect.runPromise(
    Effect.either(
      Effect.provide(runStart(startOptions(fix.sessionPath, overrides), fix.hooks), fix.layer)
    )
  );
  if (Either.isRight(outcome)) throw new Error('expected a refusal');
  const failure = outcome.left as PlayError;
  return failure.message;
};

const argumentsOf = (body: JsonObject): JsonValue => {
  const commands = body['commands'];
  const first = Array.isArray(commands) ? commands[0] : undefined;
  return isJsonObject(first) ? ((first['arguments'] ?? null) as JsonValue) : null;
};

const commandOf = (body: JsonObject): JsonValue => {
  const commands = body['commands'];
  const first = Array.isArray(commands) ? commands[0] : undefined;
  return (first ?? null) as JsonValue;
};

// ---------------------------------------------------------------------------
// The tests
// ---------------------------------------------------------------------------

describe('play start', () => {
  test('names resolve, then configure, then RE-ENUMERATE, then ready', async () => {
    const recorded = startResponder();
    const fix = fixture(recorded);
    const lines = await run(fix, { nation: 'eNgLiSh', leader: 'Ada', female: true });

    // Lobby check, catalog, enumerate, configure, re-enumerate, ready: the
    // refresh between the two steps is mandatory.
    expect(recorded.steps).toEqual([
      'health',
      'nations',
      'legal',
      'batch',
      'legal',
      'batch',
    ]);
    expect(commandOf(recorded.bodies[0] ?? {})).toEqual({
      action_id: String(CONFIGURE['action_id']),
      arguments: {
        nation_id: NATION_ENGLISH,
        leader_name: 'Ada',
        is_male: false,
        style_id: STYLE_EUROPEAN,
      },
    });
    expect(commandOf(recorded.bodies[1] ?? {})).toEqual({
      action_id: String(SET_READY['action_id']),
      arguments: { ready: true },
    });
    expect(recorded.bodies[1]?.['state_revision']).toEqual(CONFIGURED);
    expect(lines).toHaveLength(3);
    expect(lines[0]).toBe('starting as English — Ada (female), style the nation default');
    expect(lines[1]?.startsWith('configure English Ada female →')).toBe(true);
    expect(lines[2]?.startsWith('set ready → applied')).toBe(true);
  });

  test('a nation that is not on the catalog is refused by name', async () => {
    const fix = fixture(startResponder());
    const message = await refuse(fix, { nation: 'Atlantean', leader: 'Ada', male: true });
    expect(message).toContain("no nation named 'Atlantean'");
    expect(message).toContain('English');
  });

  test('an opaque id is not a name: the catalog is matched on the display name only', async () => {
    // CPython's `_pregame_choice` compares `item["name"]` and nothing else, so
    // `--nation nation_aaa…` is an unknown *name*.  See NOTES.md §U18.
    const fix = fixture(startResponder());
    const message = await refuse(fix, { nation: NATION_ENGLISH, leader: 'Ada', male: true });
    expect(message).toBe(
      `no nation named '${NATION_ENGLISH}' is offered; try one of: English Zulu`
    );
  });

  test('sex is optional but exclusive, and the refusal precedes the first request', async () => {
    const recorded = startResponder();
    const fix = fixture(recorded);
    const message = await refuse(fix, {
      nation: 'English',
      leader: 'Ada',
      male: true,
      female: true,
    });
    expect(message).toBe('just start takes at most one of --male or --female');
    expect(recorded.steps).toEqual([]);
  });

  test('with no arguments it resolves every choice itself', async () => {
    const recorded = startResponder();
    const drawn: ReadonlyArray<string>[] = [];
    const fix = fixture(recorded, {
      choose: (items) => {
        drawn.push(items.map((entry) => entry.name));
        const last = items[items.length - 1];
        if (last === undefined) throw new Error('the catalog was empty');
        return last;
      },
    });
    const lines = await run(fix);

    // The nation is drawn from what the lobby actually offers, sorted, so
    // seeding the RNG reproduces the pick.
    expect(drawn[0]).toEqual(['English', 'Zulu']);
    expect(recorded.steps).toEqual([
      'health',
      'nations',
      'overview',
      'legal',
      'batch',
      'legal',
      'batch',
    ]);
    expect(argumentsOf(recorded.bodies[0] ?? {})).toEqual({
      nation_id: NATION_ZULU,
      // The controller label, reduced to what Freeciv accepts.
      leader_name: 'codex-test-model',
      // The seat's own lobby default, not a client invention.
      is_male: false,
      style_id: STYLE_EUROPEAN,
    });
    expect(lines[0]).toBe(
      'starting as Zulu — codex-test-model (female), style the nation default'
    );
  });

  test('each flag overrides exactly what it names and nothing else', async () => {
    const recorded = startResponder();
    const fix = fixture(recorded, {
      choose: () => {
        throw new Error('a named nation must never draw');
      },
    });
    await run(fix, { nation: 'english', male: true });
    // A named nation never draws, and a named sex never reads the lobby
    // overview: only what is missing is fetched.
    expect(recorded.steps).not.toContain('overview');
    expect(argumentsOf(recorded.bodies[0] ?? {})).toEqual({
      nation_id: NATION_ENGLISH,
      leader_name: 'codex-test-model',
      is_male: true,
      style_id: STYLE_EUROPEAN,
    });
  });

  test('a named style is resolved against its own catalog and printed by name', async () => {
    const recorded = startResponder();
    const fix = fixture(recorded);
    const lines = await run(fix, {
      nation: 'English',
      leader: 'Ada',
      style: 'european',
      male: true,
    });
    expect(recorded.steps.slice(0, 3)).toEqual(['health', 'nations', 'styles']);
    expect(lines[0]).toBe('starting as English — Ada (male), style European');
    expect(argumentsOf(recorded.bodies[0] ?? {})).toEqual({
      nation_id: NATION_ENGLISH,
      leader_name: 'Ada',
      is_male: true,
      style_id: STYLE_EUROPEAN,
    });
  });

  test('an unknown style is refused with its own catalog quoted back', async () => {
    const fix = fixture(startResponder());
    const message = await refuse(fix, { nation: 'English', style: 'Martian', male: true });
    expect(message).toBe("no style named 'Martian' is offered; try one of: European");
  });

  test('an unusable controller label falls back to the leader the lobby holds', async () => {
    const recorded = startResponder({ leader: 'Boudica', sex: 'male', controller: '***' });
    const fix = fixture(recorded, { controller: '***' });
    await run(fix);
    expect(argumentsOf(recorded.bodies[0] ?? {})).toMatchObject({
      leader_name: 'Boudica',
      is_male: true,
    });
  });

  test('the same zero-argument command picks the same sex twice', async () => {
    const chosen: unknown[] = [];
    for (const _run of [0, 1]) {
      // The lobby volunteers no usable sex, so the fallback is a pure function
      // of the resolved leader name.
      const recorded = startResponder({ sex: 'unspecified' });
      const fix = fixture(recorded);
      await run(fix);
      const values = argumentsOf(recorded.bodies[0] ?? {});
      chosen.push(isJsonObject(values) ? values['is_male'] : null);
    }
    expect(chosen[0]).toBe(chosen[1] as boolean);
    const digest = new Bun.CryptoHasher('sha256').update('codex-test-model').digest();
    expect(chosen[0]).toBe((digest[0] ?? 0) % 2 !== 0);
  });

  test('a lobby that offers no nation fails closed before any batch', async () => {
    const recorded = startResponder({ nations: [] });
    const fix = fixture(recorded);
    const message = await refuse(fix);
    expect(message).toContain('just state --section pregame_nations');
    expect(recorded.steps).not.toContain('batch');
  });

  test('a game that has already left the lobby is told so, and told what to run', async () => {
    const recorded = startResponder({ gameState: 'running' });
    const fix = fixture(recorded);
    const message = await refuse(fix, { nation: 'English', leader: 'Ada', male: true });
    expect(message).toBe(
      'just start configures a lobby seat; this game is running -- run `just turn`'
    );
    expect(recorded.steps).toEqual(['health']);
  });

  test('--leader is bounded by the byte budget, not the character count', async () => {
    const recorded = startResponder();
    const fix = fixture(recorded);
    const message = await refuse(fix, { leader: 'é'.repeat(24) });
    expect(message).toBe('--leader must be at most 47 UTF-8 bytes');
    expect(recorded.steps).toEqual([]);
  });

  test('--json prints the composite payload and no prose', async () => {
    const recorded = startResponder();
    const fix = fixture(recorded);
    const out = await run(fix, { nation: 'English', leader: 'Ada', female: true, json: true });
    expect(out).toHaveLength(1);
    const payload: unknown = JSON.parse(out[0] ?? '');
    expect(payload).toMatchObject({
      schema_version: 1,
      command: 'start',
      nation: 'English',
      leader: 'Ada',
      is_male: false,
      style_id: STYLE_EUROPEAN,
    });
    expect(isJsonObject(payload) && Array.isArray(payload['dispositions'])).toBe(true);
  });

  /**
   * The regression guard for PORT_MAP's U18 addendum: `startCommand` is
   * `startCommandWith(liveStartHooks)`, so a `liveStartHooks` that still
   * refuses at a seam is a `play start` that cannot claim a seat at all — the
   * one thing the command exists to do.  This runs the *shipped* bundle
   * against the same fake supervisor the hand-built hooks use, so every seam
   * it names is exercised for real: enumerate, persist, POST, RE-ENUMERATE,
   * enumerate, persist, POST.
   */
  test('the shipped hooks configure and ready the seat, re-enumerating between', async () => {
    const recorded = startResponder();
    const fix = fixture(recorded);
    const out: string[] = [];
    const original = console.log;
    console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
    let outcome: Either.Either<void, PlayError | ExitCodeSignal>;
    try {
      outcome = await Effect.runPromise(
        Effect.either(
          Effect.provide(
            runStart(
              startOptions(fix.sessionPath, { nation: 'English', leader: 'Ada', male: true }),
              liveStartHooks
            ),
            fix.layer
          )
        )
      );
    } finally {
      console.log = original;
    }
    expect(Either.isRight(outcome)).toBe(true);
    expect(recorded.steps).toEqual([
      'health',
      'nations',
      'legal',
      'batch',
      'legal',
      'batch',
    ]);
    expect(commandOf(recorded.bodies[0] ?? {})).toEqual({
      action_id: String(CONFIGURE['action_id']),
      arguments: {
        nation_id: NATION_ENGLISH,
        leader_name: 'Ada',
        is_male: true,
        style_id: STYLE_EUROPEAN,
      },
    });
    expect(commandOf(recorded.bodies[1] ?? {})).toEqual({
      action_id: String(SET_READY['action_id']),
      arguments: { ready: true },
    });
    // U14's real `_render_disposition`, not the fixture's one-line stand-in.
    expect(out[0]).toBe('starting as English — Ada (male), style the nation default');
    expect(out[1]?.startsWith('configure English Ada male → applied')).toBe(true);
    expect(out[2]?.startsWith('set ready → applied')).toBe(true);
  });

  test('each enumeration carries the remedy that unblocks exactly its own failure', async () => {
    const recorded = startResponder();
    const remedies: string[] = [];
    const fix = withHooks(fixture(recorded), (hooks) => ({
      ...hooks,
      resolveKindAction: (kind, remedy) => {
        remedies.push(remedy);
        return hooks.resolveKindAction(kind, remedy);
      },
    }));
    await run(fix, { nation: 'English', leader: 'Ada', male: true });
    expect(remedies).toEqual([
      'this seat may already be ready -- run `just legal --kind pregame.set_ready ' +
        '--all` and withdraw readiness before configuring again',
      'run `just legal --kind pregame.set_ready --all` once the lobby offers ' +
        'readiness, then `just batch` its action_id',
    ]);
  });

  test('an unproven configure is never readied, and its status leaves quietly', async () => {
    const recorded = startResponder();
    const fix = withHooks(fixture(recorded), (hooks, session) => ({
      ...hooks,
      submitPersistedBatch: (batchId) =>
        Effect.map(batchDisposition(session, batchId, 'receipt_first'), (disposition) => ({
          disposition,
          warning: `transport outcome is unknown for batch ${batchId}.`,
          exitCode: 2,
        })),
    }));
    const captured = await runRaw(fix, { nation: 'English', leader: 'Ada', male: true });

    // The seat was configured-or-not; readying on top of that would be a
    // second unproven mutation.
    expect(captured.out.at(-1)).toBe(NOT_READIED_LINE);
    expect(recorded.steps.filter((step) => step === 'legal')).toHaveLength(1);
    // The warning is stderr's, never stdout's.
    expect(captured.err[0]).toContain('transport outcome is unknown');
    // And the status carries no second `error: …` sentence with it.
    expect(Either.isLeft(captured.outcome)).toBe(true);
    if (Either.isLeft(captured.outcome)) {
      expect(captured.outcome.left._tag).toBe('ExitCodeSignal');
    }
  });

  /**
   * `_mirror_health` (client.py:3062-3072) passes `commands=V2_PROTOCOL_CARD`
   * unconditionally, so the `state/header.txt` that `command_start`'s health
   * probe writes ends in the whole card — not `_DEFAULT_COMMAND_CARD`.  `just
   * show` and `just show header` print that file verbatim (client.py:11170
   * maps the name to it), which puts it on the offline byte-diff oracle's
   * read-only path (PLAN §The oracle item 2).  Forwarding no options silently
   * downgrades the header to five lines and costs the agent the
   * ALIASES/ERRORS/ONE CALL PER TURN/MULTIPLAYER/WHICH BINDING block.  The
   * `wait` half of this assertion is `test/pvp-wait-interop.test.ts`; the
   * Python half is `test_v2_join_card_and_state_header_carry_the_same_contract`
   * (tests/test_client.py:7194-7254).
   */
  test("start's health probe writes the full protocol card into state/header.txt", async () => {
    const fix = fixture(startResponder());
    await run(fix, { nation: 'English', leader: 'Ada', male: true });

    const dir = Effect.runSync(mirrorDir(fix.sessionPath));
    const header = fs.readFileSync(path.join(dir, 'state', 'header.txt'), 'utf-8');
    for (const line of V2_PROTOCOL_CARD) expect(header).toContain(line);
    // The default card is only ever the fallback for a dropped argument; none
    // of its lines belongs in a header this client wrote.
    for (const line of DEFAULT_COMMAND_CARD) expect(header).not.toContain(line);
  });
});
