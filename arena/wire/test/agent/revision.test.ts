/**
 * State revisions, checked against `_validate_revision`
 * (`play/client.py:1304-1316`) and `_revision_order` (`:2407-2408`).
 *
 * Two faces are pinned separately: {@link decodeRevision}, which tolerates an
 * added field, and {@link decodeRevisionExact}, which reproduces the Python's
 * closed key set and its refusal sentences verbatim.
 */
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import { CANON_ASCII, canonicalText } from 'src/canon';
import {
  compareRevisions,
  decodeRevision,
  decodeRevisionExact,
  REVISION_FIELDS,
  Revision,
  revisionCanonRecord,
  revisionOrder,
  revisionsEqual,
  StateRevision,
} from 'src/agent/revision';
import { OpaqueId } from 'src/agent/ids';
import { encodeTolerant } from 'src/tolerant';

const accepts = (either: Either.Either<unknown, unknown>): boolean => Either.isRight(either);

const message = <A>(either: Either.Either<A, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

const value = <A, E>(either: Either.Either<A, E>): A => {
  expect(Either.isRight(either)).toBe(true);
  if (Either.isLeft(either)) throw new Error('expected a decoded value');
  return either.right;
};

// play-cli/test/_fixtures/wire.ts:18-22
const token = (text: string): OpaqueId => OpaqueId.make(text);

const FIXTURE_REVISION: Revision = {
  turn: 5,
  revision: 12,
  state_token: token('token_5_12'),
};

describe('decodeRevision', () => {
  test('decodes the three-field shape', () => {
    // play-cli/test/schema.test.ts:91-93
    expect(value(decodeRevision({ ...FIXTURE_REVISION }))).toEqual(FIXTURE_REVISION);
  });

  test('refuses a negative counter', () => {
    // play-cli/test/schema.test.ts:95-99
    expect(accepts(decodeRevision({ ...FIXTURE_REVISION, revision: -1 }))).toBe(false);
    expect(accepts(decodeRevision({ ...FIXTURE_REVISION, turn: -1 }))).toBe(false);
  });

  test('booleans are not integers', () => {
    // play-cli/test/schema.test.ts:101-105
    expect(accepts(decodeRevision({ ...FIXTURE_REVISION, turn: true }))).toBe(false);
    expect(accepts(decodeRevision({ ...FIXTURE_REVISION, revision: false }))).toBe(false);
  });

  test('a fractional counter is not an integer either', () => {
    expect(accepts(decodeRevision({ ...FIXTURE_REVISION, turn: 5.5 }))).toBe(false);
  });

  test('turn zero is a real turn', () => {
    expect(accepts(decodeRevision({ ...FIXTURE_REVISION, turn: 0, revision: 0 }))).toBe(true);
  });

  test('the state token must satisfy OPAQUE_ID_RE', () => {
    expect(accepts(decodeRevision({ ...FIXTURE_REVISION, state_token: 'has space' }))).toBe(false);
    expect(accepts(decodeRevision({ ...FIXTURE_REVISION, state_token: '' }))).toBe(false);
    expect(accepts(decodeRevision({ ...FIXTURE_REVISION, state_token: 7 }))).toBe(false);
  });

  test('every field is required', () => {
    expect(accepts(decodeRevision({ turn: 5, revision: 12 }))).toBe(false);
    expect(accepts(decodeRevision({ turn: 5, state_token: 'token' }))).toBe(false);
    expect(accepts(decodeRevision({}))).toBe(false);
    expect(accepts(decodeRevision(null))).toBe(false);
  });

  test('an added field is preserved and re-encoded — the tolerance rule', () => {
    const payload = { ...FIXTURE_REVISION, epoch: 3 };
    const decoded = value(decodeRevision(payload));
    expect(decoded.turn).toBe(5);
    const roundTripped = value(encodeTolerant(Revision)(decoded));
    expect(roundTripped).toEqual(payload);
    expect(Object.keys(roundTripped)).toEqual([
      'turn',
      'revision',
      'state_token',
      'epoch',
    ]);
  });

  test('StateRevision is the same schema under the wire field name', () => {
    expect(StateRevision).toBe(Revision);
  });
});

describe('decodeRevisionExact', () => {
  test('accepts the fixture and returns exactly the three fields', () => {
    expect(value(decodeRevisionExact({ ...FIXTURE_REVISION }))).toEqual(FIXTURE_REVISION);
  });

  test('names a missing field the way the Python does', () => {
    // play-cli/test/aliases.test.ts:626 asserts this sentence.
    expect(message(decodeRevisionExact({ turn: 5, revision: 12 }))).toBe(
      'invalid state revision: missing state_token. ' +
        'Expected exactly revision, state_token, turn',
    );
  });

  test('refuses an added field — the one place closedness is kept', () => {
    expect(message(decodeRevisionExact({ ...FIXTURE_REVISION, epoch: 3 }))).toBe(
      'invalid state revision: unexpected epoch. Expected exactly revision, state_token, turn',
    );
    // The tolerant door accepts the very same payload.
    expect(accepts(decodeRevision({ ...FIXTURE_REVISION, epoch: 3 }))).toBe(true);
  });

  test('a non-object names what it wanted', () => {
    expect(message(decodeRevisionExact(7))).toBe(
      'invalid state revision: expected a JSON object with exactly revision, state_token, turn',
    );
  });

  test('both counters share one sentence', () => {
    // play-cli/test/schema.test.ts:95-105 and client.py:1311.
    expect(message(decodeRevisionExact({ ...FIXTURE_REVISION, revision: -1 }))).toBe(
      'invalid state revision counters',
    );
    expect(message(decodeRevisionExact({ ...FIXTURE_REVISION, turn: true }))).toBe(
      'invalid state revision counters',
    );
    expect(message(decodeRevisionExact({ ...FIXTURE_REVISION, turn: 5.5 }))).toBe(
      'invalid state revision counters',
    );
  });

  test('the token has its own sentence', () => {
    expect(message(decodeRevisionExact({ ...FIXTURE_REVISION, state_token: 'has space' }))).toBe(
      'invalid state token',
    );
  });

  test('the counters are checked before the token, as in the Python', () => {
    const both = { turn: -1, revision: 12, state_token: 'has space' };
    expect(message(decodeRevisionExact(both))).toBe('invalid state revision counters');
  });

  test('the field set is the one the Python passes to _exact', () => {
    expect([...REVISION_FIELDS].toSorted()).toEqual(['revision', 'state_token', 'turn']);
  });
});

describe('ordering', () => {
  const at = (turn: number, revision: number): Revision => ({
    turn,
    revision,
    state_token: token(`token_${String(turn)}_${String(revision)}`),
  });

  test('revisionOrder is the (turn, revision) tuple', () => {
    expect(revisionOrder(FIXTURE_REVISION)).toEqual([5, 12]);
  });

  test('turn dominates revision', () => {
    expect(compareRevisions(at(5, 99), at(6, 0))).toBe(-1);
    expect(compareRevisions(at(6, 0), at(5, 99))).toBe(1);
  });

  test('revision breaks a turn tie', () => {
    expect(compareRevisions(at(5, 11), at(5, 12))).toBe(-1);
    expect(compareRevisions(at(5, 12), at(5, 11))).toBe(1);
    expect(compareRevisions(at(5, 12), at(5, 12))).toBe(0);
  });

  test('the token is not part of the order but is part of equality', () => {
    const left: Revision = { turn: 5, revision: 12, state_token: token('token_a') };
    const right: Revision = { turn: 5, revision: 12, state_token: token('token_b') };
    expect(compareRevisions(left, right)).toBe(0);
    expect(revisionsEqual(left, right)).toBe(false);
    expect(revisionsEqual(left, { ...left })).toBe(true);
  });

  test('sorting a list of revisions puts the oldest first', () => {
    const sorted = [at(6, 0), at(5, 12), at(5, 3)].toSorted(compareRevisions);
    expect(sorted.map(revisionOrder)).toEqual([
      [5, 3],
      [5, 12],
      [6, 0],
    ]);
  });
});

describe('revisionCanonRecord', () => {
  test('spells the counters as Python ints, so canonical JSON matches', () => {
    // `_legacy_catalog_id` (client.py:1416-1423) hashes the revision inside
    // `json.dumps(..., sort_keys=True, separators=(",", ":"))`, where a Python
    // int prints without a decimal point.
    const canonical = canonicalText(revisionCanonRecord(FIXTURE_REVISION), CANON_ASCII);
    expect(value(canonical)).toBe('{"revision":12,"state_token":"token_5_12","turn":5}');
  });

  test('a number-spelled counter would have printed as a float', () => {
    // The trap this conversion exists to close: canon.ts renders `number` as a
    // Python float, so the digest would differ from the Python's.
    const wrong = canonicalText(
      { turn: 5, revision: 12, state_token: 'token_5_12' },
      CANON_ASCII,
    );
    expect(value(wrong)).toBe('{"revision":12.0,"state_token":"token_5_12","turn":5.0}');
  });

  test('keys come out in Python sort_keys order', () => {
    const canonical = value(canonicalText(revisionCanonRecord(FIXTURE_REVISION), CANON_ASCII));
    expect(canonical.indexOf('revision')).toBeLessThan(canonical.indexOf('state_token'));
    expect(canonical.indexOf('state_token')).toBeLessThan(canonical.indexOf('"turn"'));
  });
});
