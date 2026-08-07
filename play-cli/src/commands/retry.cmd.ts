/**
 * `play retry` — resolve a persisted batch without ever guessing.
 *
 * Ports `_command_retry_locked` (client.py:9837-9906) and `command_retry`
 * (9908-9914), plus its argparse surface (11789-11793).
 *
 * The name is a lie the command refuses to live up to.  `retry` does **not**
 * re-send: it re-reads the batch this client persisted before the first POST,
 * asks the server what became of it, and re-derives the disposition from the
 * answer.  It reaches the wire with a batch body in exactly one situation —
 * the server says the batch does not exist (404 `invalid_request`) *and* this
 * client has never seen a receipt for it.  Anything else is resolved, not
 * replayed:
 *
 * - a cached `applied`/`rejected`/`ambiguous` receipt is terminal and printed
 *   from the cache, with **no request at all**;
 * - an `accepted` receipt is polled for up to 30 s, and if it then vanishes it
 *   is terminalized as `ambiguous` rather than re-sent.
 *
 * That last case is the one that loses games, and it is why the seat is
 * resolved exactly once and the whole body runs under the request lock: a
 * workspace that gained a second seat between two resolutions would lock one
 * and refuse the other.
 */
import { Command, Options } from '@effect/cli';
import { Console, Effect } from 'effect';
import type {
  DriftError,
  LockTimeoutError,
  PlayerError,
  SessionMissingError,
} from 'src/errors';
import { V2ResponseError } from 'src/errors';
import { ExitCodeSignal, exitWith } from 'src/exit';
import { dualText, resolveDualRequired } from 'src/options';
import { render } from 'src/render/primitives';
import { renderDisposition, renderReceipt } from 'src/render/receipt';
import { decodeError, v2ErrorMessage } from 'src/schema/error';
import { opaque } from 'src/schema/primitives';
import { decodeReceipt, type CommandReceipt } from 'src/schema/receipt';
import { warnIfAmbiguous } from 'src/commands/receipt.cmd';
import { isTerminalReceipt } from 'src/services/disposition';
import { jsonRequested, printV2Json } from 'src/services/json-output';
import { PrivateFs } from 'src/services/private-fs';
import {
  ACCEPTED_RECEIPT_VANISHED,
  RETRY_POLL_DEADLINE_S,
  RETRY_POLL_INTERVAL_S,
  getReceiptResponse,
  liveReceiptHooks,
  missingAcceptedReceipt,
  noPersistedBatch,
  systemRetryClock,
  type ReceiptHooksFor,
  type RetryClock,
} from 'src/services/receipts';
import { SessionStore, type Session } from 'src/services/session-store';
import { V2Client } from 'src/services/v2-client';

export interface RetryOptions {
  readonly session: string;
  readonly batchId: string;
  readonly json: boolean;
}

export type RetryError =
  | PlayerError
  | DriftError
  | V2ResponseError
  | SessionMissingError
  | LockTimeoutError;

/**
 * The whole command, under the request lock, returning CPython's exit status.
 *
 * Exported so a test can drive every branch without a lock file or a process
 * exit; `runRetry` is the same thing plus the lock and the status signal.
 */
