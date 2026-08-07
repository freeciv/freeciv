/**
 * Submitting one persisted `CommandBatch`, and reading its outcome.
 *
 * Ports `_limit` (client.py:6513-6518), `_parse_json_object` (6521-6541),
 * `_batch_intent` (5771-5799), `_batch_disposition` (8338-8380),
 * `_batch_error_disposition` (8383-8420) and `_submit_persisted_batch`
 * (8474-8544).
 *
 * The contract this file exists to keep: **a mutation is never sent twice on a
 * guess.**  Every path out of {@link submitBatch} yields a
 * {@link BatchDisposition} that names what the caller is allowed to do next,
 * and the only two that permit another request are the ones the server itself
 * proved safe:
 *
 * - `receipt_first` — the outcome is unknown (transport died, the response did
 *   not decode, or the refusal did not carry its recovery contract).  Resolve
 *   by receipt; never replay.
 * - `retry_exact` — the server proved the batch was not accepted *and* said the
 *   identical bytes may be sent again (a rate limit, or a sidecar that was down).
 * - `refresh` — not accepted, and the world moved: re-enumerate first.
 * - `receipt_poll` / `receipt_terminal` — there is an authoritative receipt.
 *
 * `_v2_response`'s busy backoff lives in core's `V2Client` and deliberately does
 * **not** retry a POST: a retry there would be exactly the blind replay this
 * disposition machinery exists to prevent.
 */
import { Effect, Either } from 'effect';
import { FULL_CONTROL_V2, V2_DISPOSITIONS, V2_TERMINAL_RECEIPTS } from 'src/constants';
import {
  playerError,
  type DriftError,
  type LockTimeoutError,
  type PlayerError,
} from 'src/errors';
import type { BatchDisposition, Disposition } from 'src/schema/batch';
import { decodeError, type StructuredError } from 'src/schema/error';
import { isJsonObject, opaque } from 'src/schema/primitives';
import {
  isPyMapping,
  parsePython,
  pyJsonObject,
  pyScalar,
  type PyObject,
  type PyParseFailure,
  type PyValue,
} from 'src/services/canonical-body';
import { decodeReceipt, type CommandReceipt } from 'src/schema/receipt';
import type { JsonResponse } from 'src/services/http';
import { rememberReceipt } from 'src/services/aliases';
// U04's barrel does not re-export the receipt bridge; the module that owns
// `update_from_receipt` does (PORT_MAP §8's U07 addendum).
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
// _limit (client.py:6513-6518)
// ---------------------------------------------------------------------------

const LIMIT_RE = /^(?:[1-9]|1[0-6])$/;

/**
 * A server page size, as the agent typed it.
 *
 * "Canonical" is the point: `08` and `+8` name the same number to `int()` and
 * would produce two different cache keys for one page, so only the one spelling
 * is accepted.
 *
 * PORT_MAP routes this to U13 with `_parse_json_object` (they are adjacent in
 * the Python and share no caller); `state` (U10) and `legal` (U11) import it
 * from here rather than restating the sentence.
 */
export const pageLimit = (
  value: string | null | undefined,
  fallback = 16
): Effect.Effect<number, PlayerError> => {
  if (value === null || value === undefined) return Effect.succeed(fallback);
  return LIMIT_RE.test(value)
    ? Effect.succeed(Number.parseInt(value, 10))
    : Effect.fail(playerError('limit must be a canonical integer from 1 through 16'));
};

// ---------------------------------------------------------------------------
// _parse_json_object (client.py:6521-6541)
// ---------------------------------------------------------------------------

/**
 * `--arguments` — strict JSON, an object, and closed against the wire's limits.
 *
 * The value that comes back is a {@link PyValue} tree, not a `JsonValue` one:
 * every number remembers whether its literal was an `int` or a `float`, because
 * the object is embedded verbatim in the canonical body and `40` and `40.0` are
 * two different idempotency keys on the wire (NOTES.md §17.9).
 *
 * The syntax refusal is the one an agent meets most often — a typo in
 * `--arguments` is the whole failure mode — so it is CPython's sentence and not
 * the host's: `{"a":}` answers `Expecting value: line 1 column 6 (char 5)`, with
 * the position that names the character to fix.  `parsePython` produces the
 * text; see NOTES.md §17.1.
 */
