/**
 * Command receipts and the observation they may carry, checked against
 * `_validate_receipt` (`play/client.py:1851-1937`) and
 * `_validate_investigation_observation` (`:1713-1848`).
 *
 * The suite is organized around the question each rule answers.  The
 * safety-critical ones — an `ambiguous` receipt may not be retryable, a
 * `rejected` one may not claim ambiguity, an observation may only ride an
 * `applied` receipt at its own revision — get a test each, phrased as the
 * mistake they prevent.
 */
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  carriesError,
  CommandReceipt,
  decodeReceipt,
  decodeReceiptFor,
  isTerminalReceiptState,
  V2_RECEIPT_STATES,
  V2_TERMINAL_RECEIPTS,
} from 'src/agent/receipt';
import {
  CityInvestigationObservation,
  decodeInvestigation,
  decodeInvestigationAt,
  FEELING_STAGES,
} from 'src/agent/observation';
import { decodeRevision } from 'src/agent/revision';
import { encodeTolerant } from 'src/tolerant';
import type { JsonObject, JsonValue } from 'src/json';
import {
  AGENT_ID,
  BATCH_ID,
  cityWire,
  errorEnvelope,
  feelingsWire,
  GAME_ID,
  investigationWire,
  NEXT_REVISION,
  REPUBLISHED_REVISION,
  receiptWire,
  REVISION,
  SESSION,
} from 'test/agent/wire-fixtures';

const accepts = (either: Either.Either<unknown, unknown>): boolean => Either.isRight(either);

const failure = (either: Either.Either<unknown, { readonly message: string }>): string =>
  Either.isLeft(either) ? either.left.message : '<accepted>';

const revision = Either.getOrThrow(decodeRevision(REVISION));

const applied = (observation: JsonValue): JsonObject =>
  receiptWire(BATCH_ID, 'applied', { observation });

// ---------------------------------------------------------------------------
// The happy path, and the tolerance guarantee
// ---------------------------------------------------------------------------

