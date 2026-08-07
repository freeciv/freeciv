/**
 * The compact projection and the two windows that bound it.
 *
 * Ports `test_v2_compact_legal_action_retains_semantic_discriminators`
 * (test_client.py:714) and the `_legal_query` / `_limit` assertions inside
 * `test_v2_legal_all_requires_a_scope_and_keeps_the_kind_form` (4957) and
 * `test_v2_result_accepts_positional_or_named_id_and_state_hints` (473).
 *
 * The leak guard is the reason this file exists: a subject key naming an
 * internal term must lose its *value* and keep its *name*, because a row that
 * silently dropped a discriminator would tell an agent two different actions
 * are the same one.
 */
import { describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import { V2_WITHHELD } from 'src/constants';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import type { Revision } from 'src/schema/revision';
import {
  COMPACT_LIMIT_REFUSAL,
  OFFSET_REFUSAL,
  compactActionBytes,
  compactLegalAction,
  compactLegalLimit,
  compactLegalOffset,
} from 'src/services/legal-compact';
import { PAGE_LIMIT_REFUSAL, legalQuery, pageLimit } from 'src/services/legal-query';

const run = <A, E>(effect: Effect.Effect<A, E>): Either.Either<A, E> =>
  Effect.runSync(Effect.either(effect));

const ok = <A, E>(either: Either.Either<A, E>): A => {
  if (Either.isLeft(either)) throw new Error(`expected success: ${JSON.stringify(either.left)}`);
  return either.right;
};

const failure = <A, E extends { readonly message: string }>(
  either: Either.Either<A, E>
): string => {
  if (Either.isRight(either)) throw new Error(`expected failure, got ${JSON.stringify(either.right)}`);
  return either.left.message;
};

const revision = (number: number, turn = 3): Revision => ({
  turn,
  revision: number,
  state_token: `state_${String(number).padStart(32, '0')}`,
});

const UNIT = `unit_${'a'.repeat(32)}`;
const CITY = `city_${'b'.repeat(32)}`;

const descriptor = (overrides: JsonObject): JsonObject => ({
  action_id: 'action_opaque',
  kind: 'phase.end',
  label: 'End phase',
  subject: { operation: 'end' },
  arguments_schema: { type: 'object' },
  state_revision: revision(12) as unknown as JsonValue,
  ...overrides,
});

// ---------------------------------------------------------------------------
// _compact_legal_action
// ---------------------------------------------------------------------------

describe('compactLegalAction', () => {
  const order = descriptor({
    action_id: 'action_order',
    kind: 'unit.order',
    label: 'Sentry Warrior',
    subject: {
      operation: 'order',
      order: 'sentry',
      actor: { id: UNIT, type: 'unit' },
      target: null,
      probability: { kind: 'exact', minimum_percent: 100, maximum_percent: 100 },
      internal_native_packet: 77,
      private_context: 'not part of the public projection',
      wire_sequence: 88,
    },
  });

  const perform = descriptor({
    action_id: 'action_perform',
    kind: 'unit.perform_action',
    label: 'Sabotage City production',
    subject: {
      operation: 'perform_action',
      action: 'sabotage_city',
      building_choice: { id: 'improvement_choice', name: 'Production' },
      target: { id: CITY, type: 'city', name: 'Target City' },
    },
  });

  test('a reserved key keeps its name and loses only its value', () => {
    const compact = ok(run(compactLegalAction(order)));
    expect(compact['label']).toBe('Sentry Warrior');
    expect(compact['subject']).toEqual({
      operation: 'order',
      order: 'sentry',
      actor: { id: UNIT, type: 'unit' },
      internal_native_packet: V2_WITHHELD,
      private_context: V2_WITHHELD,
      wire_sequence: V2_WITHHELD,
    });
    // Doc §5 forbids unconditional field omission; only defaults may be elided.
    for (const key of ['internal_native_packet', 'private_context', 'wire_sequence']) {
      expect(compact['subject']).toHaveProperty(key);
    }
  });

  test('a leading underscore is withheld even when no term matches', () => {
    const hidden = descriptor({
      subject: { operation: 'end', _staging_slot: 'secret' },
    });
    const compact = ok(run(compactLegalAction(hidden)));
    expect(compact['subject']).toEqual({ operation: 'end', _staging_slot: V2_WITHHELD });
  });

  test('a non-reserved discriminator survives whole, and the target is lifted out', () => {
    const compact = ok(run(compactLegalAction(perform)));
    expect(compact['subject']).toEqual({
      operation: 'perform_action',
      action: 'sabotage_city',
      building_choice: { id: 'improvement_choice', name: 'Production' },
    });
    expect(compact['target']).toEqual({ id: CITY, type: 'city', name: 'Target City' });
  });

  test('the certain-probability envelope is the only one that renders away', () => {
    const compact = ok(run(compactLegalAction(order)));
    expect(Object.keys(compact).sort()).toEqual([
      'action_id',
      'argument_schema',
      'kind',
      'label',
      'subject',
      'target',
    ]);
    const gamble = descriptor({
      subject: {
        operation: 'end',
        probability: { kind: 'unknown', minimum_percent: 0, maximum_percent: 100 },
      },
    });
    expect(ok(run(compactLegalAction(gamble)))['probability']).toEqual({
      kind: 'unknown',
      minimum_percent: 0,
      maximum_percent: 100,
    });
    // A probability this client cannot interpret is still a probability.
    const unshaped = descriptor({ subject: { operation: 'end', probability: 'maybe' } });
    expect(ok(run(compactLegalAction(unshaped)))['probability']).toBe('maybe');
  });

  test('gold falls back to the target, and the schema supplies the range', () => {
    const fromTarget = descriptor({
      subject: { operation: 'buy', target: { id: CITY, gold_cost: 42 } },
      arguments_schema: { properties: { gold: { minimum: 1, maximum: 99, type: 'integer' } } },
    });
    const compact = ok(run(compactLegalAction(fromTarget)));
    expect(compact['gold_cost']).toBe(42);
    expect(compact['gold_range']).toEqual({ minimum: 1, maximum: 99 });
  });

  test('a subject that is not an object is drift, not an empty row', () => {
    expect(failure(run(compactLegalAction(descriptor({ subject: 'nope' }))))).toContain(
      'cannot render legal action subject'
    );
  });

  test('the byte budget is CPython json.dumps: sorted, tight, ASCII-escaped', () => {
    // `{"a":2,"b":1}` — keys sorted, no spaces.
    expect(compactActionBytes({ b: 1, a: 2 })).toBe(13);
    // `{"a":"é"}` — `ensure_ascii=True`, so the byte count is the escape's.
    expect(compactActionBytes({ a: 'é' })).toBe(14);
  });
});

// ---------------------------------------------------------------------------
// _compact_legal_offset / _compact_legal_limit / _limit
// ---------------------------------------------------------------------------

describe('compactLegalOffset', () => {
  test('an absent offset is zero', () => {
    expect(ok(run(compactLegalOffset('')))).toBe(0);
    expect(ok(run(compactLegalOffset(null)))).toBe(0);
    expect(ok(run(compactLegalOffset(undefined)))).toBe(0);
  });

  test('0 through 8192 are canonical integers and nothing else is', () => {
    expect(ok(run(compactLegalOffset('0')))).toBe(0);
    expect(ok(run(compactLegalOffset('8192')))).toBe(8192);
    for (const bad of ['8193', '007', '-1', '1.0', ' 4', '4 ', 'four', '+4']) {
      expect(failure(run(compactLegalOffset(bad)))).toBe(OFFSET_REFUSAL);
    }
  });
});

describe('compactLegalLimit', () => {
  test('the default depends on the form, not on the flag', () => {
    expect(ok(run(compactLegalLimit(null)))).toBe(64);
    expect(ok(run(compactLegalLimit('', 4096)))).toBe(4096);
  });

  test('1 through 64 only, canonical spelling only', () => {
    expect(ok(run(compactLegalLimit('1')))).toBe(1);
    expect(ok(run(compactLegalLimit('64')))).toBe(64);
    for (const bad of ['0', '65', '100', '06', '-1', '1.5']) {
      expect(failure(run(compactLegalLimit(bad)))).toBe(COMPACT_LIMIT_REFUSAL);
    }
  });
});

describe('pageLimit', () => {
  test('the server page size is 1 through 16', () => {
    expect(ok(run(pageLimit(null)))).toBe(16);
    expect(ok(run(pageLimit('1')))).toBe(1);
    expect(ok(run(pageLimit('16')))).toBe(16);
    for (const bad of ['0', '17', '64', '01', '']) {
      expect(failure(run(pageLimit(bad)))).toBe(PAGE_LIMIT_REFUSAL);
    }
  });
});

// ---------------------------------------------------------------------------
// _legal_query
// ---------------------------------------------------------------------------

const CURSOR = `cursor_${'a'.repeat(32)}`;
const TILE = `tile_${'b'.repeat(32)}`;
const RELATION = `relation_${'c'.repeat(32)}`;
const PLAYER = `player_${'f'.repeat(32)}`;

const query = (
  overrides: Partial<{
    cursor: string;
    actorId: string;
    targetId: string;
    limit: string | null;
  }> = {},
  options: { readonly ignoreLimit?: boolean } = {}
): Either.Either<string, { readonly message: string }> =>
  run(
    legalQuery(
      { cursor: '', actorId: '', targetId: '', limit: null, ...overrides },
      options
    )
  );

describe('legalQuery', () => {
  test('no scope at all is an empty query', () => {
    expect(ok(query())).toBe('');
    expect(ok(query({ limit: '4' }))).toBe('limit=4');
    expect(ok(query({ actorId: UNIT }))).toBe(`actor_id=${UNIT}`);
  });

  test('a cursor must be the only page option', () => {
    expect(ok(query({ cursor: CURSOR }))).toBe(`cursor=${CURSOR}`);
    for (const clash of [{ actorId: UNIT }, { targetId: TILE }, { limit: '4' }]) {
      expect(failure(query({ cursor: CURSOR, ...clash }))).toBe(
        'legal cursor must be the only page option'
      );
    }
    expect(failure(query({ cursor: 'cursor_short' }))).toBe(
      'legal cursor must be the only page option'
    );
  });

  test('a target needs an actor, and the pair must be the right ID types', () => {
    expect(failure(query({ targetId: TILE }))).toBe('legal target requires actor');
    expect(ok(query({ actorId: PLAYER, targetId: TILE }))).toBe(
      `actor_id=${PLAYER}&target_id=${TILE}`
    );
    expect(failure(query({ actorId: TILE, targetId: TILE }))).toBe(
      'actor or target ID has the wrong v2 ID type'
    );
    expect(failure(query({ actorId: PLAYER, targetId: 'nope' }))).toBe(
      'actor or target ID has the wrong v2 ID type'
    );
  });

  test('a relation target takes no limit; a tile target does', () => {
    expect(failure(query({ actorId: PLAYER, targetId: RELATION, limit: '4' }))).toBe(
      'legal relation target does not accept a limit'
    );
    expect(ok(query({ actorId: PLAYER, targetId: TILE, limit: '4' }))).toBe(
      `actor_id=${PLAYER}&target_id=${TILE}&limit=4`
    );
  });

  test('a relation named as the actor is refused by naming the form that works', () => {
    const message = failure(query({ actorId: RELATION }));
    expect(message).toContain('a relation ID is a diplomacy target, not an actor');
    expect(message).toContain(`--target_id ${RELATION} --all`);
    expect(message).toContain('just state --section overview');
    expect(failure(query({ actorId: 'unit_short' }))).toBe(
      'actor ID has the wrong v2 ID type'
    );
  });

  test('--all reads --limit as a window, so the query never carries limit=', () => {
    expect(ok(query({ actorId: UNIT, limit: '2' }, { ignoreLimit: true }))).toBe(
      `actor_id=${UNIT}`
    );
    expect(ok(query({ limit: '64' }, { ignoreLimit: true }))).toBe('');
  });
});