const failureText = (failure: PyParseFailure, label: string): string => {
  switch (failure._tag) {
    // `JSONDecodeError` and `parse_constant`'s `ValueError` are one `except`.
    case 'decode':
    case 'constant':
      return `${label} must be valid strict JSON: ${failure.message}`;
    // `object_pairs_hook` raises a `PlayerError`, which is a `RuntimeError` and
    // so is not caught by that `except` at all.
    case 'duplicate':
      return `${label} must not contain duplicate keys`;
    // `_json_value`'s own sentence, arriving one pass earlier so that a
    // pathological document costs a refusal and not a stack.
    case 'too_deep':
      return `invalid ${label}: JSON is nested too deeply`;
  }
};

export const parseJsonObject = (
  value: string,
  label: string
): Effect.Effect<PyObject, PlayerError> =>
  Effect.gen(function* () {
    const parsed = parsePython(value, { hooks: true });
    if (parsed.failure !== null) {
      return yield* Effect.fail(playerError(failureText(parsed.failure, label)));
    }
    if (!isPyMapping(parsed.value)) {
      return yield* Effect.fail(playerError(`${label} must be a JSON object`));
    }
    return yield* pyJsonObject(parsed.value, label);
  });

// ---------------------------------------------------------------------------
// _batch_intent (client.py:5771-5799)
// ---------------------------------------------------------------------------

/**
 * Name what a persisted batch asked for, from the local cache only.
 *
 * "From the local cache only" is the whole design: `receipt` and `retry` print
 * this next to an outcome, and both may run long after the revision that issued
 * the action retired.  Anything it cannot read degrades to a weaker name — the
 * action ID, or the bare word `batch` — and never to a request.
 *
 * The persisted text is re-read through {@link parsePython}, not `JSON.parse`,
 * because `_scalar`'s two numeric branches are not the same function: an `int`
 * prints as `str(value)` and a `float` through `%g`, so a persisted
 * `"tax":1234567.0` prints `tax=1.23457e+06` and a persisted `"tax":1234567`
 * prints `tax=1234567`.  `JSON.parse` cannot tell the two apart.
 */
export const batchIntent = (state: V2ClientState, batchId: string): string => {
  const encoded = state.batches[batchId];
  if (typeof encoded !== 'string') return 'batch';
  // `_batch_intent` is `json.loads` with no hook: a repeated key is last-wins
  // here, because refusing one would only lose a name it could have printed.
  const parsed = parsePython(encoded);
  const body: PyValue = parsed.failure === null ? parsed.value : null;
  const commands = isPyMapping(body) ? body['commands'] : null;
  if (!Array.isArray(commands) || commands.length !== 1) return 'batch';
  const command = commands[0];
  if (command === undefined || !isPyMapping(command)) return 'batch';
  const actionId = command['action_id'];
  if (typeof actionId !== 'string') return 'batch';
  const descriptor = state.actions[actionId];
  const kind = isJsonObject(descriptor) ? descriptor['kind'] : null;
  const descriptorLabel = isJsonObject(descriptor) ? descriptor['label'] : null;
  const named =
    typeof kind === 'string' && typeof descriptorLabel === 'string'
      ? `${kind} ${descriptorLabel}`
      : actionId;
  const args = command['arguments'] ?? null;
  if (!isPyMapping(args)) return named;
  const entries = Object.entries(args);
  if (entries.length === 0) return named;
  return `${named} {${entries
    .map(([key, item]) => `${key}=${pyScalar(item ?? null)}`)
    .join(',')}}`;
};

// ---------------------------------------------------------------------------
// _batch_disposition (client.py:8338-8380)
// ---------------------------------------------------------------------------

export interface DispositionParts {
  readonly receipt?: unknown;
  readonly error?: unknown;
}

const SAFE_NEXT: ReadonlySet<string> = new Set(['refresh', 'retry_exact', 'receipt_first']);

/** Codes whose outcome cannot be known without the authoritative receipt. */
const RECEIPT_FIRST_CODES: ReadonlySet<string> = new Set([
  'conflict',
  'internal_error',
  'action_outcome_ambiguous',
]);

/**
 * Build the CLI-side disposition, refusing every combination that would lie.
 *
 * The payload rules are not decoration: `receipt_poll` carrying an `applied`
 * receipt would tell a caller to keep polling a finished command, and
 * `retry_exact` carrying a receipt would invite a resend of a batch the server
 * already answered.
 */
