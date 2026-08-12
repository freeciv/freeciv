/**
 * `play retry` — the command that re-derives instead of re-sending.
 *
 * Ports the retry half of `test_v2_batch_persists_before_send_and_retry_is_
 * receipt_first` (test_client.py:3701) and the `retry` arm of
 * `test_v2_json_escape_hatch_covers_turn_batch_retry_and_wait` (1040).
 * `test_v2_retry_accepted_then_missing_is_ambiguous_without_resend` (3784)
 * lives in `ambiguous.test.ts` with the rest of the terminal-state proofs.
 *
 * Every assertion here is really the same assertion: **the wire is touched as
 * little as the answer allows.** A cached terminal receipt makes no request at
 * all; a receipt that exists is read, never replayed; a batch body goes back on
 * the wire only when the server has said it never arrived.
 */
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import { FULL_CONTROL_V2 } from 'src/constants';
import { retryLocked, runRetry } from 'src/commands/retry.cmd';
import { decodeBatchDisposition } from 'src/schema/batch';
import { decodeReceipt } from 'src/schema/receipt';
import type { JsonObject } from 'src/schema/primitives';
import { rememberReceipt } from 'src/services/aliases';
import { PrivateFs } from 'src/services/private-fs';
import {
  RETRY_POLL_DEADLINE_S,
  RETRY_POLL_INTERVAL_S,
  type SubmitOutcome,
} from 'src/services/receipts';
import { SessionStore } from 'src/services/session-store';
import { FIXTURE_AGENT_ID, FIXTURE_GAME_ID, type FakeRoute } from 'test/_fixtures';
import {
  BATCH_ID,
  DESCRIPTOR,
  batchBody,
  buildFixture,
  capture,
  cleanupScratches,
  errorEnvelope,
  messageOf,
  receiptWire,
  recordingHooks,
  tagOf,
  testClock,
  type Fixture,
} from 'test/receipt-harness';

afterEach(cleanupScratches);

const RECEIPT_ROUTE = '/receipts/';

const queue = (routes: ReadonlyArray<FakeRoute>): ReadonlyArray<FakeRoute> => routes;

const ok = (body: JsonObject): FakeRoute => ({ status: 200, body });
const missing = (): FakeRoute => ({ status: 404, body: errorEnvelope('invalid_request', null) });

/**
 * A test double for U13's `submitBatch`, used to observe *what* was submitted.
 *
 * It does the two things about `_submit_persisted_batch` that `retry` depends
 * on — read the persisted bytes back out of `.v2-state` and remember the
 * resulting receipt — while recording the bytes so a test can assert them
 * directly.  The real seam is exercised end-to-end further down
 * ("the LIVE seam (U13 submitBatch) …"), against the fake supervisor.
 */
const submitting = (
  fixture: Fixture,
  outcome: { readonly state?: string; readonly warning?: string; readonly exitCode?: 0 | 2 } = {}
): { readonly hooks: ReturnType<typeof recordingHooks>; readonly posted: ReadonlyArray<string> } => {
  const posted: string[] = [];
  const provided = Layer.merge(
    Layer.succeed(SessionStore, fixture.store),
    Layer.succeed(PrivateFs, fixture.scratch.files)
  );
  const hooks = recordingHooks({
    submitPersistedBatch: (batchId: string): Effect.Effect<SubmitOutcome> =>
      Effect.gen(function* () {
        const state = yield* fixture.store.readState(fixture.sessionPath, fixture.session);
        const encoded = state.batches[batchId];
        posted.push(typeof encoded === 'string' ? encoded : '');
        const receipt = yield* decodeReceipt(
          receiptWire(batchId, outcome.state ?? 'applied'),
          fixture.session,
          { batchId }
        );
        yield* Effect.provide(
          rememberReceipt(fixture.sessionPath, fixture.session, receipt),
          provided
        );
        const disposition = yield* decodeBatchDisposition(
          {
            schema_version: 2,
            control_protocol: FULL_CONTROL_V2,
            game_id: FIXTURE_GAME_ID,
            agent_id: FIXTURE_AGENT_ID,
            batch_id: batchId,
            disposition: 'receipt_terminal',
            receipt: receiptWire(batchId, outcome.state ?? 'applied'),
            error: null,
          },
          fixture.session
        );
        const submission: SubmitOutcome = {
          disposition,
          warning: outcome.warning ?? null,
          exitCode: outcome.exitCode ?? 0,
        };
        return submission;
      }).pipe(Effect.orDie),
  });
  return { hooks, posted };
};