export const retryLocked = (
  path: string,
  session: Session,
  options: RetryOptions,
  makeHooks: ReceiptHooksFor = liveReceiptHooks,
  clock: RetryClock = systemRetryClock
): Effect.Effect<number, RetryError, SessionStore | V2Client | PrivateFs> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const batchId = yield* opaque(options.batchId.trim(), 'batch ID');
    const state = yield* store.readState(path, session);
    // Nothing was ever persisted for this ID, so there is nothing to resolve
    // and — crucially — nothing this command could legitimately send.
    if (!Object.prototype.hasOwnProperty.call(state.batches, batchId)) {
      return yield* Effect.fail(noPersistedBatch(batchId));
    }
    const hooks = yield* makeHooks(path, session);
    const intent = hooks.batchIntent(state, batchId);
    const json = jsonRequested('retry', options.json);

    const emitReceipt = (receipt: CommandReceipt): Effect.Effect<void> =>
      json ? printV2Json(receipt) : render(renderReceipt(receipt, intent));

    const cached = state.receipts[batchId];
    const cachedReceipt =
      cached === undefined ? null : yield* decodeReceipt(cached, session, { batchId });
    if (cachedReceipt !== null && isTerminalReceipt(cachedReceipt.receipt_state)) {
      // THE invariant: a terminal receipt — ambiguous above all — is answered
      // from the cache. No GET, no POST, no chance of a second application.
      yield* emitReceipt(cachedReceipt);
      yield* warnIfAmbiguous(cachedReceipt);
      return 0;
    }

    const deadline = (yield* clock.monotonic()) + RETRY_POLL_DEADLINE_S;

    /**
     * `accepted` carries the last receipt this client saw in that state.  It is
     * the sole reason a vanished receipt becomes `ambiguous` instead of a
     * re-send: once the server has said "accepted" even once, the batch is on
     * its side of the wire and replay is unsafe forever.
     */
    const poll = (
      accepted: CommandReceipt | null
    ): Effect.Effect<number, RetryError, SessionStore | V2Client | PrivateFs> =>
      Effect.gen(function* () {
        const response = yield* getReceiptResponse(session, batchId);
        if (response.status >= 200 && response.status < 300) {
          const receipt = yield* decodeReceipt(response.value, session, { batchId });
          yield* hooks.rememberReceipt(receipt);
          yield* hooks.mirrorReceipt(receipt, 'retry');
          if (receipt.receipt_state === 'accepted' && (yield* clock.monotonic()) < deadline) {
            yield* clock.sleep(RETRY_POLL_INTERVAL_S);
            return yield* poll(receipt);
          }
          yield* emitReceipt(receipt);
          yield* warnIfAmbiguous(receipt);
          return 0;
        }
        const error = yield* decodeError(response.value);
        // Only "this batch does not exist" is a licence to submit. Every other
        // refusal is raised as-is, because it says nothing about acceptance.
        if (response.status !== 404 || error.error.code !== 'invalid_request') {
          return yield* Effect.fail(
            new V2ResponseError({
              message: v2ErrorMessage(response.status, error),
              status: response.status,
              payload: error,
            })
          );
        }
        if (accepted !== null) {
          const terminal = yield* missingAcceptedReceipt(session, accepted, batchId);
          yield* hooks.rememberReceipt(terminal);
          yield* hooks.mirrorReceipt(terminal, 'retry');
          yield* emitReceipt(terminal);
          yield* Console.error(ACCEPTED_RECEIPT_VANISHED);
          return 0;
        }
        // The one send: persisted body, never accepted, server has no record.
        const outcome = yield* hooks.submitPersistedBatch(batchId);
        if (json) {
          yield* printV2Json(outcome.disposition);
        } else {
          yield* render(renderDisposition(outcome.disposition, intent));
        }
        if (outcome.warning !== null) yield* Console.error(outcome.warning);
        return outcome.exitCode;
      });

    return yield* poll(cachedReceipt);
  });

export const runRetry = (
  options: RetryOptions,
  makeHooks: ReceiptHooksFor = liveReceiptHooks,
  clock: RetryClock = systemRetryClock
): Effect.Effect<
  void,
  RetryError | ExitCodeSignal,
  SessionStore | V2Client | PrivateFs
> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    // The seat is resolved exactly once: a workspace that gained a second seat
    // between two resolutions would lock one and refuse the other.
    const { path, session } = yield* store.resolveV2(options.session);
    const code = yield* store.withRequestLock(
      path,
      retryLocked(path, session, options, makeHooks, clock)
    );
    // A non-zero status here is a disposition already printed and a warning
    // already on stderr — not an error, so it must not print `error: …`.
    if (code !== 0) return yield* Effect.fail(exitWith(code));
  });

// ---------------------------------------------------------------------------
// The command
// ---------------------------------------------------------------------------

const sessionOption = Options.text('session').pipe(
  Options.withDefault(''),
  Options.withDescription('the private session file this command acts against')
);

const jsonOption = Options.boolean('json').pipe(
  Options.withDescription('print the full-fidelity JSON payload instead of text')
);

/** Build the command against a given set of cross-unit hooks. */
export const retryCommandWith = (makeHooks: ReceiptHooksFor) =>
  Command.make(
    'retry',
    { session: sessionOption, batchId: dualText('batch-id'), json: jsonOption },
    (options) =>
      Effect.flatMap(resolveDualRequired('batch-id', options.batchId), (batchId) =>
        runRetry({ session: options.session, batchId, json: options.json }, makeHooks)
      )
  );

export const retryCommand = retryCommandWith(liveReceiptHooks);