export const batchDisposition = (
  session: Session,
  batchId: string,
  disposition: string,
  parts: DispositionParts = {}
): Effect.Effect<BatchDisposition, PlayerError | DriftError> =>
  Effect.gen(function* () {
    const identifier = yield* opaque(batchId, 'batch disposition ID');
    if (!V2_DISPOSITIONS.has(disposition)) {
      return yield* Effect.fail(playerError('invalid batch disposition'));
    }
    const rawReceipt = parts.receipt ?? null;
    const rawError = parts.error ?? null;
    const receipt: CommandReceipt | null =
      rawReceipt === null
        ? null
        : yield* decodeReceipt(rawReceipt, session, { batchId: identifier });
    const error: StructuredError | null =
      rawError === null ? null : yield* decodeError(rawError);
    const contradicts =
      (disposition === 'receipt_terminal' &&
        (receipt === null ||
          !V2_TERMINAL_RECEIPTS.has(receipt.receipt_state) ||
          error !== null)) ||
      (disposition === 'receipt_poll' &&
        (receipt === null || receipt.receipt_state !== 'accepted' || error !== null)) ||
      ((disposition === 'retry_exact' || disposition === 'refresh') &&
        (receipt !== null || error === null)) ||
      (disposition === 'receipt_first' && receipt !== null);
    if (contradicts) {
      return yield* Effect.fail(playerError('invalid batch disposition payload'));
    }
    return {
      schema_version: 2,
      control_protocol: FULL_CONTROL_V2,
      game_id: session.gameId,
      agent_id: session.agentId,
      batch_id: identifier,
      disposition: disposition as Disposition,
      receipt,
      error,
    } as const;
  });

// ---------------------------------------------------------------------------
// _batch_error_disposition (client.py:8383-8420)
// ---------------------------------------------------------------------------

/**
 * Turn a refusal into a disposition, only if the refusal proved it may be one.
 *
 * Two independent checks have to agree: the server must *say* the batch was not
 * accepted and name its safe next step, and that step must be the one its own
 * error code implies.  A server that says "not accepted, retry the exact bytes"
 * under a code that means "we may have applied it" is contradicting itself, and
 * the only safe reading of a contradiction is receipt-first — which is what the
 * caller falls back to when this fails.
 */
export const batchErrorDisposition = (
  response: JsonResponse,
  session: Session,
  batchId: string
): Effect.Effect<BatchDisposition, PlayerError | DriftError> =>
  Effect.gen(function* () {
    const error = yield* decodeError(response.value);
    const body = error.error;
    const details = body.details;
    const safeNext = details['safe_next'];
    if (
      details['batch_id'] !== batchId ||
      details['acceptance'] !== 'not_accepted' ||
      typeof safeNext !== 'string' ||
      !SAFE_NEXT.has(safeNext)
    ) {
      return yield* Effect.fail(playerError('batch error omitted its safe recovery contract'));
    }
    const code = body.code;
    const expected = RECEIPT_FIRST_CODES.has(code)
      ? 'receipt_first'
      : (code === 'rate_limited' && response.status === 429 && body.retryable) ||
          (code === 'sidecar_unavailable' && response.status === 503 && body.retryable)
        ? 'retry_exact'
        : 'refresh';
    if (safeNext !== expected) {
      return yield* Effect.fail(
        playerError('batch error recovery contract contradicts its code')
      );
    }
    return yield* batchDisposition(session, batchId, safeNext, { error });
  });

// ---------------------------------------------------------------------------
// _submit_persisted_batch (client.py:8474-8544)
// ---------------------------------------------------------------------------

/** What `_submit_persisted_batch` returned as a 3-tuple. */
export interface BatchSubmission {
  readonly disposition: BatchDisposition;
  /** A sentence for stderr, or `null`.  Never part of stdout. */
  readonly warning: string | null;
  readonly exitCode: 0 | 2;
}

/**
 * The mirror write `_mirror_receipt` performs, as a seam.
 *
 * `_mirror` swallows every projection failure with a stderr warning — "a
 * projection never fails a command" — so this hook cannot fail either.
 */
export type ReceiptMirror = (
  receipt: CommandReceipt,
  command: string
) => Effect.Effect<void, never, PrivateFs>;

/** For a test that asserts on the wire and has no mirror to write into. */
export const inertReceiptMirror: ReceiptMirror = () => Effect.void;