// ---------------------------------------------------------------------------
// Nothing persisted
// ---------------------------------------------------------------------------

describe('play retry: nothing to re-derive', () => {
  test('an unknown batch is refused by name, and nothing is sent', async () => {
    const fixture = buildFixture(new Map<string, FakeRoute>());
    fixture.seed({});
    const hooks = recordingHooks();
    const { result } = await capture(
      runRetry({ session: fixture.sessionPath, batchId: BATCH_ID, json: false }, hooks.make),
      fixture
    );
    expect(Either.isLeft(result)).toBe(true);
    if (Either.isLeft(result)) {
      expect(messageOf(result.left)).toBe(`no persisted command batch '${BATCH_ID}'`);
    }
    expect(fixture.requests).toEqual([]);
    expect(hooks.submits).toEqual([]);
  });

  test('a malformed batch ID is refused before the state is even consulted', async () => {
    const fixture = buildFixture(new Map<string, FakeRoute>());
    fixture.seed({});
    const { result } = await capture(
      runRetry({ session: fixture.sessionPath, batchId: '  ', json: false }),
      fixture
    );
    expect(Either.isLeft(result)).toBe(true);
    if (Either.isLeft(result)) expect(messageOf(result.left)).toBe('invalid batch ID');
    expect(fixture.requests).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// The cache answers
// ---------------------------------------------------------------------------

describe('play retry: a cached terminal receipt is the answer', () => {
  for (const state of ['applied', 'rejected'] as const) {
    test(`a cached ${state} receipt is printed with no request at all`, async () => {
      const fixture = buildFixture(new Map<string, FakeRoute>());
      fixture.seed({
        actions: { action_opaque: DESCRIPTOR },
        batches: { [BATCH_ID]: batchBody(BATCH_ID) },
        receipts: { [BATCH_ID]: receiptWire(BATCH_ID, state) },
      });
      const hooks = recordingHooks();
      const { out, err, result } = await capture(
        runRetry({ session: fixture.sessionPath, batchId: BATCH_ID, json: false }, hooks.make),
        fixture
      );
      expect(Either.isRight(result)).toBe(true);
      expect(out).toHaveLength(1);
      expect(out[0]).toContain(`→ ${state}`);
      expect(out[0]?.startsWith('phase.end End phase {ready=yes} → ')).toBe(true);
      expect(err).toEqual([]);
      // The whole point: no GET, no POST, no re-remembering.
      expect(fixture.requests).toEqual([]);
      expect(hooks.submits).toEqual([]);
      expect(hooks.remembered).toEqual([]);
    });
  }

  test('a cached accepted receipt is NOT terminal, so it is polled', async () => {
    const fixture = buildFixture(queue([ok(receiptWire(BATCH_ID, 'applied'))]));
    fixture.seed({
      batches: { [BATCH_ID]: batchBody(BATCH_ID) },
      receipts: { [BATCH_ID]: receiptWire(BATCH_ID, 'accepted') },
    });
    const { out, result } = await capture(
      runRetry({ session: fixture.sessionPath, batchId: BATCH_ID, json: false }),
      fixture
    );
    expect(Either.isRight(result)).toBe(true);
    expect(fixture.requests.map((request) => request.method)).toEqual(['GET']);
    expect(fixture.requests[0]?.url).toContain(`${RECEIPT_ROUTE}${BATCH_ID}`);
    expect(out[0]).toContain('→ applied');
  });
});

// ---------------------------------------------------------------------------
// Polling an accepted receipt
// ---------------------------------------------------------------------------

describe('play retry: polling', () => {
  test('accepted is polled until it resolves, at the documented interval', async () => {
    const fixture = buildFixture(
      queue([
        ok(receiptWire(BATCH_ID, 'accepted')),
        ok(receiptWire(BATCH_ID, 'accepted')),
        ok(receiptWire(BATCH_ID, 'applied')),
      ])
    );
    fixture.seed({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } });
    const clock = testClock();
    const { out, result } = await capture(
      retryLocked(
        fixture.sessionPath,
        fixture.session,
        { session: fixture.sessionPath, batchId: BATCH_ID, json: false },
        recordingHooks().make,
        clock
      ),
      fixture
    );
    expect(Either.isRight(result)).toBe(true);
    expect(fixture.requests).toHaveLength(3);
    expect(clock.sleeps).toEqual([RETRY_POLL_INTERVAL_S, RETRY_POLL_INTERVAL_S]);
    // Only the resolved receipt is printed: an agent must not see three lines
    // for one order.
    expect(out).toHaveLength(1);
    expect(out[0]).toContain('→ applied');
  });

  test('the poll gives up at the deadline and prints the accepted line once', async () => {
    const fixture = buildFixture(
      new Map([[RECEIPT_ROUTE, ok(receiptWire(BATCH_ID, 'accepted'))]])
    );
    fixture.seed({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } });
    // Each sleep burns a quarter of the budget, so the loop cannot outrun the
    // deadline and the test cannot hang.
    const clock = testClock({ advanceOnSleep: RETRY_POLL_DEADLINE_S / 4 });
    const { out, result } = await capture(
      retryLocked(
        fixture.sessionPath,
        fixture.session,
        { session: fixture.sessionPath, batchId: BATCH_ID, json: false },
        recordingHooks().make,
        clock
      ),
      fixture
    );
    expect(Either.isRight(result)).toBe(true);
    expect(clock.sleeps).toHaveLength(4);
    expect(fixture.requests).toHaveLength(5);
    expect(out).toEqual([
      `action_opaque {ready=yes} → accepted (not final; resolve with just receipt) ` +
        `rev8/t3  ${BATCH_ID}`,
    ]);
  });

  test('a receipt already past the deadline is printed without a single sleep', async () => {
    const fixture = buildFixture(queue([ok(receiptWire(BATCH_ID, 'accepted'))]));
    fixture.seed({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } });
    const clock = testClock();
    const hooks = recordingHooks();
    // The clock jumps past the deadline between the two `monotonic()` reads,
    // which is the boundary CPython's `time.monotonic() < deadline` guards.
    const jumped = {
      ...clock,
      monotonic: ((): (() => Effect.Effect<number>) => {
        const reads = { count: 0 };
        return () =>
          Effect.sync(() => {
            reads.count += 1;
            return reads.count === 1 ? 0 : RETRY_POLL_DEADLINE_S + 1;
          });
      })(),
    };
    const { out, result } = await capture(
      retryLocked(
        fixture.sessionPath,
        fixture.session,
        { session: fixture.sessionPath, batchId: BATCH_ID, json: false },
        hooks.make,
        jumped
      ),
      fixture
    );
    expect(Either.isRight(result)).toBe(true);
    expect(clock.sleeps).toEqual([]);
    expect(fixture.requests).toHaveLength(1);
    expect(out[0]).toContain('accepted (not final; resolve with just receipt)');
  });

  test('every polled receipt is remembered and mirrored under the "retry" tag', async () => {
    const fixture = buildFixture(
      queue([ok(receiptWire(BATCH_ID, 'accepted')), ok(receiptWire(BATCH_ID, 'applied'))])
    );
    fixture.seed({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } });
    const hooks = recordingHooks();
    await capture(
      retryLocked(
        fixture.sessionPath,
        fixture.session,
        { session: fixture.sessionPath, batchId: BATCH_ID, json: false },
        hooks.make,
        testClock()
      ),
      fixture
    );
    expect(hooks.remembered.map((receipt) => receipt.receipt_state)).toEqual([
      'accepted',
      'applied',
    ]);
    expect(hooks.mirrored).toEqual([
      [BATCH_ID, 'retry'],
      [BATCH_ID, 'retry'],
    ]);
  });
});

