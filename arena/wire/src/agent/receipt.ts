/**
 * Command receipts — the only truthful answer to "did my command happen?".
 *
 * Clean-room Effect Schema re-expression of `_validate_receipt`
 * (`play/client.py:1861-1918`), read a second time through
 * `play-cli/src/schema/receipt.ts`, and cross-checked against `CommandReceipt`
 * in `play/docs/full-control-v2.openapi.json`.  Nothing here imports from
 * `play-cli`.
 *
 * ## The four states
 *
 * | state       | terminal | error       | observation | means |
 * | ----------- | -------- | ----------- | ----------- | ----- |
 * | `accepted`  | no       | `null`      | `null`      | queued; poll again |
 * | `applied`   | yes      | `null`      | optional    | it happened |
 * | `rejected`  | yes      | required    | `null`      | it did not happen |
 * | `ambiguous` | yes      | required    | `null`      | **unknown** whether it happened |
 *
 * ## Why the invariants are here and not in a command
 *
 * The receipt-state rules are safety-critical, and safety rules belong at the
 * boundary that first sees the bytes.  Two of them carry the whole weight:
 *
 * - an **`ambiguous`** receipt must carry `action_outcome_ambiguous` and that
 *   error must **not** be retryable.  Ambiguity means the command may already
 *   have been applied; a retryable ambiguous receipt would invite a caller to
 *   double-move a unit or double-spend gold.  A payload claiming both is
 *   refused rather than normalized, because a boundary that emits it is
 *   already wrong and the only safe reading is "I do not understand this".
 * - a **`rejected`** receipt must **not** carry `action_outcome_ambiguous`.
 *   "It definitely did not happen" and "it may have happened" are opposite
 *   claims; a payload asserting both is not a receipt.
 *
 * The remaining rules keep a receipt internally consistent: a non-terminal
 * receipt carries no error, a terminal error names the receipt's own revision,
 * and an observation may only ride on an `applied` receipt — and only one
 * captured at that same revision.
 *
 * ## Tolerance, and where it stops
 *
 * `_validate_receipt` reaches `_exact` through `_validate_v2_header` and so
 * refuses a receipt that gained a field.  This port preserves unknown fields
 * instead (`../tolerant.ts`); a supervisor that adds a key must not strand a
 * seat mid-game.  Everything else the Python refuses, this refuses.
 *
 * @module
 */

import { Schema } from 'effect';
import { ACTION_OUTCOME_AMBIGUOUS, StructuredError } from './error.ts';
import { OpaqueId } from './ids.ts';
import { CityInvestigationObservation } from './observation.ts';
import {
  ControlProtocolV2,
  SchemaVersionV2,
  sessionMismatch,
  type V2Session,
} from './protocol.ts';
import { Revision, revisionsEqual } from './revision.ts';
import { decodeTolerant, type TolerantDecoder } from '../tolerant.ts';

// ---------------------------------------------------------------------------
// The state vocabulary — play/client.py:48-49
// ---------------------------------------------------------------------------

/**
 * `V2_RECEIPT_STATES` — `play/client.py:48`, and the `receipt_state` enum
 * of `CommandReceipt` in the OpenAPI document.
 *
 * **Closed.**  The Python refuses a state it does not know, and so does this:
 * every caller branches on `receipt_state` to decide whether to poll, act or
 * stop, and a state with no branch has no safe default.
 */
export const V2_RECEIPT_STATES = ['accepted', 'applied', 'rejected', 'ambiguous'] as const;

/**
 * `V2_TERMINAL_RECEIPTS` — `play/client.py:49`.  The states that will never
 * change again; `accepted` is the only one worth polling.
 */
export const V2_TERMINAL_RECEIPTS = ['applied', 'rejected', 'ambiguous'] as const;

/** The receipt-state vocabulary, as a schema. */
export const ReceiptState = Schema.Literal(...V2_RECEIPT_STATES).annotations({
  identifier: 'ReceiptState',
  description: 'One of accepted | applied | rejected | ambiguous (client.py:48)',
});
/** One of the four receipt states. */
export type ReceiptState = typeof ReceiptState.Type;

const TERMINAL_RECEIPT_STATES: ReadonlySet<string> = new Set<string>(V2_TERMINAL_RECEIPTS);

/** True when a receipt state will never change again. */
export const isTerminalReceiptState = (
  state: string,
): state is (typeof V2_TERMINAL_RECEIPTS)[number] => TERMINAL_RECEIPT_STATES.has(state);

/** The two states that must carry a structured error (`play/client.py:1881`). */
export const carriesError = (state: ReceiptState): boolean =>
  state === 'rejected' || state === 'ambiguous';

// ---------------------------------------------------------------------------
// The receipt — play/client.py:1861-1918
// ---------------------------------------------------------------------------

const receiptFields = Schema.Struct({
  schema_version: SchemaVersionV2,
  control_protocol: ControlProtocolV2,
  /**
   * The game this receipt belongs to.
   *
   * A plain string, like the OpenAPI's `game_id`: `_validate_v2_header`
   * *compares* it against the session and never pattern-matches it
   * (`play/client.py:1325`).  Bind it with {@link commandReceiptFor}.
   */
  game_id: Schema.String,
  /** The agent this receipt belongs to.  Compared, not patterned. */
  agent_id: Schema.String,
  /** The batch this receipt answers.  `_opaque` in the Python (`:1857`). */
  batch_id: OpaqueId,
  receipt_state: ReceiptState,
  /**
   * Whether re-submitting the identical batch is a no-op.
   *
   * Strictly a boolean: the Python refuses a truthy string here
   * (`play/client.py:1878`), because "idempotent" is the flag a retry decision
   * is made from and a coerced value would silently authorize a double move.
   */
  idempotent: Schema.Boolean,
  /** The state the command landed on, or was refused against. */
  state_revision: Revision,
  /** Required by `rejected` and `ambiguous`, forbidden otherwise. */
  error: Schema.NullOr(StructuredError),
  /** Only ever present on an `applied` receipt. */
  observation: Schema.NullOr(CityInvestigationObservation),
});

