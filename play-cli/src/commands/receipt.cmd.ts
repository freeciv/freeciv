/**
 * `play receipt` — what actually happened to a batch.
 *
 * Ports `command_receipt` (client.py:9785-9803) and its argparse surface
 * (11782-11787).
 *
 * One GET, one validation, one line.  The command is deliberately read-only:
 * it cannot send a batch under any input, which is precisely why every refusal
 * remedy in this client points at it.  `receipt_first` means "run the command
 * that cannot make things worse".
 */
import { Command, Options } from '@effect/cli';
import { Console, Effect } from 'effect';
import type {
  DriftError,
  LockTimeoutError,
  PlayerError,
  SessionMissingError,
  V2ResponseError,
} from 'src/errors';
import { dualText, resolveDualRequired } from 'src/options';
import { render } from 'src/render/primitives';
import { renderReceipt } from 'src/render/receipt';
import { opaque } from 'src/schema/primitives';
import { decodeReceipt, type CommandReceipt } from 'src/schema/receipt';
import { jsonRequested, printV2Json } from 'src/services/json-output';
import { PrivateFs } from 'src/services/private-fs';
import {
  AMBIGUOUS_IS_TERMINAL,
  getReceiptResponse,
  liveReceiptHooks,
  type ReceiptHooksFor,
} from 'src/services/receipts';
import { SessionStore } from 'src/services/session-store';
import { V2Client } from 'src/services/v2-client';

export interface ReceiptOptions {
  readonly session: string;
  readonly batchId: string;
  readonly json: boolean;
}

export type ReceiptError =
  | PlayerError
  | DriftError
  | V2ResponseError
  | SessionMissingError
  | LockTimeoutError;

/**
 * Print the ambiguity warning on stderr, not stdout.
 *
 * `--json` has to stay a byte-identical wire payload, and an agent piping it
 * through `jq` still has to be told the batch is unreplayable — so the sentence
 * goes to the stream nothing parses.
 */
export const warnIfAmbiguous = (receipt: CommandReceipt): Effect.Effect<void> =>
  receipt.receipt_state === 'ambiguous' ? Console.error(AMBIGUOUS_IS_TERMINAL) : Effect.void;

export const runReceipt = (
  options: ReceiptOptions,
  makeHooks: ReceiptHooksFor = liveReceiptHooks
): Effect.Effect<void, ReceiptError, SessionStore | V2Client | PrivateFs> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const client = yield* V2Client;
    const { path, session } = yield* store.resolveV2(options.session);
    const batchId = yield* opaque(options.batchId.trim(), 'batch ID');
    const response = yield* getReceiptResponse(session, batchId);
    if (response.status < 200 || response.status >= 300) {
      return yield* client.raiseValidated(response);
    }
    const receipt = yield* decodeReceipt(response.value, session, { batchId });
    const hooks = yield* makeHooks(path, session);
    // The intent is read BEFORE the receipt is remembered: a validated receipt
    // retires the cached descriptors its action came from, and the line still
    // has to name what was asked for.
    const state = yield* store.readState(path, session);
    const intent = hooks.batchIntent(state, batchId);
    yield* hooks.rememberReceipt(receipt);
    yield* hooks.mirrorReceipt(receipt, 'receipt');
    if (jsonRequested('receipt', options.json)) {
      yield* printV2Json(receipt);
    } else {
      yield* render(renderReceipt(receipt, intent));
    }
    yield* warnIfAmbiguous(receipt);
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
export const receiptCommandWith = (makeHooks: ReceiptHooksFor) =>
  Command.make(
    'receipt',
    { session: sessionOption, batchId: dualText('batch-id'), json: jsonOption },
    (options) =>
      Effect.flatMap(resolveDualRequired('batch-id', options.batchId), (batchId) =>
        runReceipt({ session: options.session, batchId, json: options.json }, makeHooks)
      )
  );

export const receiptCommand = receiptCommandWith(liveReceiptHooks);
