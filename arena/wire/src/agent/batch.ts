/**
 * Command batches, and the CLI-side disposition that records what happened to
 * one.
 *
 * Clean-room Effect Schema re-expression of the `Command`, `CommandBatch` and
 * `CliBatchDisposition` schemas in `play/docs/full-control-v2.openapi.json`
 * plus the shape check `_submit_persisted_batch` (`play/client.py:8474-8500`)
 * performs when it re-reads a batch body it wrote before the first POST.  Read
 * a second time through `play-cli/src/schema/batch.ts`; nothing here imports
 * from `play-cli`.
 *
 * ## A batch is exactly one command
 *
 * `minItems: 1, maxItems: 1` in the contract, `len(commands) != 1` in the
 * client.  This is not an accident of the current implementation: a batch is
 * the unit a receipt answers, and a receipt reports *one* outcome.  Two
 * commands under one `batch_id` would make `applied` unanswerable — applied
 * how far? — so the arity is part of the safety story and is enforced, not
 * relaxed.
 *
 * ## A batch is written before it is sent
 *
 * The CLI canonicalizes a batch, persists those exact bytes, and only then
 * POSTs them; if the transport outcome is unknown it re-reads the bytes and
 * checks their shape rather than rebuilding them.  That is what this schema
 * decodes.  Note the re-POST uses the **persisted bytes**, never a
 * re-serialization of the decoded value — see `V2JsonObject` in
 * `./protocol.ts` for why a decoded `arguments` tree is not a canonicalizable
 * one.
 *
 * ## Dispositions are ours, not the server's
 *
 * A `CliBatchDisposition` is emitted by the CLI to describe how a batch ended
 * (`receipt_terminal`, `receipt_poll`, `receipt_first`, `retry_exact`,
 * `refresh`).  `receipt_first` is the one that matters: it means the transport
 * failed in a way that leaves the command's fate unknown, and the *only* safe
 * next step is to read the receipt — never to replay the batch.
 *
 * @module
 */

import { Schema } from 'effect';
import { StructuredError } from './error.ts';
import { OpaqueId } from './ids.ts';
import {
  ControlProtocolV2,
  SchemaVersionV2,
  sessionMismatch,
  V2JsonObject,
  type V2Session,
} from './protocol.ts';
import { CommandReceipt } from './receipt.ts';
import { Revision } from './revision.ts';
import { decodeTolerant, type TolerantDecoder } from '../tolerant.ts';

// ---------------------------------------------------------------------------
// Command
// ---------------------------------------------------------------------------

/**
 * One command: an opaque capability, plus the arguments its descriptor's
 * `arguments_schema` demanded.
 *
 * `action_id` is the *whole* selection.  The contract is explicit that the
 * opaque id already binds the target — "exact building sabotage takes `{}`;
 * the opaque action ID already binds the selected improvement" — so a caller
 * that tried to steer an action by editing `arguments` would be describing an
 * action it was never offered.
 *
 * `arguments` is therefore bounded but unshaped: its shape is per-kind, named
 * by the descriptor, and the client never interprets it.
 */
export const Command = Schema.Struct({
  action_id: OpaqueId,
  arguments: V2JsonObject,
}).annotations({
  identifier: 'Command',
  description: 'One opaque action id plus its descriptor-shaped arguments',
});
/** A decoded command. */
export type Command = typeof Command.Type;

/** Decode an unknown value as a {@link Command}. */
export const decodeCommand: TolerantDecoder<Command> = decodeTolerant(Command, 'Command');

// ---------------------------------------------------------------------------
// CommandBatch
// ---------------------------------------------------------------------------

/** `minItems: 1, maxItems: 1` on `CommandBatch.commands`; `len(...) != 1` at client.py:8490. */
export const COMMANDS_PER_BATCH = 1;

const batchFields = Schema.Struct({
  schema_version: SchemaVersionV2,
  control_protocol: ControlProtocolV2,
  /** Compared against the session, never patterned — see {@link commandBatchFor}. */
  game_id: Schema.String,
  agent_id: Schema.String,
  /** The idempotency key the receipt will answer under. */
  batch_id: OpaqueId,
  /**
   * The revision the batch was built against.
   *
   * A batch is only legal at the revision its descriptor was minted at; the
   * supervisor rejects a stale one with `stale_revision` rather than guessing.
   */
  state_revision: Revision,
  commands: Schema.Array(Command).pipe(Schema.itemsCount(COMMANDS_PER_BATCH)),
});

/**
 * A command batch: one command, stamped with the seat and the state it was
 * built for.
 *
 * Identity is not checked here (no session in scope).  {@link commandBatchFor}
 * is the faithful port of what the client does — it refuses a batch belonging
 * to another game or agent, because a persisted batch is read back from a
 * workspace that may hold several seats' files.
 */
export const CommandBatch = batchFields.annotations({
  identifier: 'CommandBatch',
  description: 'A full-control-v2 command batch (openapi CommandBatch)',
});
/** A decoded command batch. */
export type CommandBatch = typeof CommandBatch.Type;
/** The wire form of a {@link CommandBatch}. */
export type CommandBatchEncoded = typeof CommandBatch.Encoded;

/** Decode a batch without binding it to a session. */
export const decodeCommandBatch: TolerantDecoder<CommandBatch> = decodeTolerant(
  CommandBatch,
  'CommandBatch',
);