// ---------------------------------------------------------------------------
// The one path that may submit
// ---------------------------------------------------------------------------

describe('play retry: the persisted body goes back only when it never arrived', () => {
  test('404 invalid_request with no receipt ever seen re-sends the exact bytes', async () => {
    const fixture = buildFixture(new Map([[RECEIPT_ROUTE, missing()]]));
    const body = batchBody(BATCH_ID);
    fixture.seed({ actions: { action_opaque: DESCRIPTOR }, batches: { [BATCH_ID]: body } });
    const submit = submitting(fixture);
    const { out, err, result } = await capture(
      runRetry(
        { session: fixture.sessionPath, batchId: BATCH_ID, json: false },
        submit.hooks.make
      ),
      fixture
    );
    expect(Either.isRight(result)).toBe(true);
    expect(submit.posted).toEqual([body]);
    expect(out).toEqual([
      `phase.end End phase {ready=yes} → applied rev8/t3  ${BATCH_ID}`,
    ]);
    expect(err).toEqual([]);
  });

  test('and once it has applied, a second retry sends nothing at all', async () => {
    const fixture = buildFixture(new Map([[RECEIPT_ROUTE, missing()]]));
    fixture.seed({
      actions: { action_opaque: DESCRIPTOR },
      batches: { [BATCH_ID]: batchBody(BATCH_ID) },
    });
    const first = submitting(fixture);
    await capture(
      runRetry({ session: fixture.sessionPath, batchId: BATCH_ID, json: false }, first.hooks.make),
      fixture
    );
    const before = fixture.requests.length;
    const second = submitting(fixture);
    const { out, result } = await capture(
      runRetry({ session: fixture.sessionPath, batchId: BATCH_ID, json: false }, second.hooks.make),
      fixture
    );
    expect(Either.isRight(result)).toBe(true);
    expect(second.posted).toEqual([]);
    expect(fixture.requests).toHaveLength(before);
    expect(out[0]).toContain('→ applied');
  });

  test('a warning from the submit path goes to stderr, the disposition to stdout', async () => {
    const fixture = buildFixture(new Map([[RECEIPT_ROUTE, missing()]]));
    fixture.seed({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } });
    const submit = submitting(fixture, { warning: 'transport outcome is unknown', exitCode: 2 });
    const { out, err, result } = await capture(
      runRetry(
        { session: fixture.sessionPath, batchId: BATCH_ID, json: false },
        submit.hooks.make
      ),
      fixture
    );
    expect(out).toHaveLength(1);
    expect(err).toEqual(['transport outcome is unknown']);
    // Exit 2 leaves as a quiet status signal, not an `error: …` line.
    expect(Either.isLeft(result)).toBe(true);
    if (Either.isLeft(result)) {
      expect(tagOf(result.left)).toBe('ExitCodeSignal');
      expect((result.left as { readonly code: number }).code).toBe(2);
    }
  });

  test('the LIVE seam (U13 submitBatch) puts the persisted bytes on the wire verbatim', async () => {
    // No hook stand-in here: this is `retry` wired to the real submit path,
    // against a fake supervisor. It is the end-to-end proof that a re-send
    // reuses the idempotency key rather than re-serializing one.
    const fixture = buildFixture(
      new Map([
        [RECEIPT_ROUTE, missing()],
        ['/batches', ok(receiptWire(BATCH_ID, 'applied'))],
      ])
    );
    const body = batchBody(BATCH_ID);
    fixture.seed({ actions: { action_opaque: DESCRIPTOR }, batches: { [BATCH_ID]: body } });
    const { out, result } = await capture(
      runRetry({ session: fixture.sessionPath, batchId: BATCH_ID, json: false }),
      fixture
    );
    expect(Either.isRight(result)).toBe(true);
    const posts = fixture.requests.filter((request) => request.method === 'POST');
    expect(posts).toHaveLength(1);
    expect(posts[0]?.body).toBe(body);
    expect(posts[0]?.url).toContain('/batches');
    expect(out).toEqual([`phase.end End phase {ready=yes} → applied rev8/t3  ${BATCH_ID}`]);

    // Run it again and the send does not repeat: the receipt is terminal now.
    const { result: second } = await capture(
      runRetry({ session: fixture.sessionPath, batchId: BATCH_ID, json: false }),
      fixture
    );
    expect(Either.isRight(second)).toBe(true);
    expect(fixture.requests.filter((request) => request.method === 'POST')).toHaveLength(1);
  });
});

