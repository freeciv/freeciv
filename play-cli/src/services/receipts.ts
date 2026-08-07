/**
 * Receipt fetching, terminalization and the seams `receipt`/`retry` reach
 * through.
 *
 * Ports `_get_receipt_response` (client.py:9777-9783), `_missing_accepted_receipt`
 * (9805-9836), and carries the two stderr sentences that path prints.
 *
 * The safety rule this file exists to enforce: **an accepted batch whose
 * authoritative receipt has vanished becomes `ambiguous`, not "gone, try
 * again".**  The server took it, the server cannot say what happened to it, and
 * re-sending it could apply the same order twice.  `_missing_accepted_receipt`
 * manufactures that terminal receipt locally and runs it through the real
 * validator, so the ambiguity is recorded in `.v2-state` exactly as a
 * server-sent one would be.
 */
import { Effect, Layer } from 'effect';
import { FULL_CONTROL_V2 } from 'src/constants';
import type { DriftError, LockTimeoutError, PlayerError, V2ResponseError } from 'src/errors';
import { playerError } from 'src/errors';
import { decodeReceipt, type CommandReceipt } from 'src/schema/receipt';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import { rememberReceipt } from 'src/services/aliases';
import { batchIntent, submitBatch, type BatchSubmission } from 'src/services/batch';
import type { JsonResponse } from 'src/services/http';
import { mirrorReceipt } from 'src/services/mirror/update-receipt';
import { PrivateFs } from 'src/services/private-fs';
import {
  SessionStore,
  credentialsOf,
  type Session,
  type V2ClientState,
} from 'src/services/session-store';
import { V2Client } from 'src/services/v2-client';

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/** `_get_receipt_response`'s own timeout — a receipt read is a probe, not work. */
export const RECEIPT_TIMEOUT_S = 10;

/** `_command_retry_locked`'s poll budget: 30 s of `accepted`, then give up. */
export const RETRY_POLL_DEADLINE_S = 30;

/** How long `retry` sleeps between two polls of an `accepted` receipt. */
export const RETRY_POLL_INTERVAL_S = 0.25;

/** Printed on stderr whenever an `ambiguous` receipt is rendered. */
export const AMBIGUOUS_IS_TERMINAL = 'Ambiguous is terminal; never replay this batch.';

/** Printed on stderr when an accepted batch's receipt has vanished. */
export const ACCEPTED_RECEIPT_VANISHED =
  'The accepted receipt disappeared. Its outcome is ambiguous ' +
  'and terminal; never replay this batch.';

/** `_missing_accepted_receipt`'s error message, verbatim. */
export const AMBIGUOUS_REPLAY_UNSAFE =
  'the server previously accepted this batch but no longer ' +
  'has its authoritative receipt; replay is unsafe';

/** `_command_retry_locked`'s refusal when nothing was ever persisted. */
export const noPersistedBatch = (batchId: string): PlayerError =>
  // CPython interpolates with `!r`, which single-quotes a plain ASCII ID.
  playerError(`no persisted command batch '${batchId}'`);

// ---------------------------------------------------------------------------
// _get_receipt_response (client.py:9777-9783)
// ---------------------------------------------------------------------------

/** GET `/receipts/{batch_id}`, every status included — a 404 is data here. */
export const getReceiptResponse = (
  session: Session,
  batchId: string
): Effect.Effect<JsonResponse, PlayerError, V2Client> =>
  Effect.gen(function* () {
    const client = yield* V2Client;
    const credentials = credentialsOf(session);
    const url = yield* client.url(credentials, `/receipts/${batchId}`);
    return yield* client.response('GET', url, credentials, { timeout: RECEIPT_TIMEOUT_S });
  });

// ---------------------------------------------------------------------------
// _missing_accepted_receipt (client.py:9805-9836)
// ---------------------------------------------------------------------------

/**
 * Terminalize an accepted command whose authoritative receipt vanished.
 *
 * Built as a wire payload and run back through {@link decodeReceipt} on
 * purpose: the validator is what proves an `ambiguous` receipt carries
 * `action_outcome_ambiguous` and is not retryable, and a locally minted receipt
 * that skipped it could smuggle a replayable ambiguity into `.v2-state`.
 */
