/**
 * The shared harness for U14's three suites.
 *
 * `receipt.test.ts`, `retry.test.ts` and `ambiguous.test.ts` all need the same
 * three things — the wire shapes `test_client.py`'s own `receipt()`/`error()`
 * classmethods produce, a scratch workspace with a real `.v2-state`, and stdout
 * *and* stderr captured separately (this unit's whole point is that the
 * ambiguity warning is on stderr while the payload is on stdout).
 *
 * It is not a `*.test.ts` file, so `bun test` never collects it as a suite.
 */
import * as path from 'node:path';
import { Effect, Either, Layer } from 'effect';
import { FULL_CONTROL_V2 } from 'src/constants';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import type { CommandReceipt } from 'src/schema/receipt';
import { v2StateSchema } from 'src/services/aliases';
import { httpFor } from 'src/services/http';
import { canonicalJson } from 'src/services/json-output';
import { PrivateFs } from 'src/services/private-fs';
import {
  liveReceiptHooks,
  type ReceiptHooks,
  type ReceiptHooksFor,
  type RetryClock,
} from 'src/services/receipts';
import {
  SessionStore,
  emptyV2ClientState,
  sessionStoreFor,
  type Session,
  type SessionStoreApi,
  type V2ClientState,
} from 'src/services/session-store';
import { V2Client, v2ClientFor } from 'src/services/v2-client';
import {
  FIXTURE_AGENT_ID,
  FIXTURE_GAME_ID,
  recordingFetch,
  scratchWorkspace,
  sessionFile,
  type FakeRoute,
  type RecordedRequest,
  type Scratch,
} from 'test/_fixtures';

// ---------------------------------------------------------------------------
// Wire shapes
// ---------------------------------------------------------------------------

/** `test_client.py`'s `revision(8)` — printed as `rev8/t3`. */
export const REVISION: JsonObject = {
  turn: 3,
  revision: 8,
  state_token: `state_${'0'.repeat(26)}`,
};

export const BATCH_ID = `batch_${'R'.repeat(24)}`;

/** `test_client.py`'s `error()` classmethod. */
export const errorEnvelope = (
  code = 'invalid_request',
  revision: JsonValue = REVISION,
  retryable = false
): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  error: { code, message: 'validated test error', retryable, details: {} },
  state_revision: revision,
});

/** `test_client.py`'s `receipt()` classmethod. */
export const receiptWire = (
  batchId: string,
  state = 'applied',
  overrides: JsonObject = {}
): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  batch_id: batchId,
  receipt_state: state,
  idempotent: false,
  state_revision: REVISION,
  error:
    state === 'rejected'
      ? errorEnvelope('illegal_action')
      : state === 'ambiguous'
        ? errorEnvelope('action_outcome_ambiguous')
        : null,
  observation: null,
  ...overrides,
});

/** `test_client.py`'s `descriptor()` classmethod. */
export const DESCRIPTOR: JsonObject = {
  action_id: 'action_opaque',
  kind: 'phase.end',
  label: 'End phase',
  subject: { operation: 'end' },
  arguments_schema: { type: 'object' },
  state_revision: REVISION,
};

/** The exact bytes `_persist_batch_for_action` would have written. */
export const batchBody = (batchId: string, args: JsonObject = { ready: true }): string =>
  canonicalJson({
    schema_version: 2,
    control_protocol: FULL_CONTROL_V2,
    game_id: FIXTURE_GAME_ID,
    agent_id: FIXTURE_AGENT_ID,
    batch_id: batchId,
    state_revision: REVISION,
    commands: [{ action_id: 'action_opaque', arguments: args }],
  });

// ---------------------------------------------------------------------------
// The workspace
// ---------------------------------------------------------------------------

const scratches: Scratch[] = [];

/** Call from an `afterEach`. */
export const cleanupScratches = (): void => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
};

export interface Fixture {
  readonly scratch: Scratch;
  readonly store: SessionStoreApi;
  readonly sessionPath: string;
  readonly session: Session;
  readonly layer: Layer.Layer<SessionStore | V2Client | PrivateFs>;
  /** Everything that reached the wire — "it made no request" is an assertion. */
  readonly requests: ReadonlyArray<RecordedRequest>;
  readonly seed: (state: Partial<V2ClientState>) => void;
  readonly readState: () => V2ClientState;
}

