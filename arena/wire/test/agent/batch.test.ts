/**
 * Command batches and CLI batch dispositions, checked against the OpenAPI
 * `CommandBatch` / `CliBatchDisposition` schemas and the shape check
 * `_submit_persisted_batch` (`play/client.py:8474-8500`) performs on a body it
 * wrote before the first POST.
 *
 * Two rules carry the weight here.  A batch is **exactly one command**,
 * because a receipt reports one outcome and "applied" would otherwise be
 * unanswerable.  And a disposition's receipt must answer the disposition's own
 * batch, because a receipt about someone else's batch is worse than no receipt
 * — it answers a question nobody asked.
 */

import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  BatchDisposition,
  batchDispositionFor,
  COMMANDS_PER_BATCH,
  Command,
  CommandBatch,
  commandBatchFor,
  decodeBatchDisposition,
  decodeBatchDispositionFor,
  decodeCommand,
  decodeCommandBatch,
  decodeCommandBatchFor,
  Disposition,
  V2_DISPOSITIONS,
} from 'src/agent/batch';
import { encodeTolerant, isTolerant } from 'src/tolerant';
import {
  AGENT_ID,
  BATCH_ID,
  batchWire,
  dispositionWire,
  errorEnvelope,
  GAME_ID,
  NEXT_REVISION,
  receiptWire,
  SESSION,
} from 'test/agent/wire-fixtures';

const accepts = (either: Either.Either<unknown, unknown>): boolean => Either.isRight(either);

const failure = (either: Either.Either<unknown, { readonly message: string }>): string =>
  Either.isLeft(either) ? either.left.message : '<accepted>';

const OTHER_BATCH_ID = `batch_${'S'.repeat(24)}`;

// ---------------------------------------------------------------------------
// Command
// ---------------------------------------------------------------------------

describe('a command', () => {
  test('is an opaque action id plus an arguments object', () => {
    const decoded = decodeCommand({ action_id: 'action_opaque', arguments: { ready: true } });
    expect(Either.isRight(decoded)).toBe(true);
    if (Either.isRight(decoded)) expect(String(decoded.right.action_id)).toBe('action_opaque');
  });

  test('takes empty arguments — the opaque id is the whole selection', () => {
    expect(accepts(decodeCommand({ action_id: 'action_opaque', arguments: {} }))).toBe(true);
  });

  test('refuses an action id outside OPAQUE_ID_RE', () => {
    expect(accepts(decodeCommand({ action_id: 'action id', arguments: {} }))).toBe(false);
  });

  test('refuses arguments that are not an object', () => {
    expect(accepts(decodeCommand({ action_id: 'action_opaque', arguments: [] }))).toBe(false);
    expect(accepts(decodeCommand({ action_id: 'action_opaque', arguments: null }))).toBe(false);
  });

  test('keeps an unrecognized argument key rather than dropping it', () => {
    const wire = { action_id: 'action_opaque', arguments: { ready: true, note: 'hi' } };
    const decoded = decodeCommand(wire);
    expect(Either.isRight(decoded)).toBe(true);
    if (!Either.isRight(decoded)) return;
    expect(decoded.right.arguments['note']).toBe('hi');
    const encoded = encodeTolerant(Command)(decoded.right);
    expect(Either.isRight(encoded)).toBe(true);
    if (Either.isRight(encoded)) expect(JSON.stringify(encoded.right)).toBe(JSON.stringify(wire));
  });
});

// ---------------------------------------------------------------------------
// CommandBatch
// ---------------------------------------------------------------------------

describe('a command batch', () => {
  test('decodes the body the CLI persists before the first POST', () => {
    const decoded = decodeCommandBatch(batchWire());
    expect(Either.isRight(decoded)).toBe(true);
    if (!Either.isRight(decoded)) return;
    expect(String(decoded.right.batch_id)).toBe(BATCH_ID);
    expect(decoded.right.state_revision.revision).toBe(8);
    expect(decoded.right.commands).toHaveLength(1);
  });

  test('carries exactly one command — zero and two are both refused', () => {
    expect(COMMANDS_PER_BATCH).toBe(1);
    expect(accepts(decodeCommandBatch(batchWire({ commands: [] })))).toBe(false);
    const two = [
      { action_id: 'action_a', arguments: {} },
      { action_id: 'action_b', arguments: {} },
    ];
    expect(accepts(decodeCommandBatch(batchWire({ commands: two })))).toBe(false);
  });

  test('pins the protocol header', () => {
    expect(accepts(decodeCommandBatch(batchWire({ schema_version: 1 })))).toBe(false);
    expect(accepts(decodeCommandBatch(batchWire({ control_protocol: 'strategic-v1' })))).toBe(
      false,
    );
  });

  test('needs a well-formed revision — a batch with no revision is not replayable', () => {
    expect(
      accepts(decodeCommandBatch(batchWire({ state_revision: { turn: 3, revision: 8 } }))),
    ).toBe(false);
    expect(accepts(decodeCommandBatch(batchWire({ state_revision: null })))).toBe(false);
  });

  test('a missing key is a refusal; an added one is preserved through a round trip', () => {
    const { batch_id: _dropped, ...withoutBatchId } = batchWire();
    expect(accepts(decodeCommandBatch(withoutBatchId))).toBe(false);

    const grown = batchWire({ client_build: 'arena-0.1.0' });
    const decoded = decodeCommandBatch(grown);
    expect(Either.isRight(decoded)).toBe(true);
    if (!Either.isRight(decoded)) return;
    const encoded = encodeTolerant(CommandBatch)(decoded.right);
    expect(Either.isRight(encoded)).toBe(true);
    if (Either.isRight(encoded)) expect(JSON.stringify(encoded.right)).toBe(JSON.stringify(grown));
  });
});

