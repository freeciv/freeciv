/**
 * The wire schema layer.
 *
 * The assertions that matter are the *refusal* sentences: `test_client.py`
 * checks them verbatim (`"invalid v2 health: unexpected future_field"`,
 * `"unexpected invented_field"`), so a reworded message is a behavioural break
 * even though nothing about the happy path changed.
 */
import { describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import {
  cursorExpiry,
  decodeError,
  decodeHealth,
  decodeLegalPage,
  decodePage,
  decodeReceipt,
  decodeRevision,
  decodeWait,
  exact,
  jsonValue,
  legacyCatalogId,
  opaque,
  safeNumber,
} from 'src/schema';
import {
  FIXTURE_CURSOR,
  FIXTURE_REVISION,
  errorPayload,
  healthPayload,
  identity,
  legalPagePayload,
  pagePayload,
  receiptPayload,
  waitPayload,
} from 'test/_fixtures';

const run = <A, E>(effect: Effect.Effect<A, E>): Either.Either<A, E> =>
  Effect.runSync(Effect.either(effect));

const failureMessage = <A>(either: Either.Either<A, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

const value = <A, E>(either: Either.Either<A, E>): A => {
  expect(Either.isRight(either)).toBe(true);
  if (Either.isLeft(either)) throw new Error('expected a decoded value');
  return either.right;
};

describe('primitives', () => {
  test('exact names the drift rather than dumping the field list', () => {
    const either = run(exact({ a: 1, c: 3 }, new Set(['a', 'b']), 'thing'));
    expect(failureMessage(either)).toBe(
      'invalid thing: missing b; unexpected c. Expected exactly a, b'
    );
  });

  test('exact refuses a non-object by naming what it wanted', () => {
    expect(failureMessage(run(exact(7, new Set(['a', 'b']), 'thing')))).toBe(
      'invalid thing: expected a JSON object with exactly a, b'
    );
  });

  test('exact passes an object whose keys match', () => {
    expect(value(run(exact({ a: 1 }, new Set(['a']), 'thing')))).toEqual({ a: 1 });
  });

  test('opaque enforces the ID alphabet', () => {
    expect(value(run(opaque('unit_0.a:b-c', 'x')))).toBe('unit_0.a:b-c');
    expect(failureMessage(run(opaque('has space', 'action ID')))).toBe('invalid action ID');
  });

  test('jsonValue refuses non-finite numbers and over-deep nesting', () => {
    expect(failureMessage(run(jsonValue(Number.POSITIVE_INFINITY, 'x')))).toBe(
      'invalid x: number is not finite'
    );
    const deep = Array.from({ length: 14 }).reduce<unknown>((acc) => ({ n: acc }), 1);
    expect(failureMessage(run(jsonValue(deep, 'x')))).toBe('invalid x: JSON is nested too deeply');
  });

  test('safeNumber refuses negatives and honours nullable', () => {
    expect(value(run(safeNumber(0, 'x')))).toBe(0);
    expect(failureMessage(run(safeNumber(-1, 'x')))).toBe('invalid x');
    expect(value(run(safeNumber(null, 'x', { nullable: true })))).toBeNull();
    expect(failureMessage(run(safeNumber(null, 'x')))).toBe('invalid x');
  });
});

describe('revision', () => {
  test('decodes the three-field shape', () => {
    expect(value(run(decodeRevision({ ...FIXTURE_REVISION })))).toEqual(FIXTURE_REVISION);
  });

  test('refuses a negative counter', () => {
    expect(
      failureMessage(run(decodeRevision({ ...FIXTURE_REVISION, revision: -1 })))
    ).toBe('invalid state revision counters');
  });

  test('booleans are not integers', () => {
    expect(failureMessage(run(decodeRevision({ ...FIXTURE_REVISION, turn: true })))).toBe(
      'invalid state revision counters'
    );
  });
});

describe('error envelope', () => {
  test('trims the message and keeps the code', () => {
    const decoded = value(run(decodeError(errorPayload())));
    expect(decoded.error.code).toBe('illegal_action');
    expect(decoded.error.retryable).toBe(false);
  });

  test('refuses an unknown error code', () => {
    const payload = errorPayload({
      error: { code: 'invented', message: 'x', retryable: false, details: {} },
    });
    expect(failureMessage(run(decodeError(payload)))).toBe('invalid v2 error response');
  });
});

describe('pages', () => {
  test('decodes a bare state page', () => {
    const decoded = value(run(decodePage(pagePayload(), identity())));
    expect(decoded.page.section).toBe('units');
    expect(decoded.page.items).toHaveLength(1);
    expect(decoded.page.catalog_id).toBeUndefined();
  });

  test('a cursor with no remaining items is a pagination refusal', () => {
    const payload = pagePayload([{ id: 'unit_0' }], {
      page: { section: 'units', items: [{ id: 'unit_0' }], total_items: 1, next_cursor: FIXTURE_CURSOR, cursor_expires_at: null },
    });
    expect(failureMessage(run(decodePage(payload, identity())))).toBe('invalid v2 page pagination');
  });

  test('a scope on a state page is refused', () => {
    const payload = pagePayload([{ id: 'unit_0' }], {
      page: {
        section: 'units',
        items: [],
        total_items: 0,
        next_cursor: null,
        scope: { actor_id: `unit_${'0'.repeat(32)}`, actor_type: 'unit' },
      },
    });
    expect(failureMessage(run(decodePage(payload, identity())))).toBe('invalid state page scope');
  });

  test('a legacy scoped legal page gets an inferred catalog identity', () => {
    const actorId = `unit_${'a'.repeat(32)}`;
    const payload = legalPagePayload(undefined, {
      page: {
        section: 'legal_actions',
        items: [],
        total_items: 0,
        next_cursor: null,
        scope: { actor_id: actorId, actor_type: 'unit' },
      },
    });
    const decoded = value(run(decodeLegalPage(payload, identity())));
    expect(decoded.page.catalog_complete).toBe(true);
    expect(decoded.page.catalog_id).toBe(
      legacyCatalogId(identity(), FIXTURE_REVISION, { actor_id: actorId, actor_type: 'unit' })
    );
    expect(decoded.page.catalog_id).toMatch(/^catalog_[A-Za-z0-9_-]{32}$/);
  });

  test('a response for another game is refused by the header', () => {
    const payload = pagePayload(undefined, { game_id: 'game_someoneelsesgamehandle00' });
    expect(failureMessage(run(decodePage(payload, identity())))).toBe(
      'invalid v2 page: response belongs to another game'
    );
  });

  test('cursorExpiry accepts only Z-terminated UTC', () => {
    expect(value(run(cursorExpiry('2026-08-07T12:00:00Z')))).toBe('2026-08-07T12:00:00Z');
    expect(failureMessage(run(cursorExpiry('2026-08-07T12:00:00+00:00')))).toBe(
      'invalid v2 page cursor expiry'
    );
  });
});

describe('receipts', () => {
  test('an applied receipt carries no error', () => {
    const decoded = value(run(decodeReceipt(receiptPayload(), identity())));
    expect(decoded.receipt_state).toBe('applied');
    expect(decoded.error).toBeNull();
  });

  test('an ambiguous receipt must carry the ambiguity code and must not be retryable', () => {
    const ambiguous = receiptPayload({
      receipt_state: 'ambiguous',
      error: {
        schema_version: 2,
        control_protocol: 'full-control-v2',
        error: {
          code: 'action_outcome_ambiguous',
          message: 'the outcome cannot be determined',
          retryable: true,
          details: {},
        },
        state_revision: { ...FIXTURE_REVISION },
      },
    });
    expect(failureMessage(run(decodeReceipt(ambiguous, identity())))).toBe(
      'invalid ambiguous receipt'
    );
  });

  test('a rejected receipt may not claim ambiguity', () => {
    const rejected = receiptPayload({
      receipt_state: 'rejected',
      error: {
        schema_version: 2,
        control_protocol: 'full-control-v2',
        error: {
          code: 'action_outcome_ambiguous',
          message: 'x',
          retryable: false,
          details: {},
        },
        state_revision: { ...FIXTURE_REVISION },
      },
    });
    expect(failureMessage(run(decodeReceipt(rejected, identity())))).toBe(
      'invalid rejected receipt'
    );
  });

  test('an observation is only legal on an applied receipt', () => {
    const accepted = receiptPayload({ receipt_state: 'accepted', observation: {} });
    expect(failureMessage(run(decodeReceipt(accepted, identity())))).toBe(
      'invalid v2 receipt observation'
    );
  });

  test('a receipt for another batch is refused when a batch is named', () => {
    const either = run(decodeReceipt(receiptPayload(), identity(), { batchId: 'batch_other' }));
    expect(failureMessage(either)).toBe('invalid v2 receipt');
  });
});

describe('health', () => {
  test('decodes the minimal running envelope', () => {
    const decoded = value(run(decodeHealth(healthPayload(), identity())));
    expect(decoded.game_state).toBe('running');
    expect(decoded.phase?.state).toBe('awaiting_agent');
    expect(decoded.last_recovery).toBeUndefined();
  });

  test('an additive top-level field is named in the refusal', () => {
    const payload = healthPayload({ future_field: 1 });
    expect(failureMessage(run(decodeHealth(payload, identity())))).toContain(
      'invalid v2 health: unexpected future_field'
    );
  });

  test('an unknown sidecar field names itself and the remedy', () => {
    const payload = healthPayload({ sidecar: { state: 'ready', generation: 1, brand_new_field: 2 } });
    const message = failureMessage(run(decodeHealth(payload, identity())));
    expect(message).toContain('unexpected sidecar field(s) brand_new_field');
    expect(message).toContain('re-materialize');
  });

  test('an optional-if-present field is accepted, not refused', () => {
    const payload = healthPayload({ last_recovery: null });
    const decoded = value(run(decodeHealth(payload, identity())));
    expect(decoded.last_recovery).toBeNull();
  });

  test('a terminal game may not retain an actionable phase', () => {
    const payload = healthPayload({ game_state: 'completed' });
    expect(failureMessage(run(decodeHealth(payload, identity())))).toBe(
      'terminal v2 health retained stale phase state'
    );
  });

  test('an unknown waiting_on kind quotes itself Python-style', () => {
    const health = healthPayload();
    const phase = { ...(health['phase'] as Record<string, unknown>) };
    phase['waiting_on'] = { kind: 'brand_new', summary: 's', seats: [], waiting_s: 1 };
    const message = failureMessage(
      run(decodeHealth({ ...health, phase }, identity()))
    );
    expect(message).toContain("unknown waiting_on kind 'brand_new'");
  });
});

describe('wait', () => {
  test('a phase_active wake must actually be this seat active phase', () => {
    const decoded = value(
      run(decodeWait(waitPayload(), identity(), { until: 'phase', afterStateToken: null }))
    );
    expect(decoded.wake_reason).toBe('phase_active');
  });

  test('phase_active over an inactive phase breaks the wake contract', () => {
    const health = healthPayload();
    const phase = { ...(health['phase'] as Record<string, unknown>), active: false };
    const payload = waitPayload({ health: { ...health, phase } });
    expect(
      failureMessage(
        run(decodeWait(payload, identity(), { until: 'phase', afterStateToken: null }))
      )
    ).toBe('invalid v2 wait wake contract');
  });

  test('an unknown wake reason is refused before the health is read', () => {
    const payload = waitPayload({ wake_reason: 'invented' });
    expect(
      failureMessage(
        run(decodeWait(payload, identity(), { until: 'phase', afterStateToken: null }))
      )
    ).toBe('invalid v2 wait wake reason');
  });
});