// ---------------------------------------------------------------------------
// Refusals that are not "the batch does not exist"
// ---------------------------------------------------------------------------

describe('play retry: a refusal it cannot interpret is raised, never guessed', () => {
  test('a 404 with another code is raised, and nothing is sent', async () => {
    const fixture = buildFixture(
      new Map([[RECEIPT_ROUTE, { status: 404, body: errorEnvelope('conflict', null) }]])
    );
    fixture.seed({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } });
    const submit = submitting(fixture);
    const { result } = await capture(
      runRetry(
        { session: fixture.sessionPath, batchId: BATCH_ID, json: false },
        submit.hooks.make
      ),
      fixture
    );
    expect(Either.isLeft(result)).toBe(true);
    if (Either.isLeft(result)) {
      expect(tagOf(result.left)).toBe('V2ResponseError');
      expect((result.left as { readonly status: number }).status).toBe(404);
    }
    expect(submit.posted).toEqual([]);
  });

  test('a 500 invalid_request is raised too — only 404 means "never arrived"', async () => {
    const fixture = buildFixture(
      new Map([[RECEIPT_ROUTE, { status: 500, body: errorEnvelope('invalid_request', null) }]])
    );
    fixture.seed({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } });
    const submit = submitting(fixture);
    const { result } = await capture(
      runRetry(
        { session: fixture.sessionPath, batchId: BATCH_ID, json: false },
        submit.hooks.make
      ),
      fixture
    );
    expect(Either.isLeft(result)).toBe(true);
    if (Either.isLeft(result)) expect(tagOf(result.left)).toBe('V2ResponseError');
    expect(submit.posted).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// --json
// ---------------------------------------------------------------------------

describe('play retry: --json', () => {
  test('a receipt prints as the validated receipt envelope', async () => {
    const fixture = buildFixture(queue([ok(receiptWire(BATCH_ID, 'applied'))]));
    fixture.seed({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } });
    const { out } = await capture(
      runRetry({ session: fixture.sessionPath, batchId: BATCH_ID, json: true }),
      fixture
    );
    expect(out).toHaveLength(1);
    const printed = JSON.parse(out[0] ?? '') as { readonly receipt_state: string };
    expect(printed.receipt_state).toBe('applied');
  });

  test('a submit prints the disposition envelope, not the receipt', async () => {
    const fixture = buildFixture(new Map([[RECEIPT_ROUTE, missing()]]));
    fixture.seed({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } });
    const submit = submitting(fixture);
    const { out } = await capture(
      runRetry({ session: fixture.sessionPath, batchId: BATCH_ID, json: true }, submit.hooks.make),
      fixture
    );
    expect(out).toHaveLength(1);
    const printed = JSON.parse(out[0] ?? '') as { readonly disposition: string };
    expect(printed.disposition).toBe('receipt_terminal');
  });

  test('a cached terminal receipt prints as JSON too, with no request', async () => {
    const fixture = buildFixture(new Map<string, FakeRoute>());
    fixture.seed({
      batches: { [BATCH_ID]: batchBody(BATCH_ID) },
      receipts: { [BATCH_ID]: receiptWire(BATCH_ID, 'rejected') },
    });
    const { out } = await capture(
      runRetry({ session: fixture.sessionPath, batchId: BATCH_ID, json: true }),
      fixture
    );
    expect(fixture.requests).toEqual([]);
    const printed = JSON.parse(out[0] ?? '') as { readonly receipt_state: string };
    expect(printed.receipt_state).toBe('rejected');
  });
});
