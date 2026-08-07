/**
 * `play receipt`, the receipt line and the disposition line.
 *
 * Ports the receipt half of `test_v2_receipts_render_one_line_with_the_batch_id_last`
 * (test_client.py:1494) and the `receipt` arm of
 * `test_v2_json_escape_hatch_covers_turn_batch_retry_and_wait` (1040).
 *
 * The line shape is the contract every other command inherits — `do`, `batch`
 * and `retry` all print through `renderReceipt` — so the goldens live here and
 * the other units assert against these bytes rather than inventing their own.
 */
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import { FULL_CONTROL_V2 } from 'src/constants';
import { runReceipt } from 'src/commands/receipt.cmd';
import {
  observationLines,
  receiptLine,
  renderDisposition,
  renderReceipt,
} from 'src/render/receipt';
import { decodeBatchDisposition } from 'src/schema/batch';
import { decodeReceipt, type CommandReceipt } from 'src/schema/receipt';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import { batchIntent } from 'src/services/batch';
import { FIXTURE_AGENT_ID, FIXTURE_GAME_ID, identity, type FakeRoute } from 'test/_fixtures';
import {
  BATCH_ID,
  DESCRIPTOR,
  REVISION,
  bareState,
  batchBody,
  buildFixture,
  capture,
  cleanupScratches,
  errorEnvelope,
  messageOf,
  receiptWire,
  recordingHooks,
  tagOf,
} from 'test/receipt-harness';

afterEach(cleanupScratches);

const decode = (payload: JsonObject, batchId?: string): CommandReceipt =>
  Effect.runSync(
    decodeReceipt(payload, identity(), batchId === undefined ? {} : { batchId })
  );

// ---------------------------------------------------------------------------
// The line
// ---------------------------------------------------------------------------

describe('receiptLine', () => {
  test('applied: intent, outcome, revision, and the batch ID last', () => {
    const line = receiptLine(decode(receiptWire(BATCH_ID)), 'phase.end End phase {ready=yes}');
    expect(line).toBe(`phase.end End phase {ready=yes} → applied rev8/t3  ${BATCH_ID}`);
    // The rendered line is prose, not a payload dump.
    expect(line).not.toContain('schema_version');
    expect(line).not.toContain('receipt_state');
    expect(line.endsWith(BATCH_ID)).toBe(true);
  });

  test('rejected inlines the error text, so the reason is on the same line', () => {
    expect(receiptLine(decode(receiptWire(BATCH_ID, 'rejected')), 'batch')).toBe(
      `batch → rejected illegal_action: validated test error rev8/t3  ${BATCH_ID}`
    );
  });

  test('accepted says out loud that it is not final', () => {
    expect(receiptLine(decode(receiptWire(BATCH_ID, 'accepted')), 'batch')).toBe(
      `batch → accepted (not final; resolve with just receipt) rev8/t3  ${BATCH_ID}`
    );
  });

  test('idempotent is appended before the "not final" clause', () => {
    expect(
      receiptLine(decode(receiptWire(BATCH_ID, 'accepted', { idempotent: true })), 'batch')
    ).toBe(
      `batch → accepted idempotent (not final; resolve with just receipt) rev8/t3  ${BATCH_ID}`
    );
  });

  test('an ambiguous receipt reads as ambiguous, with its own code', () => {
    const line = receiptLine(decode(receiptWire(BATCH_ID, 'ambiguous')), 'batch');
    expect(line).toBe(
      `batch → ambiguous action_outcome_ambiguous: validated test error rev8/t3  ${BATCH_ID}`
    );
    // "applied" must never appear anywhere in an ambiguous line.
    expect(line).not.toContain('applied');
  });

  test('only "accepted" gets the not-final clause, never a terminal state', () => {
    for (const state of ['applied', 'rejected', 'ambiguous'] as const) {
      expect(receiptLine(decode(receiptWire(BATCH_ID, state)), 'batch')).not.toContain(
        'not final'
      );
    }
  });
});

// ---------------------------------------------------------------------------
// The observation
// ---------------------------------------------------------------------------