describe('a well-formed receipt', () => {
  test('the four states are exactly the Python vocabulary', () => {
    expect([...V2_RECEIPT_STATES]).toEqual(['accepted', 'applied', 'rejected', 'ambiguous']);
    expect([...V2_TERMINAL_RECEIPTS]).toEqual(['applied', 'rejected', 'ambiguous']);
    expect(V2_RECEIPT_STATES.filter(isTerminalReceiptState)).toEqual([
      'applied',
      'rejected',
      'ambiguous',
    ]);
    expect(V2_RECEIPT_STATES.filter(carriesError)).toEqual(['rejected', 'ambiguous']);
  });

  test.each([...V2_RECEIPT_STATES])('decodes a %s receipt', (state) => {
    expect(accepts(decodeReceipt(receiptWire(BATCH_ID, state)))).toBe(true);
  });

  test('decodes the payload the CLI actually reads', () => {
    const decoded = Either.getOrThrow(decodeReceipt(receiptWire()));
    expect(decoded.receipt_state).toBe('applied');
    expect(String(decoded.batch_id)).toBe(BATCH_ID);
    expect(decoded.state_revision.turn).toBe(3);
    expect(decoded.error).toBeNull();
    expect(decoded.observation).toBeNull();
  });

  test('a field a newer supervisor added survives decode and re-encode', () => {
    const grown = receiptWire(BATCH_ID, 'applied', { sidecar_latency_ms: 12 });
    const decoded = Either.getOrThrow(decodeReceipt(grown));
    expect(decoded).toMatchObject({ receipt_state: 'applied' });
    const reencoded = Either.getOrThrow(encodeTolerant(CommandReceipt)(decoded));
    expect(JSON.stringify(reencoded)).toBe(JSON.stringify(grown));
  });

  test('a missing field is still a refusal — tolerance is about excess only', () => {
    const { idempotent: _dropped, ...withoutIdempotent } = receiptWire();
    expect(accepts(decodeReceipt(withoutIdempotent))).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// Field-level strictness the Python has and tolerance does not relax
// ---------------------------------------------------------------------------

describe('field-level refusals', () => {
  test('an unknown receipt_state has no safe branch', () => {
    expect(accepts(decodeReceipt(receiptWire(BATCH_ID, 'partially_applied')))).toBe(false);
  });

  test('idempotent must be a boolean, never a truthy string', () => {
    expect(
      accepts(decodeReceipt(receiptWire(BATCH_ID, 'applied', { idempotent: 'true' }))),
    ).toBe(false);
  });

  test('batch_id must satisfy OPAQUE_ID_RE', () => {
    expect(accepts(decodeReceipt(receiptWire('batch id with spaces')))).toBe(false);
  });

  test('the protocol header is closed', () => {
    expect(
      accepts(decodeReceipt(receiptWire(BATCH_ID, 'applied', { schema_version: 1 }))),
    ).toBe(false);
    expect(
      accepts(
        decodeReceipt(receiptWire(BATCH_ID, 'applied', { control_protocol: 'strategic-v1' })),
      ),
    ).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// The error invariants — client.py:1871-1903
// ---------------------------------------------------------------------------

describe('the error a receipt does or does not carry', () => {
  test('a non-terminal receipt carrying an error is refused', () => {
    const wire = receiptWire(BATCH_ID, 'accepted', { error: errorEnvelope('illegal_action') });
    expect(failure(decodeReceipt(wire))).toContain('must not carry an error');
  });

  test('an applied receipt carrying an error is refused', () => {
    const wire = receiptWire(BATCH_ID, 'applied', { error: errorEnvelope('illegal_action') });
    expect(failure(decodeReceipt(wire))).toContain('must not carry an error');
  });

  test.each(['rejected', 'ambiguous'])('a %s receipt must explain itself', (state) => {
    const wire = receiptWire(BATCH_ID, state, { error: null });
    expect(failure(decodeReceipt(wire))).toContain('must carry a structured error');
  });

  test('a terminal error with no revision at all is refused', () => {
    const wire = receiptWire(BATCH_ID, 'rejected', {
      error: errorEnvelope('illegal_action', null),
    });
    expect(failure(decodeReceipt(wire))).toContain('different state revision');
  });

  test('a terminal error naming a later revision is refused', () => {
    const wire = receiptWire(BATCH_ID, 'rejected', {
      error: errorEnvelope('illegal_action', NEXT_REVISION),
    });
    expect(failure(decodeReceipt(wire))).toContain('different state revision');
  });

  test('the same counters under a republished token are a different state', () => {
    const wire = receiptWire(BATCH_ID, 'rejected', {
      error: errorEnvelope('illegal_action', REPUBLISHED_REVISION),
    });
    expect(accepts(decodeReceipt(wire))).toBe(false);
  });
});

describe('ambiguity, which is the whole reason these rules exist', () => {
  test('an ambiguous receipt must carry the ambiguous code', () => {
    const wire = receiptWire(BATCH_ID, 'ambiguous', { error: errorEnvelope('internal_error') });
    expect(failure(decodeReceipt(wire))).toContain('action_outcome_ambiguous');
  });

  test('a retryable ambiguous receipt is refused — retrying it could double-move', () => {
    const wire = receiptWire(BATCH_ID, 'ambiguous', {
      error: errorEnvelope('action_outcome_ambiguous', REVISION, true),
    });
    expect(failure(decodeReceipt(wire))).toContain('must not be retryable');
  });

  test('a non-retryable ambiguous receipt is the one legal shape', () => {
    const decoded = Either.getOrThrow(decodeReceipt(receiptWire(BATCH_ID, 'ambiguous')));
    expect(decoded.error?.error.code).toBe('action_outcome_ambiguous');
    expect(decoded.error?.error.retryable).toBe(false);
  });

  test('a rejected receipt may not borrow the ambiguous code', () => {
    const wire = receiptWire(BATCH_ID, 'rejected', {
      error: errorEnvelope('action_outcome_ambiguous'),
    });
    expect(failure(decodeReceipt(wire))).toContain('must not carry');
  });
});

// ---------------------------------------------------------------------------
// The observation — client.py:1905-1917 plus 1713-1848
// ---------------------------------------------------------------------------

describe('the observation a receipt may carry', () => {
  test('an applied receipt at the observation revision is accepted', () => {
    const decoded = Either.getOrThrow(decodeReceipt(applied(investigationWire())));
    expect(decoded.observation?.city.name).toBe('München');
    expect(decoded.observation?.city.citizens.feelings[5].stage).toBe('final');
  });

  test.each(['accepted', 'rejected', 'ambiguous'])(
    'a %s receipt may not carry one',
    (state) => {
      const wire = receiptWire(BATCH_ID, state, { observation: investigationWire() });
      expect(failure(decodeReceipt(wire))).toContain('only an applied receipt');
    },
  );

  test('an observation captured at another revision is not evidence', () => {
    const wire = applied(investigationWire({ state_revision: NEXT_REVISION }));
    expect(failure(decodeReceipt(wire))).toContain('different state revision');
  });

  test('the three provenance literals are closed', () => {
    expect(accepts(decodeInvestigation(investigationWire({ type: 'city_snapshot' })))).toBe(
      false,
    );
    expect(accepts(decodeInvestigation(investigationWire({ source: 'guess' })))).toBe(false);
    expect(accepts(decodeInvestigation(investigationWire({ freshness: 'current' })))).toBe(
      false,
    );
  });

  test('decodeInvestigationAt binds a standalone observation to a revision', () => {
    expect(accepts(decodeInvestigationAt(revision)(investigationWire()))).toBe(true);
    expect(
      accepts(decodeInvestigationAt(revision)(investigationWire({ state_revision: NEXT_REVISION }))),
    ).toBe(false);
  });

  test('unknown fields inside the city survive a round trip', () => {
    const grown = investigationWire({ city: cityWire({ food_stock: 7 }) });
    const decoded = Either.getOrThrow(decodeInvestigation(grown));
    const reencoded = Either.getOrThrow(encodeTolerant(CityInvestigationObservation)(decoded));
    expect(JSON.stringify(reencoded)).toBe(JSON.stringify(grown));
  });
});

describe('the city numbers have to add up', () => {
  test('every feeling stage must account for the whole population', () => {
    const torn = investigationWire({
      city: cityWire({ citizens: { feelings: feelingsWire(3), specialists: [] } }),
    });
    expect(failure(decodeInvestigation(torn))).toContain('citizens');
  });

  test('specialists count towards the size', () => {
    const withSpecialists = investigationWire({
      city: cityWire({
        size: 6,
        citizens: {
          feelings: feelingsWire(4),
          specialists: [{ id: 'sp_1', name: 'Scientist', count: 2 }],
        },
      }),
    });
    expect(accepts(decodeInvestigation(withSpecialists))).toBe(true);
  });

  test('a size-zero city does not exist', () => {
    const empty = investigationWire({
      city: cityWire({ size: 0, citizens: { feelings: feelingsWire(0), specialists: [] } }),
    });
    expect(accepts(decodeInvestigation(empty))).toBe(false);
  });

  test('the six feeling rows are positional, not a set', () => {
    const shuffled = feelingsWire(4).toReversed();
    const wire = investigationWire({
      city: cityWire({ citizens: { feelings: shuffled, specialists: [] } }),
    });
    expect(accepts(decodeInvestigation(wire))).toBe(false);
    expect(FEELING_STAGES).toHaveLength(6);
  });

  test('five feeling rows are not six', () => {
    const wire = investigationWire({
      city: cityWire({ citizens: { feelings: feelingsWire(4).slice(0, 5), specialists: [] } }),
    });
    expect(accepts(decodeInvestigation(wire))).toBe(false);
  });

  test('a duplicate improvement id means the capture merged two entities', () => {
    const wire = investigationWire({
      city: cityWire({
        improvements: [
          { id: 'b_1', name: 'Barracks' },
          { id: 'b_1', name: 'Granary' },
        ],
      }),
    });
    expect(failure(decodeInvestigation(wire))).toContain('improvement ids are not distinct');
  });

  test('a duplicate improvement name is refused too', () => {
    const wire = investigationWire({
      city: cityWire({
        improvements: [
          { id: 'b_1', name: 'Barracks' },
          { id: 'b_2', name: 'Barracks' },
        ],
      }),
    });
    expect(failure(decodeInvestigation(wire))).toContain('improvement names are not distinct');
  });

  test('duplicate specialists are refused by id and by name', () => {
    const duplicate = (rows: ReadonlyArray<JsonObject>): boolean =>
      accepts(
        decodeInvestigation(
          investigationWire({
            city: cityWire({
              size: 6,
              citizens: { feelings: feelingsWire(4), specialists: rows },
            }),
          }),
        ),
      );
    expect(
      duplicate([
        { id: 'sp_1', name: 'Scientist', count: 1 },
        { id: 'sp_1', name: 'Tax collector', count: 1 },
      ]),
    ).toBe(false);
    expect(
      duplicate([
        { id: 'sp_1', name: 'Scientist', count: 1 },
        { id: 'sp_2', name: 'Scientist', count: 1 },
      ]),
    ).toBe(false);
  });

  test('a negative shield surplus is legal; a negative stock is not', () => {
    const surplus = investigationWire({ city: cityWire({ shields: { stock: 12, surplus: -3 } }) });
    expect(accepts(decodeInvestigation(surplus))).toBe(true);
    const stock = investigationWire({ city: cityWire({ shields: { stock: -1, surplus: 2 } }) });
    expect(accepts(decodeInvestigation(stock))).toBe(false);
  });

  test('production kind is a two-value enum', () => {
    const wire = investigationWire({
      city: cityWire({ production: { id: 'x_1', kind: 'wonder', name: 'Pyramids' } }),
    });
    expect(accepts(decodeInvestigation(wire))).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// Bound to a session — client.py:1853-1866
// ---------------------------------------------------------------------------

describe('a receipt bound to a seat', () => {
  test('accepts the seat it was addressed to', () => {
    expect(accepts(decodeReceiptFor(SESSION)(receiptWire()))).toBe(true);
  });

  test('refuses a receipt for another game', () => {
    const wire = receiptWire(BATCH_ID, 'applied', { game_id: `${GAME_ID}_other` });
    expect(failure(decodeReceiptFor(SESSION)(wire))).toContain('another game');
  });

  test('refuses a receipt for another agent', () => {
    const wire = receiptWire(BATCH_ID, 'applied', { agent_id: `${AGENT_ID}f` });
    expect(failure(decodeReceiptFor(SESSION)(wire))).toContain('another agent');
  });

  test('refuses a receipt that answers a different batch', () => {
    const other = `batch_${'S'.repeat(24)}`;
    expect(failure(decodeReceiptFor(SESSION, { batchId: BATCH_ID })(receiptWire(other)))).toContain(
      'not',
    );
    expect(accepts(decodeReceiptFor(SESSION, { batchId: other })(receiptWire(other)))).toBe(true);
  });

  test('without a batchId any well-formed receipt for the seat is accepted', () => {
    const other = `batch_${'S'.repeat(24)}`;
    expect(accepts(decodeReceiptFor(SESSION)(receiptWire(other)))).toBe(true);
  });
});