type ReceiptShape = typeof receiptFields.Type;

/**
 * The error rules, in the Python's own order (`play/client.py:1881-1899`).
 *
 * Read top to bottom, this is the whole safety argument: a non-terminal
 * receipt has nothing to explain; a terminal one must explain itself against
 * the revision it names; an ambiguous one must say *exactly* that and must not
 * invite a retry; and a rejected one must not borrow ambiguity's code.
 */
const errorIssue = (receipt: ReceiptShape): string | undefined => {
  if (!carriesError(receipt.receipt_state)) {
    return receipt.error === null
      ? undefined
      : `a ${receipt.receipt_state} receipt must not carry an error`;
  }
  if (receipt.error === null) {
    return `a ${receipt.receipt_state} receipt must carry a structured error`;
  }
  if (
    receipt.error.state_revision === null ||
    !revisionsEqual(receipt.error.state_revision, receipt.state_revision)
  ) {
    return 'the receipt error names a different state revision than the receipt';
  }
  if (receipt.receipt_state === 'ambiguous') {
    if (receipt.error.error.code !== ACTION_OUTCOME_AMBIGUOUS) {
      return `an ambiguous receipt must carry the ${ACTION_OUTCOME_AMBIGUOUS} code`;
    }
    return receipt.error.error.retryable
      ? 'an ambiguous receipt must not be retryable'
      : undefined;
  }
  return receipt.error.error.code === ACTION_OUTCOME_AMBIGUOUS
    ? `a rejected receipt must not carry the ${ACTION_OUTCOME_AMBIGUOUS} code`
    : undefined;
};

/**
 * The observation rules (`play/client.py:1900-1906`).
 *
 * An observation is evidence about a state, so it may only accompany a command
 * that reached one (`applied`), and only at the revision the receipt names.
 * The revision comparison is the intra-payload half of
 * `_validate_investigation_observation`'s provenance check, which is why
 * `./observation.ts` cannot make it alone.
 */
const observationIssue = (receipt: ReceiptShape): string | undefined => {
  if (receipt.observation === null) return undefined;
  if (receipt.receipt_state !== 'applied') {
    return `only an applied receipt may carry an observation, not a ${receipt.receipt_state} one`;
  }
  return revisionsEqual(receipt.observation.state_revision, receipt.state_revision)
    ? undefined
    : 'the observation was captured at a different state revision than the receipt';
};

const receiptIssues = (receipt: ReceiptShape): string | undefined =>
  errorIssue(receipt) ?? observationIssue(receipt);

/**
 * One command receipt, with every cross-field invariant enforced.
 *
 * Identity is *not* checked here — `game_id` and `agent_id` are decoded but
 * not compared, because a bare schema has no session.  Use
 * {@link commandReceiptFor} wherever a session exists, which is everywhere a
 * receipt is read for real.
 */
export const CommandReceipt = receiptFields
  .pipe(Schema.filter(receiptIssues))
  .annotations({
    identifier: 'CommandReceipt',
    description: 'A full-control-v2 command receipt (client.py:1861)',
  });
/** A decoded command receipt. */
export type CommandReceipt = typeof CommandReceipt.Type;
/** The wire form of a {@link CommandReceipt}. */
export type CommandReceiptEncoded = typeof CommandReceipt.Encoded;

/** Decode a receipt without binding it to a session. */
export const decodeReceipt: TolerantDecoder<CommandReceipt> = decodeTolerant(
  CommandReceipt,
  'CommandReceipt',
);

// ---------------------------------------------------------------------------
// Bound to a session — play/client.py:1861-1872
// ---------------------------------------------------------------------------

/** What else a receipt must agree with, beyond the session identity. */
export interface ReceiptOptions {
  /**
   * When set, the receipt must name this batch (`batch_id=` at
   * `play/client.py:1862`).
   *
   * A receipt for someone else's batch is worse than no receipt: it answers a
   * question that was never asked, and a caller that trusted it would treat
   * another command's outcome as its own.
   */
  readonly batchId?: string;
}

const batchIssue = (
  receipt: ReceiptShape,
  options: ReceiptOptions,
): string | undefined =>
  options.batchId !== undefined && receipt.batch_id !== options.batchId
    ? `receipt names batch ${receipt.batch_id}, not ${options.batchId}`
    : undefined;

/**
 * The receipt schema bound to one seat, and optionally to one batch.
 *
 * A factory rather than a decoder argument so the identity check runs *inside*
 * the parser: a payload for another game fails the same way, with the same
 * error type and the same issue list, as a payload with a malformed revision.
 * Callers therefore have exactly one failure shape to handle.
 */
export const commandReceiptFor = (
  session: V2Session,
  options: ReceiptOptions = {},
): Schema.Schema<CommandReceipt, CommandReceiptEncoded> =>
  CommandReceipt.pipe(
    Schema.filter((receipt) => sessionMismatch(session, receipt) ?? batchIssue(receipt, options)),
  ).annotations({ identifier: 'CommandReceipt' });

/** Decode a receipt that must belong to `session` (and, optionally, to a batch). */
export const decodeReceiptFor = (
  session: V2Session,
  options: ReceiptOptions = {},
): TolerantDecoder<CommandReceipt> =>
  decodeTolerant(commandReceiptFor(session, options), 'CommandReceipt');