export interface SubmitOptions {
  /**
   * Overrides `_mirror_receipt`.  **Absent means the real one**, because
   * `_submit_persisted_batch` (client.py:8528) calls it on every send — the
   * `applied batch … at rev N` line and the "state files still show rev N"
   * lag sentence it appends to `state/delta.md` are what `just show delta`
   * prints, so a send that skips it prints strictly fewer lines than CPython.
   */
  readonly mirrorReceipt?: ReceiptMirror;
}

const TRANSPORT_UNKNOWN = (batchId: string): string =>
  `transport outcome is unknown for batch ${batchId}. Check its receipt first ` +
  `with \`just receipt --batch_id ${batchId}\`; never blindly replay it.`;

const UNPROVED =
  'the server response did not prove that this persisted batch was unaccepted; ' +
  'resolve its receipt first';

const INVALID_SUCCESS =
  'the server returned an invalid success response; resolve the persisted batch ' +
  'by receipt before any retry';

const UNCACHED =
  'the authoritative receipt was validated but could not be cached locally; ' +
  'retain this output';

/**
 * Send the exact bytes already written to `.v2-state`, and read the outcome.
 *
 * The body is the persisted string, not a re-serialization of a re-decoded
 * object: an idempotent resend is only idempotent if the bytes are identical,
 * and re-encoding is where they would silently stop being.
 */
export const submitBatch = (
  sessionPath: string,
  session: Session,
  batchId: string,
  options: SubmitOptions = {}
): Effect.Effect<
  BatchSubmission,
  PlayerError | DriftError | LockTimeoutError,
  SessionStore | PrivateFs | V2Client
> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const client = yield* V2Client;
    const mirror =
      options.mirrorReceipt ??
      ((receipt: CommandReceipt, command: string) =>
        mirrorReceipt(sessionPath, receipt, command));
    const state = yield* store.readState(sessionPath, session);
    const encoded = state.batches[batchId];
    if (typeof encoded !== 'string') {
      return yield* Effect.fail(playerError(`no persisted command batch '${batchId}'`));
    }
    const credentials = credentialsOf(session);
    const sent = yield* Effect.either(
      Effect.flatMap(client.url(credentials, '/batches'), (url) =>
        client.response('POST', url, credentials, { encodedBody: encoded, timeout: 30 })
      )
    );
    if (Either.isLeft(sent)) {
      // The request may or may not have been applied.  Nothing here is allowed
      // to guess which, and nothing downstream is allowed to resend.
      return {
        disposition: yield* batchDisposition(session, batchId, 'receipt_first'),
        warning: TRANSPORT_UNKNOWN(batchId),
        exitCode: 2,
      };
    }
    const response = sent.right;
    const decoded = yield* Effect.either(
      decodeReceipt(response.value, session, { batchId })
    );
    if (Either.isLeft(decoded)) {
      if (!(response.status >= 200 && response.status < 300)) {
        const proved = yield* Effect.either(
          batchErrorDisposition(response, session, batchId)
        );
        if (Either.isLeft(proved)) {
          return {
            disposition: yield* batchDisposition(session, batchId, 'receipt_first'),
            warning: UNPROVED,
            exitCode: 2,
          };
        }
        return {
          disposition: proved.right,
          warning:
            'the server proved this batch was not accepted; follow the ' +
            `${proved.right.disposition} disposition`,
          exitCode: 2,
        };
      }
      return {
        disposition: yield* batchDisposition(session, batchId, 'receipt_first'),
        warning: INVALID_SUCCESS,
        exitCode: 2,
      };
    }
    const receipt = decoded.right;
    const cached = yield* Effect.either(
      Effect.zipRight(rememberReceipt(sessionPath, session, receipt), mirror(receipt, 'batch'))
    );
    const disposition = receipt.receipt_state === 'accepted' ? 'receipt_poll' : 'receipt_terminal';
    return {
      disposition: yield* batchDisposition(session, batchId, disposition, { receipt }),
      warning: Either.isLeft(cached) ? UNCACHED : null,
      exitCode: 0,
    };
  });

/** `_order_receipt_ok`'s reading of a disposition, for the local fallbacks. */
export const dispositionReceiptState = (disposition: BatchDisposition): string | null =>
  disposition.receipt === null ? null : disposition.receipt.receipt_state;
