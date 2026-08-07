/**
 * The receipt-state truth table.
 *
 * Ports `_order_receipt_ok` (client.py:9483-9489) and names the two invariants
 * the rest of the client is built on.  There are four receipt states and
 * exactly one of them is pollable:
 *
 * | state       | terminal | pollable | may a batch be re-sent? |
 * | ----------- | -------- | -------- | ----------------------- |
 * | `accepted`  | no       | **yes**  | never — poll it instead |
 * | `applied`   | **yes**  | no       | never                   |
 * | `rejected`  | **yes**  | no       | never                   |
 * | `ambiguous` | **yes**  | no       | **never, permanently**  |
 *
 * `ambiguous` is the one that loses games: the server took the batch, cannot
 * say whether it applied, and a replay could apply it twice.  It is terminal,
 * `retry` refuses to resubmit it, and no code path in this client may put it
 * back on the wire.
 */
import { V2_TERMINAL_RECEIPTS } from 'src/constants';
import type { BatchDisposition } from 'src/schema/batch';
import type { CommandReceipt, ReceiptState } from 'src/schema/receipt';

/** A receipt state that will never change again. */
export const isTerminalReceipt = (state: ReceiptState): boolean =>
  V2_TERMINAL_RECEIPTS.has(state);

/** The only state worth asking the server about a second time. */
export const isPollableReceipt = (state: ReceiptState): boolean => state === 'accepted';

/**
 * Whether a locally cached receipt in this state is allowed to reach the wire
 * again.
 *
 * The answer is never — for every one of the four states.  A terminal receipt
 * has an outcome, and an `accepted` one is already with the server.  Only
 * `retry`'s persisted-batch path, which asked the server for the receipt and
 * was told the batch does not exist *and* has never seen it accepted, may
 * submit again.  Kept as a named predicate so a future caller has to argue with
 * a function rather than rediscover the rule.
 */
export const mayResubmitCachedReceipt = (_state: ReceiptState): false => false;

/**
 * `_order_receipt_ok` — did this order actually go through?
 *
 * True only for a disposition carrying a receipt in `accepted` or `applied`.
 * A missing receipt is false, and so is `rejected`/`ambiguous`: `do`'s summary
 * must never claim an order landed when the receipt above it says otherwise.
 */
export const orderReceiptOk = (disposition: BatchDisposition): boolean => {
  const receipt: CommandReceipt | null = disposition.receipt;
  return (
    receipt !== null &&
    (receipt.receipt_state === 'accepted' || receipt.receipt_state === 'applied')
  );
};