export const buildFixture = (
  routes: ReadonlyMap<string, FakeRoute> | ReadonlyArray<FakeRoute>
): Fixture => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const store = sessionStoreFor(scratch.workspace, scratch.files, v2StateSchema, {});
  const sessionPath = path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID, 'seat.json');
  Effect.runSync(
    scratch.files.writeJson(sessionPath, sessionFile({ control_protocol: FULL_CONTROL_V2 }))
  );
  const session = Effect.runSync(store.resolveV2(sessionPath)).session;
  const server = recordingFetch(routes);
  const client = v2ClientFor(httpFor(server.fetch), () => Effect.void);
  return {
    scratch,
    store,
    sessionPath,
    session,
    requests: server.requests,
    layer: Layer.mergeAll(
      Layer.succeed(SessionStore, store),
      Layer.succeed(V2Client, client),
      Layer.succeed(PrivateFs, scratch.files)
    ),
    seed: (overrides) =>
      Effect.runSync(
        store.writeState(sessionPath, { ...emptyV2ClientState(session), ...overrides })
      ),
    readState: () => Effect.runSync(store.readState(sessionPath, session)),
  };
};

/**
 * A clock the test drives by hand.
 *
 * `retry` polls an accepted receipt for 30 s at 0.25 s a turn; with the real
 * clock the deadline case is a 30-second test. `advanceOnSleep` makes every
 * sleep move the clock, which is what makes "it gives up after 30 s" assertable
 * in microseconds.
 */
export interface TestClock extends RetryClock {
  readonly sleeps: ReadonlyArray<number>;
  readonly set: (seconds: number) => void;
}

export const testClock = (options: { readonly advanceOnSleep?: number } = {}): TestClock => {
  const sleeps: number[] = [];
  const state = { now: 0 };
  return {
    sleeps,
    set: (seconds) => {
      state.now = seconds;
    },
    monotonic: () => Effect.sync(() => state.now),
    sleep: (seconds) =>
      Effect.sync(() => {
        sleeps.push(seconds);
        state.now += options.advanceOnSleep ?? 0;
      }),
  };
};

/** An empty `.v2-state` for the pure-function suites, with no workspace at all. */
export const bareState = (overrides: Partial<V2ClientState> = {}): V2ClientState => ({
  schema_version: 5,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  last_revision: null,
  actions: {},
  pending_catalogs: {},
  batches: {},
  receipts: {},
  action_aliases: { state_revision: null, by_alias: {} },
  entity_aliases: {},
  tile_aliases: {},
  drained_actors: [],
  ...overrides,
});

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

export interface Recorded<A, E> {
  readonly out: ReadonlyArray<string>;
  readonly err: ReadonlyArray<string>;
  readonly result: Either.Either<A, E>;
}

/** Run one effect with stdout and stderr captured separately. */
export const capture = async <A, E>(
  effect: Effect.Effect<A, E, SessionStore | V2Client | PrivateFs>,
  fixture: Fixture
): Promise<Recorded<A, E>> => {
  const out: string[] = [];
  const err: string[] = [];
  const log = console.log;
  const error = console.error;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
  console.error = (...parts: ReadonlyArray<unknown>) => err.push(parts.join(' '));
  try {
    const result = await Effect.runPromise(
      Effect.either(Effect.provide(effect, fixture.layer))
    );
    return { out, err, result };
  } finally {
    console.log = log;
    console.error = error;
  }
};

export const messageOf = (value: unknown): string =>
  typeof value === 'object' && value !== null && 'message' in value
    ? String((value as { readonly message: unknown }).message)
    : String(value);

export const tagOf = (value: unknown): string =>
  typeof value === 'object' && value !== null && '_tag' in value
    ? String((value as { readonly _tag: unknown })._tag)
    : '';

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

export interface HookLog {
  readonly make: ReceiptHooksFor;
  readonly remembered: ReadonlyArray<CommandReceipt>;
  readonly mirrored: ReadonlyArray<readonly [string, string]>;
  readonly submits: ReadonlyArray<string>;
}

/**
 * The live hooks, wrapped so a test can see what the command asked for.
 *
 * `rememberReceipt` still writes `.v2-state` for real: the whole point of the
 * ambiguity tests is that the terminal state survives to the next invocation.
 */
export const recordingHooks = (overrides: Partial<ReceiptHooks> = {}): HookLog => {
  const remembered: CommandReceipt[] = [];
  const mirrored: Array<readonly [string, string]> = [];
  const submits: string[] = [];
  const make: ReceiptHooksFor = (sessionPath, session) =>
    Effect.map(liveReceiptHooks(sessionPath, session), (live): ReceiptHooks => {
      const base: ReceiptHooks = { ...live, ...overrides };
      return {
        ...base,
        rememberReceipt: (receipt) =>
          Effect.flatMap(
            Effect.sync(() => {
              remembered.push(receipt);
            }),
            () => base.rememberReceipt(receipt)
          ),
        mirrorReceipt: (receipt, command) =>
          Effect.sync(() => {
            mirrored.push([receipt.batch_id, command]);
          }),
        submitPersistedBatch: (batchId) =>
          Effect.flatMap(
            Effect.sync(() => {
              submits.push(batchId);
            }),
            () => base.submitPersistedBatch(batchId)
          ),
      };
    });
  return { make, remembered, mirrored, submits };
};