describe('observationLines', () => {
  const investigation = (
    surplus: number,
    improvements: ReadonlyArray<JsonValue>
  ): JsonObject => ({
    id: 'obs_1',
    type: 'city_investigation',
    source: 'human_client_city_info',
    freshness: 'captured_at_receipt_revision',
    state_revision: REVISION,
    city: {
      id: 'city_1',
      name: 'München',
      size: 4,
      production: { id: 'unit_warriors', kind: 'unit', name: 'Warriors' },
      shields: { stock: 12, surplus },
      improvements,
      citizens: {
        feelings: ['base', 'luxury', 'effects', 'nationality', 'martial_law', 'final'].map(
          (stage) => ({ stage, happy: 4, content: 0, unhappy: 0, angry: 0 })
        ),
        specialists: [],
      },
    },
  });

  test('one indented line, with the surplus signed', () => {
    const receipt = decode(
      receiptWire(BATCH_ID, 'applied', {
        observation: investigation(2, [{ id: 'b_1', name: 'Barracks' }]),
      })
    );
    if (receipt.observation === null) throw new Error('expected an observation');
    expect(observationLines(receipt.observation)).toEqual([
      '  investigated München sz4 Warriors 12 shields (+2/turn), 1 improvements',
    ]);
  });

  test('a negative surplus keeps its own sign rather than gaining a plus', () => {
    const receipt = decode(
      receiptWire(BATCH_ID, 'applied', { observation: investigation(-3, []) })
    );
    if (receipt.observation === null) throw new Error('expected an observation');
    expect(observationLines(receipt.observation)[0]).toContain('(-3/turn), 0 improvements');
  });

  test('renderReceipt appends it under the receipt line, never above', () => {
    const receipt = decode(
      receiptWire(BATCH_ID, 'applied', { observation: investigation(0, []) })
    );
    const lines = renderReceipt(receipt, 'city.investigate Investigate');
    expect(lines).toHaveLength(2);
    expect(lines[0]?.startsWith('city.investigate Investigate → applied')).toBe(true);
    expect(lines[1]?.startsWith('  investigated')).toBe(true);
    expect(lines[1]).toContain('(+0/turn)');
  });
});

// ---------------------------------------------------------------------------
// The disposition
// ---------------------------------------------------------------------------

describe('renderDisposition', () => {
  const disposition = (kind: string, overrides: JsonObject = {}): JsonObject => ({
    schema_version: 2,
    control_protocol: FULL_CONTROL_V2,
    game_id: FIXTURE_GAME_ID,
    agent_id: FIXTURE_AGENT_ID,
    batch_id: BATCH_ID,
    disposition: kind,
    receipt: null,
    error: null,
    ...overrides,
  });

  const decodeDisposition = (payload: JsonObject) =>
    Effect.runSync(decodeBatchDisposition(payload, identity()));

  test('a receipt-bearing disposition is just the receipt', () => {
    const value = decodeDisposition(
      disposition('receipt_terminal', { receipt: receiptWire(BATCH_ID) })
    );
    expect(renderDisposition(value, 'phase.end End phase')).toEqual(
      renderReceipt(decode(receiptWire(BATCH_ID)), 'phase.end End phase')
    );
  });

  test('no receipt and no error is "outcome unknown", never "failed"', () => {
    expect(
      renderDisposition(decodeDisposition(disposition('receipt_first')), 'phase.end End phase')
    ).toEqual([`phase.end End phase → outcome unknown next=receipt_first  ${BATCH_ID}`]);
  });

  test('a proved refusal says "not accepted" and names the next step', () => {
    const value = decodeDisposition(
      disposition('refresh', { error: errorEnvelope('illegal_action', null) })
    );
    expect(renderDisposition(value, 'unit.move Move')).toEqual([
      `unit.move Move → not accepted: illegal_action: validated test error next=refresh  ${BATCH_ID}`,
    ]);
  });
});

// ---------------------------------------------------------------------------
// The intent
// ---------------------------------------------------------------------------

describe('batchIntent', () => {
  test('a cached descriptor gives kind, label and the arguments', () => {
    expect(
      batchIntent(
        bareState({
          actions: { action_opaque: DESCRIPTOR },
          batches: { [BATCH_ID]: batchBody(BATCH_ID, { ready: true }) },
        }),
        BATCH_ID
      )
    ).toBe('phase.end End phase {ready=yes}');
  });

  test('no arguments means no brace block', () => {
    expect(
      batchIntent(
        bareState({
          actions: { action_opaque: DESCRIPTOR },
          batches: { [BATCH_ID]: batchBody(BATCH_ID, {}) },
        }),
        BATCH_ID
      )
    ).toBe('phase.end End phase');
  });

  test('a forgotten descriptor falls back to the action ID, not to "batch"', () => {
    expect(
      batchIntent(
        bareState({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } }),
        BATCH_ID
      )
    ).toBe('action_opaque {ready=yes}');
  });

  test('anything unreadable degrades to "batch" rather than throwing', () => {
    const cases = ['not json', '[1,2]', '{"commands":[{},{}]}', '{"commands":[{}]}', '"s"'];
    expect(batchIntent(bareState(), BATCH_ID)).toBe('batch');
    for (const encoded of cases) {
      expect(batchIntent(bareState({ batches: { [BATCH_ID]: encoded } }), BATCH_ID)).toBe(
        'batch'
      );
    }
  });

  test('multiple arguments keep the persisted (canonical, sorted) order', () => {
    expect(
      batchIntent(
        bareState({
          actions: { action_opaque: DESCRIPTOR },
          batches: {
            [BATCH_ID]: batchBody(BATCH_ID, { name: 'München', zeal: 2, ready: false }),
          },
        }),
        BATCH_ID
      )
    ).toBe('phase.end End phase {name=München,ready=no,zeal=2}');
  });
});

// ---------------------------------------------------------------------------
// The command
// ---------------------------------------------------------------------------