export const missingAcceptedReceipt = (
  session: Session,
  cached: CommandReceipt,
  batchId: string
): Effect.Effect<CommandReceipt, DriftError> => {
  const revision: JsonValue = { ...cached.state_revision };
  const payload: JsonObject = {
    schema_version: 2,
    control_protocol: FULL_CONTROL_V2,
    game_id: session.gameId,
    agent_id: session.agentId,
    batch_id: batchId,
    receipt_state: 'ambiguous',
    idempotent: cached.idempotent,
    state_revision: revision,
    error: {
      schema_version: 2,
      control_protocol: FULL_CONTROL_V2,
      error: {
        code: 'action_outcome_ambiguous',
        message: AMBIGUOUS_REPLAY_UNSAFE,
        retryable: false,
        details: {},
      },
      state_revision: revision,
    },
    observation: null,
  };
  return decodeReceipt(payload, session, { batchId });
};

// ---------------------------------------------------------------------------
// The clock `retry` polls against
// ---------------------------------------------------------------------------

/**
 * `time.monotonic` and `time.sleep`, injectable.
 *
 * The thing under test in `retry` is *how long it is willing to keep an
 * accepted receipt open* — 30 s, at 0.25 s a poll.  With the real clock that is
 * a 30-second test, so the clock is a parameter, exactly as U05's `WaitClock`
 * is.  Seconds, not milliseconds, because CPython's is.
 */
export interface RetryClock {
  readonly monotonic: () => Effect.Effect<number>;
  readonly sleep: (seconds: number) => Effect.Effect<void>;
}

export const systemRetryClock: RetryClock = {
  monotonic: () => Effect.sync(() => performance.now() / 1000),
  sleep: (seconds) => Effect.sleep(`${Math.max(0, Math.round(seconds * 1000))} millis`),
};

// ---------------------------------------------------------------------------
// The cross-unit seam
// ---------------------------------------------------------------------------

/**
 * `_submit_persisted_batch`'s three-tuple (client.py:8474-8544).
 *
 * U13 owns the implementation and the type; this alias exists so `retry`'s
 * signature reads in its own vocabulary.
 */
export type SubmitOutcome = BatchSubmission;

export type SubmitError = PlayerError | DriftError | V2ResponseError | LockTimeoutError;

/**
 * How `receipt` and `retry` reach the units they do not own.
 *
 * The hooks are `Effect`s with **no** requirements: the command captures
 * `PrivateFs`/`SessionStore`/`V2Client` from the CLI's Layer stack and provides
 * them back per call, exactly as U05's `WaitHooks` does.  That keeps the
 * orchestration testable without a filesystem or a fake server.
 */
export interface ReceiptHooks {
  /** U13's `batchIntent` — the local-cache-only name for a persisted batch. */
  readonly batchIntent: (state: V2ClientState, batchId: string) => string;
  /** U03's `rememberReceipt` — the local receipt ledger and revision bump. */
  readonly rememberReceipt: (
    receipt: CommandReceipt
  ) => Effect.Effect<void, PlayerError | LockTimeoutError>;
  /** U04/U07's `_mirror_receipt` — append this receipt to `state/delta.md`. */
  readonly mirrorReceipt: (receipt: CommandReceipt, command: string) => Effect.Effect<void>;
  /** U13's `_submit_persisted_batch` — the ONLY path allowed to re-send. */
  readonly submitPersistedBatch: (batchId: string) => Effect.Effect<SubmitOutcome, SubmitError>;
}

export type ReceiptHooksFor = (
  sessionPath: string,
  session: Session
) => Effect.Effect<ReceiptHooks, never, PrivateFs | SessionStore | V2Client>;

/**
 * The live seams.
 *
 * `batchIntent` and `submitBatch` are U13's, `rememberReceipt` is U03's and
 * `mirrorReceipt` is U07's — `command_receipt` (client.py:9797) and
 * `_command_retry_locked` (9884, 9899) each call `_mirror_receipt` next to
 * their `_remember_receipt`, and the `applied batch … at rev N` entry it
 * appends to `state/delta.md` is what `just show delta` prints back.
 */
export const liveReceiptHooks: ReceiptHooksFor = (sessionPath, session) =>
  Effect.gen(function* () {
    const files = yield* PrivateFs;
    const store = yield* SessionStore;
    const client = yield* V2Client;
    const provided = Layer.mergeAll(
      Layer.succeed(PrivateFs, files),
      Layer.succeed(SessionStore, store),
      Layer.succeed(V2Client, client)
    );
    return {
      batchIntent,
      rememberReceipt: (receipt) =>
        Effect.provide(Effect.asVoid(rememberReceipt(sessionPath, session, receipt)), provided),
      mirrorReceipt: (receipt, command) =>
        Effect.provideService(mirrorReceipt(sessionPath, receipt, command), PrivateFs, files),
      submitPersistedBatch: (batchId) =>
        Effect.provide(submitBatch(sessionPath, session, batchId), provided),
    };
  });