describe('a batch read back off disk', () => {
  const decode = decodeCommandBatchFor(SESSION);

  test('is accepted when it is this seat\'s', () => {
    expect(accepts(decode(batchWire()))).toBe(true);
  });

  test('is refused when another game wrote it', () => {
    expect(failure(decode(batchWire({ game_id: `${GAME_ID}x` })))).toContain('another game');
  });

  test('is refused when another agent wrote it', () => {
    expect(failure(decode(batchWire({ agent_id: `${AGENT_ID}x` })))).toContain('another agent');
  });

  test('the identity check is the only difference from the unbound schema', () => {
    const foreign = batchWire({ game_id: `${GAME_ID}x` });
    expect(accepts(decodeCommandBatch(foreign))).toBe(true);
    expect(isTolerant(commandBatchFor(SESSION))(foreign)).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// CliBatchDisposition
// ---------------------------------------------------------------------------

describe('the disposition vocabulary', () => {
  test('is the five values play/client.py:245 lists', () => {
    expect([...V2_DISPOSITIONS]).toEqual([
      'receipt_terminal',
      'receipt_poll',
      'receipt_first',
      'retry_exact',
      'refresh',
    ]);
  });

  test('is closed — an unrecognized instruction has no safe reading', () => {
    expect(isTolerant(Disposition)('receipt_first')).toBe(true);
    expect(isTolerant(Disposition)('give_up')).toBe(false);
    expect(accepts(decodeBatchDisposition(dispositionWire({ disposition: 'give_up' })))).toBe(
      false,
    );
  });

  test.each([...V2_DISPOSITIONS])('decodes a %s disposition', (disposition) => {
    expect(accepts(decodeBatchDisposition(dispositionWire({ disposition })))).toBe(true);
  });
});

describe('a batch disposition', () => {
  test('may carry neither a receipt nor an error — receipt_first knows nothing yet', () => {
    const wire = dispositionWire({ disposition: 'receipt_first', receipt: null, error: null });
    const decoded = decodeBatchDisposition(wire);
    expect(Either.isRight(decoded)).toBe(true);
    if (Either.isRight(decoded)) {
      expect(decoded.right.receipt).toBeNull();
      expect(decoded.right.error).toBeNull();
    }
  });

  test('may carry a structured error instead of a receipt', () => {
    const wire = dispositionWire({
      disposition: 'refresh',
      receipt: null,
      error: errorEnvelope('stale_revision'),
    });
    expect(accepts(decodeBatchDisposition(wire))).toBe(true);
  });

  test('may carry both — the Python imposes no exclusivity and neither does this', () => {
    const wire = dispositionWire({
      disposition: 'receipt_terminal',
      receipt: receiptWire(BATCH_ID, 'rejected'),
      error: errorEnvelope('illegal_action'),
    });
    expect(accepts(decodeBatchDisposition(wire))).toBe(true);
  });

  test('refuses a receipt that answers a different batch', () => {
    const wire = dispositionWire({ receipt: receiptWire(OTHER_BATCH_ID) });
    expect(failure(decodeBatchDisposition(wire))).toContain('answers batch');
  });

  test('propagates a refusal from the receipt it carries', () => {
    const wire = dispositionWire({
      receipt: receiptWire(BATCH_ID, 'ambiguous', {
        error: errorEnvelope('action_outcome_ambiguous', NEXT_REVISION),
      }),
    });
    expect(accepts(decodeBatchDisposition(wire))).toBe(false);
  });

  test('an added field survives a round trip, receipt and all', () => {
    const grown = dispositionWire({ elapsed_ms: 41 });
    const decoded = decodeBatchDisposition(grown);
    expect(Either.isRight(decoded)).toBe(true);
    if (!Either.isRight(decoded)) return;
    const encoded = encodeTolerant(BatchDisposition)(decoded.right);
    expect(Either.isRight(encoded)).toBe(true);
    if (Either.isRight(encoded)) expect(JSON.stringify(encoded.right)).toBe(JSON.stringify(grown));
  });
});

describe('a disposition bound to a seat', () => {
  const decode = decodeBatchDispositionFor(SESSION);

  test('accepts its own seat', () => {
    expect(accepts(decode(dispositionWire()))).toBe(true);
  });

  test('refuses an envelope addressed to another game', () => {
    expect(failure(decode(dispositionWire({ game_id: `${GAME_ID}x` })))).toContain('another game');
  });

  test('refuses a foreign receipt inside an envelope of its own', () => {
    const wire = dispositionWire({
      receipt: receiptWire(BATCH_ID, 'applied', { game_id: `${GAME_ID}x` }),
    });
    expect(accepts(decodeBatchDisposition(wire))).toBe(true);
    expect(failure(decode(wire))).toContain('another game');
  });

  test('the bound schema decodes to the same value as the unbound one', () => {
    const wire = dispositionWire();
    const bound = decode(wire);
    const bare = decodeBatchDisposition(wire);
    expect(Either.isRight(bound) && Either.isRight(bare)).toBe(true);
    if (Either.isRight(bound) && Either.isRight(bare)) {
      expect(bound.right).toEqual(bare.right);
    }
    expect(isTolerant(batchDispositionFor(SESSION))(wire)).toBe(true);
  });
});