describe('play receipt', () => {
  const route = (body: JsonObject, status = 200): ReadonlyMap<string, FakeRoute> =>
    new Map([['/receipts/', { status, body }]]);

  test('it prints one line, and the batch ID is the tail of it', async () => {
    const fixture = buildFixture(route(receiptWire(BATCH_ID)));
    fixture.seed({
      actions: { action_opaque: DESCRIPTOR },
      batches: { [BATCH_ID]: batchBody(BATCH_ID) },
    });
    const { out, err, result } = await capture(
      runReceipt({ session: fixture.sessionPath, batchId: BATCH_ID, json: false }),
      fixture
    );
    expect(Either.isRight(result)).toBe(true);
    expect(out).toEqual([`phase.end End phase {ready=yes} → applied rev8/t3  ${BATCH_ID}`]);
    expect(err).toEqual([]);
  });

  test('an unknown batch still renders, with "batch" as the intent', async () => {
    const other = `batch_${'S'.repeat(24)}`;
    const fixture = buildFixture(route(receiptWire(other, 'rejected')));
    const { out, result } = await capture(
      runReceipt({ session: fixture.sessionPath, batchId: other, json: false }),
      fixture
    );
    expect(Either.isRight(result)).toBe(true);
    expect(out).toHaveLength(1);
    expect(out[0]).toContain('→ rejected illegal_action: validated test error');
    expect(out[0]?.startsWith('batch → ')).toBe(true);
    expect(out[0]?.endsWith(other)).toBe(true);
  });

  test('the receipt is remembered and mirrored, tagged "receipt"', async () => {
    const fixture = buildFixture(route(receiptWire(BATCH_ID)));
    fixture.seed({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } });
    const hooks = recordingHooks();
    await capture(
      runReceipt({ session: fixture.sessionPath, batchId: BATCH_ID, json: false }, hooks.make),
      fixture
    );
    expect(hooks.remembered.map((receipt) => receipt.batch_id)).toEqual([BATCH_ID]);
    expect(hooks.mirrored).toEqual([[BATCH_ID, 'receipt']]);
    // And it landed in `.v2-state`, so `retry` can answer from the cache.
    expect(fixture.readState().receipts[BATCH_ID]).not.toBeUndefined();
  });

  test('--json prints the validated envelope and nothing else on stdout', async () => {
    const fixture = buildFixture(route(receiptWire(BATCH_ID)));
    fixture.seed({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } });
    const { out } = await capture(
      runReceipt({ session: fixture.sessionPath, batchId: BATCH_ID, json: true }),
      fixture
    );
    expect(out).toHaveLength(1);
    expect(JSON.parse(out[0] ?? '') as unknown).toEqual(
      JSON.parse(JSON.stringify(decode(receiptWire(BATCH_ID)))) as unknown
    );
  });

  test('a non-2xx body is raised as a validated refusal, not rendered', async () => {
    const fixture = buildFixture(route(errorEnvelope('invalid_request', null), 404));
    const { out, result } = await capture(
      runReceipt({ session: fixture.sessionPath, batchId: BATCH_ID, json: false }),
      fixture
    );
    expect(out).toEqual([]);
    expect(Either.isLeft(result)).toBe(true);
    if (Either.isLeft(result)) expect(tagOf(result.left)).toBe('V2ResponseError');
  });

  test('a malformed batch ID is refused before any request is made', async () => {
    const fixture = buildFixture(new Map<string, FakeRoute>());
    const { result } = await capture(
      runReceipt({ session: fixture.sessionPath, batchId: 'not a batch id', json: false }),
      fixture
    );
    expect(Either.isLeft(result)).toBe(true);
    if (Either.isLeft(result)) expect(messageOf(result.left)).toBe('invalid batch ID');
  });

  test('a receipt naming a different batch than the one asked for is drift', async () => {
    const fixture = buildFixture(route(receiptWire(`batch_${'Z'.repeat(24)}`)));
    const { result } = await capture(
      runReceipt({ session: fixture.sessionPath, batchId: BATCH_ID, json: false }),
      fixture
    );
    expect(Either.isLeft(result)).toBe(true);
    if (Either.isLeft(result)) expect(messageOf(result.left)).toContain('v2 receipt');
  });

  test('`receipt` never sends a batch, whatever it is handed', async () => {
    // The command remedy `receipt_first` promises this: running it cannot make
    // anything worse. Every route but the receipt GET 404s loudly.
    const fixture = buildFixture(new Map([['/receipts/', { body: receiptWire(BATCH_ID) }]]));
    fixture.seed({ batches: { [BATCH_ID]: batchBody(BATCH_ID) } });
    const hooks = recordingHooks();
    const { result } = await capture(
      runReceipt({ session: fixture.sessionPath, batchId: BATCH_ID, json: false }, hooks.make),
      fixture
    );
    expect(Either.isRight(result)).toBe(true);
    expect(hooks.submits).toEqual([]);
  });
});
