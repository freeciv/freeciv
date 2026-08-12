/**
 * `state/receipts.log` — the streamed receipt ledger.
 *
 * PLAN.md §"The two behavioral upgrades allowed in phase 1", upgrade 2.  This
 * has no counterpart in `play/client.py`: it is new, and it is invisible in
 * stdout bytes.
 *
 * The failure it exists for is the one that killed the gpt-5.6-sol run.  `do`
 * submits one wire batch per order and only renders at the end; a process
 * killed between the third order's receipt and `_render` had four applied
 * actions on the server and left no record of any of them.  The agent's next
 * call then re-issued the whole batch, because nothing on disk said the first
 * three had landed.
 *
 * So each receipt is appended here the moment it applies, before the next
 * order is persisted, and the append is one `write(2)` on an `O_APPEND` file
 * descriptor of a bounded line.  A `SIGKILL` at any instant therefore leaves a
 * file whose every line is a complete JSON object; the reader never has to
 * decide whether a trailing fragment is a record.
 *
 * Nothing in this module may fail a command.  A ledger that cannot be written
 * is strictly worse than useless if it turns an applied batch into a refusal,
 * so {@link recordReceipt} degrades to silence — no stdout, no stderr, no
 * error channel.  `_render_disposition`'s lines remain the authority the agent
 * reads; this is the authority a supervisor reads afterwards.
 */
