/**
 * The structured `full-control-v2` error envelope.
 *
 * Clean-room Effect Schema re-expression of `_validate_error`
 * (`play/client.py`:1330-1358), read a second time through
 * `play-cli/src/schema/error.ts`, and cross-checked against `StructuredError`
 * in `play/docs/full-control-v2.openapi.json`.  Nothing here imports from
 * `play-cli`.
 *
 * A structured error is not a message, it is a *remedy*.  `code` names the
 * precondition that failed, `retryable` says whether re-sending the identical
 * request can help, `details` carries the machine-usable context
 * (`safe_next`, `batch_id`, `retry_after_seconds`, `restart`), and
 * `state_revision` says which world the complaint is about.  Receipt safety
 * (`./receipt.ts`) is built out of `code` and `retryable`, which is why both
 * are strict here and why `code` is one of the few closed literal unions in
 * `@arena/wire`.
 *
 * @module
 */

import { Schema } from 'effect';
import { codePointLength } from './primitives.ts';
import { ControlProtocolV2, SchemaVersionV2, V2JsonObject } from './protocol.ts';
import { Revision } from './revision.ts';
import { decodeTolerant, type TolerantDecoder } from '../tolerant.ts';

// ---------------------------------------------------------------------------
// The code vocabulary — play/client.py:50-55
// ---------------------------------------------------------------------------

/**
 * `V2_ERROR_CODES` — `play/client.py:50-55`, and the `code` enum of
 * `StructuredError` in the OpenAPI document.  The two lists are identical, in
 * this order.
 *
 * **Closed**, unlike {@link ActionKind}.  The Python refuses a code it does
 * not know (`raw["code"] not in V2_ERROR_CODES`), and the port keeps that: a
 * code this build has never heard of is a protocol change, and inventing a
 * remedy for it is the dangerous option, not the tolerant one.  This is the
 * deliberate exception to the "never spell a server string as a literal" rule
 * in `../gateway/archive.ts` — that rule is about *labels*, and these are
 * behaviour selectors.
 */
export const V2_ERROR_CODES = [
  'action_expired',
  'action_outcome_ambiguous',
  'conflict',
  'cursor_expired',
  'illegal_action',
  'internal_error',
  'invalid_batch',
  'invalid_request',
  'not_implemented',
  'rate_limited',
  'scope_too_large',
  'sidecar_unavailable',
  'stale_revision',
  'unsupported_protocol',
] as const;

/** The code vocabulary, as a schema. */
export const V2ErrorCode = Schema.Literal(...V2_ERROR_CODES).annotations({
  identifier: 'V2ErrorCode',
  description: 'One of the fourteen full-control-v2 error codes (client.py:50)',
});
/** One of the fourteen v2 error codes. */
export type V2ErrorCode = typeof V2ErrorCode.Type;

/**
 * Singled out because every receipt invariant turns on it: it is the one code
 * that means *the command may or may not have been applied*, and it may never
 * be retried blindly.  See `./receipt.ts`.
 */
export const ACTION_OUTCOME_AMBIGUOUS = 'action_outcome_ambiguous' as const;

// ---------------------------------------------------------------------------
// The envelope — play/client.py:1330-1358
// ---------------------------------------------------------------------------

/** The `message` ceiling `_validate_error` imposes (`play/client.py:1350`). */
export const ERROR_MESSAGE_MAX = 500;

/**
 * `message`: non-blank once stripped, and at most 500 characters.
 *
 * Length is counted in **code points**, the way Python's `len()` counts it —
 * `String#length` would report 480 for a 240-emoji message the Python accepts.
 * The value itself is kept verbatim; see {@link errorMessageText} for why the
 * `.strip()` the Python applies does not happen inside the schema.
 */
export const V2ErrorMessage: Schema.Schema<string> = Schema.String.pipe(
  Schema.filter((value) => {
    if (value.trim().length === 0) return 'error message is blank';
    return codePointLength(value) > ERROR_MESSAGE_MAX
      ? `error message is longer than ${String(ERROR_MESSAGE_MAX)} characters`
      : undefined;
  }),
).annotations({
  identifier: 'V2ErrorMessage',
  description: 'A non-blank error message of at most 500 characters',
});

/**
 * The error body: the four keys `_exact` requires at `play/client.py:1340-1343`.
 *
 * `details` stays open — the OpenAPI's `ErrorDetails` is `additionalProperties`
 * by design, "so a later boundary can add a key" — and is bounded, never
 * shaped.
 */
export const StructuredErrorBody = Schema.Struct({
  /** Which precondition failed.  Branch on this, never on `message`. */
  code: V2ErrorCode,
  /** Human-readable, non-blank, at most 500 characters.  Never parsed. */
  message: V2ErrorMessage,
  /** Whether re-sending the byte-identical request can succeed. */
  retryable: Schema.Boolean,
  /** Machine-usable remedy context; open by contract. */
  details: V2JsonObject,
}).annotations({
  identifier: 'StructuredErrorBody',
  description: 'The error object inside a full-control-v2 error envelope',
});
/** A decoded error body. */
export type StructuredErrorBody = typeof StructuredErrorBody.Type;

/**
 * The error envelope.
 *
 * **Drift note — `state_revision` is required-and-nullable, not optional.**
 * `_validate_error` runs `_exact(value, {"schema_version",
 * "control_protocol", "error", "state_revision"})` (`play/client.py:1333-1337`), so
 * the Python refuses an envelope that *omits* the key even though the OpenAPI
 * document lists only the first three under `required`.  The port reproduces
 * the client rather than the document, so a parity run reaches the same
 * verdict on the same bytes; if the two are ever reconciled, this is the one
 * line to change.  (`Schema.optional(Schema.NullOr(Revision))` would be the
 * document's reading.)
 */
export const StructuredError = Schema.Struct({
  schema_version: SchemaVersionV2,
  control_protocol: ControlProtocolV2,
  error: StructuredErrorBody,
  /** The state the complaint is about, or `null` when it is state-free. */
  state_revision: Schema.NullOr(Revision),
}).annotations({
  identifier: 'StructuredError',
  description: 'The full-control-v2 structured error envelope (client.py:1330)',
});
/** A decoded error envelope. */
export type StructuredError = typeof StructuredError.Type;
/** The wire form of a {@link StructuredError}. */
export type StructuredErrorEncoded = typeof StructuredError.Encoded;

/** Decode an unknown value as a {@link StructuredError}. */
export const decodeError: TolerantDecoder<StructuredError> = decodeTolerant(
  StructuredError,
  'StructuredError',
);

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

/**
 * The message with its surrounding whitespace removed — what `_validate_error`
 * *stores* (`play/client.py:1356`, `raw["message"].strip()`).
 *
 * Deliberately not a transform inside the schema: trimming during decode would
 * make `encode(decode(x))` differ from `x`, which is the one guarantee
 * `@arena/wire` sells (`../tolerant.ts`).  Renderers call this; the wire keeps
 * its bytes.
 */
export const errorMessageText = (value: StructuredError): string => value.error.message.trim();

/**
 * The `HTTP {status}: {message} ({code})` sentence the CLI's `V2ResponseError`
 * carries, rebuilt from a decoded envelope.
 */
export const v2ErrorMessage = (status: number, payload: StructuredError): string =>
  `HTTP ${String(status)}: ${errorMessageText(payload)} (${payload.error.code})`;