/**
 * The batch schema bound to one seat — `_submit_persisted_batch`'s two
 * identity refusals, "batch belongs to another game" and "batch belongs to
 * another agent".
 */
export const commandBatchFor = (
  session: V2Session,
): Schema.Schema<CommandBatch, CommandBatchEncoded> =>
  CommandBatch.pipe(Schema.filter((batch) => sessionMismatch(session, batch))).annotations({
    identifier: 'CommandBatch',
  });

/** Decode a persisted batch that must belong to `session`. */
export const decodeCommandBatchFor = (session: V2Session): TolerantDecoder<CommandBatch> =>
  decodeTolerant(commandBatchFor(session), 'CommandBatch');

// ---------------------------------------------------------------------------
// CliBatchDisposition
// ---------------------------------------------------------------------------

/**
 * `V2_DISPOSITIONS` — `play/client.py:137-140`, and the `disposition` enum of
 * `CliBatchDisposition`.
 *
 * Closed: each value is an instruction to the caller, and an unrecognized one
 * has no safe interpretation.
 *
 * - `receipt_terminal` — the receipt is final; read it and move on.
 * - `receipt_poll` — the receipt is `accepted`; ask again.
 * - `receipt_first` — the outcome is **unknown**; read the receipt before
 *   doing anything else, and never replay the batch blindly.
 * - `retry_exact` — safe to re-send the identical bytes.
 * - `refresh` — the state moved; re-read legal actions and rebuild.
 */
export const V2_DISPOSITIONS = [
  'receipt_terminal',
  'receipt_poll',
  'receipt_first',
  'retry_exact',
  'refresh',
] as const;

/** The disposition vocabulary, as a schema. */
export const Disposition = Schema.Literal(...V2_DISPOSITIONS).annotations({
  identifier: 'Disposition',
  description: 'What the CLI decided about a submitted batch (client.py:137)',
});
/** One of the five dispositions. */
export type Disposition = typeof Disposition.Type;

const dispositionFields = Schema.Struct({
  schema_version: SchemaVersionV2,
  control_protocol: ControlProtocolV2,
  game_id: Schema.String,
  agent_id: Schema.String,
  batch_id: OpaqueId,
  disposition: Disposition,
  /** The receipt, when one was read.  Must answer *this* batch. */
  receipt: Schema.NullOr(CommandReceipt),
  /** The structured error, when the failure was structured. */
  error: Schema.NullOr(StructuredError),
});

type DispositionShape = typeof dispositionFields.Type;

/**
 * The receipt a disposition carries must be the receipt for the batch the
 * disposition names — the `{ batchId }` the client passes into
 * `_validate_receipt` (`play/client.py`, `_batch_disposition` call sites).
 *
 * Intra-payload, so it holds even for a disposition read back with no session
 * in hand.
 */
const receiptBatchIssue = (envelope: DispositionShape): string | undefined =>
  envelope.receipt !== null && envelope.receipt.batch_id !== envelope.batch_id
    ? `the carried receipt answers batch ${envelope.receipt.batch_id}, not ${envelope.batch_id}`
    : undefined;

/**
 * The CLI's own record of how a batch ended.
 *
 * Unlike a receipt this is not a server response — it is the document `--json`
 * prints — so nothing here is a claim about the game state except by way of
 * the receipt it carries.
 */
export const BatchDisposition = dispositionFields
  .pipe(Schema.filter(receiptBatchIssue))
  .annotations({
    identifier: 'BatchDisposition',
    description: 'The CLI-side disposition of a command batch (openapi CliBatchDisposition)',
  });
/** A decoded batch disposition. */
export type BatchDisposition = typeof BatchDisposition.Type;
/** The wire form of a {@link BatchDisposition}. */
export type BatchDispositionEncoded = typeof BatchDisposition.Encoded;

/** Decode a disposition without binding it to a session. */
export const decodeBatchDisposition: TolerantDecoder<BatchDisposition> = decodeTolerant(
  BatchDisposition,
  'BatchDisposition',
);

/**
 * The disposition schema bound to one seat, envelope *and* carried receipt.
 *
 * **Stricter than the Python, deliberately, and in the safe direction.**
 * `_validate_receipt` is handed the session, so the carried receipt is checked
 * against it; the enclosing disposition's own `game_id`/`agent_id` are not
 * checked at all — the Python rebuilds them from the session on the way out
 * (`play-cli/src/schema/batch.ts:150-158` does the same), which silently
 * discards a mismatch instead of reporting it.  A decoder must not overwrite
 * what it was given, so the port checks both and refuses.  The unbound
 * {@link BatchDisposition} keeps the Python's exact reach for anyone who needs
 * literal parity.
 */
export const batchDispositionFor = (
  session: V2Session,
): Schema.Schema<BatchDisposition, BatchDispositionEncoded> =>
  BatchDisposition.pipe(
    Schema.filter(
      (envelope) =>
        sessionMismatch(session, envelope) ??
        (envelope.receipt === null ? undefined : sessionMismatch(session, envelope.receipt)),
    ),
  ).annotations({ identifier: 'BatchDisposition' });

/** Decode a disposition that must belong to `session`, receipt included. */
export const decodeBatchDispositionFor = (
  session: V2Session,
): TolerantDecoder<BatchDisposition> =>
  decodeTolerant(batchDispositionFor(session), 'BatchDisposition');