import { attemptOr } from 'src/errors';
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import type { BatchDisposition, Disposition } from 'src/schema/batch';
import { isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import type { ReceiptState } from 'src/schema/receipt';
import type { Revision } from 'src/schema/revision';
import { compactJson } from 'src/services/json-output';
import { appendMirror, mirrorDir, readMirror, STATE_DIR } from 'src/services/mirror/store';
import type { PrivateFs } from 'src/services/private-fs';

/** The ledger's projection path, beside `state/monitor.log`. */
export const RECEIPT_LEDGER_FILE: ReadonlyArray<string> = [STATE_DIR, 'receipts.log'];

/** Bumped when a field is removed or re-meant; new fields do not bump it. */
export const LEDGER_SCHEMA_VERSION = 1;

/**
 * The atomicity budget for one line.
 *
 * POSIX guarantees an `O_APPEND` write is seek-and-write atomic, but a *short*
 * write is still permitted for a large buffer, and a short write is exactly
 * the torn line this file exists to prevent.  4096 is the smallest page every
 * platform this client runs on writes whole, and every field below is bounded
 * so a well-formed entry fits inside it with room to spare.
 */
export const MAX_LEDGER_LINE = 4096;

/** How much of one order's text a ledger line will carry. */
export const MAX_LEDGER_ORDER = 512;

// ---------------------------------------------------------------------------
// The entry
// ---------------------------------------------------------------------------

/** One order's outcome, as the ledger records it. */
export interface LedgerEntry {
  /** The agent's own words for this order — `"u1 found_city London"`. */
  readonly order: string;
  /** The server-issued batch this order was sent as; `""` when never sent. */
  readonly batchId: string;
  /** The opaque capability handle that was submitted. */
  readonly actionId: string;
  /** The arguments bound to it, exactly as the wire carried them. */
  readonly arguments: JsonObject;
  /** `null` when the disposition carried no receipt at all. */
  readonly receiptState: ReceiptState | null;
  /** How the client resolved the submission. */
  readonly disposition: Disposition | null;
  /** The revision the receipt proved, when there was one. */
  readonly revision: Revision | null;
  /** True when `_order_receipt_ok` held — `accepted` or `applied`. */
  readonly ok: boolean;
}

/** Read one order's outcome off the disposition U13/U14 produced. */
export const ledgerEntry = (
  order: string,
  actionId: string,
  args: JsonObject,
  disposition: BatchDisposition
): LedgerEntry => {
  const receipt = disposition.receipt;
  return {
    order,
    batchId: disposition.batch_id,
    actionId,
    arguments: args,
    receiptState: receipt === null ? null : receipt.receipt_state,
    disposition: disposition.disposition,
    revision: receipt === null ? null : receipt.state_revision,
    ok: receipt !== null && (receipt.receipt_state === 'accepted' || receipt.receipt_state === 'applied'),
  };
};

const revisionJson = (revision: Revision | null): JsonValue =>
  revision === null
    ? null
    : {
        turn: revision.turn,
        revision: revision.revision,
        state_token: revision.state_token,
      };

const record = (entry: LedgerEntry, at: Date, withArguments: boolean): JsonValue => ({
  schema_version: LEDGER_SCHEMA_VERSION,
  at: at.toISOString(),
  order: entry.order.slice(0, MAX_LEDGER_ORDER),
  batch_id: entry.batchId,
  action_id: entry.actionId,
  receipt_state: entry.receiptState,
  disposition: entry.disposition,
  state_revision: revisionJson(entry.revision),
  ok: entry.ok,
  ...(withArguments ? { arguments: entry.arguments } : { arguments_omitted: true }),
});

/**
 * One newline-terminated JSON object, guaranteed to fit the atomicity budget.
 *
 * The arguments are the only unbounded field — a worklist is a list of the
 * player's own choosing — so they are what is dropped when the line would not
 * fit.  Truncating the *text* instead would produce a line that parses as
 * nothing, which is the one outcome a crash-recovery reader cannot tolerate.
 */
export const ledgerLine = (entry: LedgerEntry, at: Date = new Date()): string => {
  const full = `${compactJson(record(entry, at, true))}\n`;
  if (Buffer.byteLength(full, 'utf8') <= MAX_LEDGER_LINE) return full;
  return `${compactJson(record(entry, at, false))}\n`;
};

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

/** Append one already-rendered line; fails loudly, for callers that test it. */
export const appendLedgerLine = (
  dir: string,
  line: string
): Effect.Effect<string, PlayerError, PrivateFs> => appendMirror(dir, RECEIPT_LEDGER_FILE, line);

/** Append one entry to a mirror directory; fails loudly. */
export const appendLedgerEntry = (
  dir: string,
  entry: LedgerEntry,
  at?: Date
): Effect.Effect<string, PlayerError, PrivateFs> =>
  Effect.suspend(() => appendLedgerLine(dir, ledgerLine(entry, at ?? new Date())));

/**
 * Swallow *everything* a ledger write can end in, defects included.
 *
 * `Effect.ignore` only discards the typed error channel.  The write itself is
 * `PrivateFsApi.appendText`, whose `fstat`/`fchmod`/`write` calls sit in a bare
 * `try/finally` rather than an `Effect.try`, so an `ENOSPC`, `EDQUOT`, `EIO` or
 * `EPERM` throw arrives as a *defect* — which would sail past `ignore`, out of
 * the order loop, and into `cli-main`'s `catchAllCause`, printing
 * `error: <Cause.pretty>` on stderr with an empty stdout and status 2.  That is
 * this file's own failure mode inverted: the applied orders' server-issued
 * `batch_id`s would be lost from a command CPython would have completed, and
 * the agent would re-issue a real duplicate action.
 *
 * Interruption is not caught: `catchAllDefect` leaves it alone, so a `SIGINT`
 * still cancels the command rather than being absorbed by a log write.
 */
const quiet = <A, E, R>(body: Effect.Effect<A, E, R>): Effect.Effect<void, never, R> =>
  Effect.catchAllDefect(Effect.ignore(body), () => Effect.void);

/**
 * Record one receipt against a session, and never let that fail a command.
 *
 * Silence, not `mirrorGuard`: the mirror's warning line is correct for a
 * projection the agent is about to read, and wrong for a forensic log it will
 * not.  A warning here would put bytes on stderr that CPython never wrote.
 */
export const recordReceipt = (
  sessionPath: string,
  entry: LedgerEntry,
  at?: Date
): Effect.Effect<void, never, PrivateFs> =>
  quiet(Effect.flatMap(mirrorDir(sessionPath), (dir) => appendLedgerEntry(dir, entry, at)));

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

/**
 * Every complete record in a session's ledger, oldest first.
 *
 * A line that does not parse is skipped rather than raising: the file is
 * append-only forensic evidence, and a reader that refuses the whole record
 * because a future writer added a shape it does not know is the drift failure
 * PLAN's "decode permissively" rule is about.
 */
const EMPTY_LEDGER: ReadonlyArray<JsonObject> = [];

/** The read half of {@link quiet}: `PrivateFsApi.readText` can defect too. */
const readable = (
  body: Effect.Effect<ReadonlyArray<JsonObject>, never, PrivateFs>
): Effect.Effect<ReadonlyArray<JsonObject>, never, PrivateFs> =>
  Effect.catchAllDefect(body, () => Effect.succeed(EMPTY_LEDGER));

export const readLedger = (
  dir: string
): Effect.Effect<ReadonlyArray<JsonObject>, never, PrivateFs> =>
  readable(
    Effect.map(readMirror(dir, RECEIPT_LEDGER_FILE), (text) => {
      if (text === null) return EMPTY_LEDGER;
      const rows: Array<JsonObject> = [];
      for (const line of text.split('\n')) {
        if (line.trim() === '') continue;
        const value = attemptOr((): unknown => JSON.parse(line), () => null);
        if (isJsonObject(value)) rows.push(value);
      }
      return rows;
    })
  );

/** The ledger of one session file. */
export const readSessionLedger = (
  sessionPath: string
): Effect.Effect<ReadonlyArray<JsonObject>, never, PrivateFs> =>
  readable(
    Effect.flatMap(
      Effect.orElseSucceed(mirrorDir(sessionPath), () => ''),
      (dir) => (dir === '' ? Effect.succeed(EMPTY_LEDGER) : readLedger(dir))
    )
  );
