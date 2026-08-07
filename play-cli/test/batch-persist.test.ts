/**
 * The canonical body, and writing a batch down before it is sent.
 *
 * Ports the persistence half of
 * `test_v2_batch_persists_before_send_and_retry_is_receipt_first`
 * (test_client.py:3701) — the assertion that the bytes in `.v2-state`
 * re-canonicalize to themselves and never carry the seat credential — plus the
 * canonical-body test the unit brief asks for directly: *build the same batch
 * from differently-ordered input objects and assert identical bytes.*
 *
 * That last one is the whole idempotency contract in one assertion.  The server
 * de-duplicates a resend by the batch body it already saw; if key order or
 * number formatting could differ between two serializations of one order, a
 * `retry` would look like a second, different order and could apply twice.
 */
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import { FULL_CONTROL_V2 } from 'src/constants';
import { decodeLegalPage } from 'src/schema/page';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import { rememberPage, v2StateSchema } from 'src/services/aliases';
import { canonicalBody, canonicalText, parsePython } from 'src/services/canonical-body';
import { parseJsonObject } from 'src/services/batch';
import { persistBatchForAction } from 'src/services/batch-persist';
import { PrivateFs } from 'src/services/private-fs';
import {
  SessionStore,
  sessionStoreFor,
  type Session,
  type SessionStoreApi,
} from 'src/services/session-store';
import {
  FIXTURE_AGENT_ID,
  FIXTURE_CURSOR,
  FIXTURE_GAME_ID,
  scratchWorkspace,
  sessionFile,
  type Scratch,
} from 'test/_fixtures';

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

interface TestRevision {
  readonly turn: number;
  readonly revision: number;
  readonly state_token: string;
  readonly [key: string]: JsonValue;
}

const revision = (number = 7, turn = 3): TestRevision => ({
  turn,
  revision: number,
  state_token: `state_${String(number).padStart(32, '0')}`,
});

const UNIT_ONE = `unit_${'a'.repeat(32)}`;
const ACTION_ONE = `action_${'1'.repeat(26)}`;
const ACTION_TWO = `action_${'2'.repeat(26)}`;
const CATALOG = `catalog_${'c'.repeat(32)}`;

const descriptor = (
  stateRevision: TestRevision,
  actionId: string,
  overrides: JsonObject = {}
): JsonObject => ({
  action_id: actionId,
  kind: 'unit.found_city',
  label: 'Found city',
  subject: {
    operation: 'found_city',
    actor: { id: UNIT_ONE, type: 'unit', name: 'Settlers' },
  },
  arguments_schema: { type: 'object' },
  state_revision: stateRevision,
  ...overrides,
});

const legalPage = (
  stateRevision: TestRevision,
  items: ReadonlyArray<JsonValue>,
  page: JsonObject = {}
): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  state_revision: stateRevision,
  page: {
    section: 'legal_actions',
    items,
    total_items: items.length,
    next_cursor: null,
    cursor_expires_at: null,
    ...page,
  },
});

interface Fixture {
  readonly store: SessionStoreApi;
  readonly sessionPath: string;
  readonly session: Session;
  readonly run: <A, E>(
    effect: Effect.Effect<A, E, SessionStore | PrivateFs>
  ) => Either.Either<A, E>;
}

const fixture = (): Fixture => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const store = sessionStoreFor(scratch.workspace, scratch.files, v2StateSchema, {});
  const sessionPath = path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID, 'seat.json');
  Effect.runSync(scratch.files.writeJson(sessionPath, sessionFile()));
  const loaded = Effect.runSync(store.resolveV2(sessionPath));
  const layer = Layer.merge(
    Layer.succeed(SessionStore, store),
    Layer.succeed(PrivateFs, scratch.files)
  );
  return {
    store,
    sessionPath,
    session: loaded.session,
    run: (effect) => Effect.runSync(Effect.either(Effect.provide(effect, layer))),
  };
};

const ok = <A, E>(either: Either.Either<A, E>): A => {
  if (Either.isLeft(either)) {
    throw new Error(`expected success, got ${JSON.stringify(either.left)}`);
  }
  return either.right;
};

const failure = <A>(either: Either.Either<A, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

const ingest = (fx: Fixture, page: JsonObject): void => {
  ok(
    fx.run(
      Effect.flatMap(
        Effect.mapError(decodeLegalPage(page, fx.session), (error) => ({
          message: error.message,
        })),
        (decoded) => rememberPage(fx.sessionPath, fx.session, { legal: true, page: decoded })
      )
    )
  );
};

/** `_canonical_body(json.loads(text))` — the round trip the Python asserts. */
const reCanonical = (text: string): string => {
  const parsed = parsePython(text);
  expect(parsed.failure).toBe(null);
  return Effect.runSync(canonicalText(parsed.value));
};

const persistedBody = (fx: Fixture, batchId: string): string => {
  const state = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
  const stored = state.batches[batchId];
  expect(typeof stored).toBe('string');
  return stored as string;
};

// ---------------------------------------------------------------------------
// _canonical_body
// ---------------------------------------------------------------------------

describe('the canonical body', () => {
  const bytes = (value: JsonValue): Uint8Array => Effect.runSync(canonicalBody(value));

  test('the same order built from two differently-ordered objects is the same bytes', () => {
    const first: JsonValue = {
      schema_version: 2,
      control_protocol: FULL_CONTROL_V2,
      game_id: FIXTURE_GAME_ID,
      agent_id: FIXTURE_AGENT_ID,
      batch_id: 'batch_one',
      state_revision: { turn: 3, revision: 7, state_token: 'state_7' },
      commands: [{ action_id: ACTION_ONE, arguments: { name: 'London', x: 31, y: 72 } }],
    };
    const second: JsonValue = {
      commands: [{ arguments: { y: 72, name: 'London', x: 31 }, action_id: ACTION_ONE }],
      state_revision: { state_token: 'state_7', revision: 7, turn: 3 },
      batch_id: 'batch_one',
      agent_id: FIXTURE_AGENT_ID,
      game_id: FIXTURE_GAME_ID,
      control_protocol: FULL_CONTROL_V2,
      schema_version: 2,
    };
    expect(bytes(first)).toEqual(bytes(second));
    // Not merely equal — canonical: sorted keys, no spaces, at every depth.
    expect(Effect.runSync(canonicalText(first))).toBe(
      `{"agent_id":"${FIXTURE_AGENT_ID}","batch_id":"batch_one",` +
        `"commands":[{"action_id":"${ACTION_ONE}",` +
        '"arguments":{"name":"London","x":31,"y":72}}],' +
        `"control_protocol":"${FULL_CONTROL_V2}","game_id":"${FIXTURE_GAME_ID}",` +
        '"schema_version":2,' +
        '"state_revision":{"revision":7,"state_token":"state_7","turn":3}}'
    );
  });

  test('a non-ASCII argument stays UTF-8 — ensure_ascii=False is part of the identity', () => {
    const text = Effect.runSync(canonicalText({ city: 'München' }));
    expect(text).toBe('{"city":"München"}');
    expect(text).not.toContain('\\u');
    expect(Effect.runSync(canonicalBody({ city: 'München' })).length).toBe(
      new TextEncoder().encode(text).length
    );
  });

  test('a non-finite number is refused instead of emitted as a non-standard token', () => {
    const refused = (value: JsonValue): string =>
      failure(Effect.runSync(Effect.either(canonicalText(value))));
    // `allow_nan=False`'s `ValueError` names the offending value.
    expect(refused({ commands: [{ ratio: Number.POSITIVE_INFINITY }] })).toBe(
      'command batch is not canonical JSON: Out of range float values are not JSON ' +
        'compliant: inf'
    );
    expect(refused({ a: Number.NEGATIVE_INFINITY })).toBe(
      'command batch is not canonical JSON: Out of range float values are not JSON ' +
        'compliant: -inf'
    );
    expect(refused({ a: Number.NaN })).toBe(
      'command batch is not canonical JSON: Out of range float values are not JSON ' +
        'compliant: nan'
    );
    // `json.dumps` walks a mapping in sorted-key order, so that is which of two
    // non-finite values the sentence names.
    expect(refused({ z: Number.NaN, a: Number.POSITIVE_INFINITY })).toContain(': inf');
  });

  /**
   * The second `ValueError` `_canonical_body` catches, and the one that is a
   * fail-*open* if it is missed.
   *
   * `"\ud800"` is strict JSON, so `--arguments` can carry it and `json.loads`
   * decodes it to a lone surrogate.  `json.dumps(ensure_ascii=False)` copies it
   * through, and only `.encode("utf-8")` refuses — `UnicodeEncodeError` is a
   * `ValueError`, so CPython raises `command batch is not canonical JSON: …`
   * from inside `_persist_batch_for_action` *before*
   * `_save_v2_client_state_unlocked`: nothing is written and nothing is sent.
   *
   * `TextEncoder` would instead substitute `U+FFFD` silently, which sends a
   * mutation CPython refused, with a value the agent never wrote, and records a
   * `.v2-state` string that is not the bytes that went out.
   */
  test('a lone surrogate is refused, not silently replaced with U+FFFD', () => {
    const refused = (value: JsonValue): string =>
      failure(Effect.runSync(Effect.either(canonicalText(value))));
    expect(refused({ k: '\ud800' })).toBe(
      "command batch is not canonical JSON: 'utf-8' codec can't encode character " +
        "'\\ud800' in position 6: surrogates not allowed"
    );
    // The position is an index into the *dumped* text, in code points: an
    // astral character counts once, as it does in a Python string.
    expect(refused({ a: '😀\ud800' })).toBe(
      "command batch is not canonical JSON: 'utf-8' codec can't encode character " +
        "'\\ud800' in position 7: surrogates not allowed"
    );
    expect(refused({ '\ud800': 'v' })).toContain('in position 2:');
    expect(refused({ München: '\udfff' })).toBe(
      "command batch is not canonical JSON: 'utf-8' codec can't encode character " +
        "'\\udfff' in position 12: surrogates not allowed"
    );
    // The codec batches a maximal run into one error, and then says
    // "characters … in position X-Y" with no `repr`.
    expect(refused({ a: '\udfff\ud800' })).toBe(
      "command batch is not canonical JSON: 'utf-8' codec can't encode characters " +
        'in position 6-7: surrogates not allowed'
    );
    // A well-formed pair is one astral character and not a surrogate at all.
    expect(Effect.runSync(canonicalText({ a: '😀' }))).toBe('{"a":"😀"}');
    // `allow_nan` is reached first: `json.dumps` raises while walking the
    // value, and the encode that finds a surrogate only runs on its output.
    expect(refused({ a: Number.POSITIVE_INFINITY, b: '\ud800' })).toContain(
      'Out of range float values'
    );
  });

  test('the bytes form refuses too — the encoder is not reachable on its own', () => {
    const outcome = Effect.runSync(Effect.either(canonicalBody({ k: '\ud800' })));
    expect(failure(outcome)).toContain("codec can't encode character");
    // U+FFFD is 0xEF 0xBF 0xBD; nothing may ever emit it for a surrogate.
    expect(Effect.runSync(canonicalBody({ k: 'ok' }))).toEqual(
      new TextEncoder().encode('{"k":"ok"}')
    );
  });
});

// ---------------------------------------------------------------------------
// The number model: `int` is not `float`, and neither is a JavaScript number
// ---------------------------------------------------------------------------

/**
 * Every expectation below was produced by running the CPython original:
 *
 *     json.dumps(json.loads(TEXT), sort_keys=True,
 *                separators=(",", ":"), ensure_ascii=False, allow_nan=False)
 *
 * They are the bytes the supervisor de-duplicates on and the bytes `retry`
 * re-sends, so a divergence here is a mutation that can apply twice — or, for
 * the >2**53 case, a *different order* than the agent typed.
 */
describe('the canonical body carries CPython numbers', () => {
  const canonicalOf = (text: string): string => {
    const parsed = parsePython(text);
    expect(parsed.failure).toBe(null);
    return Effect.runSync(canonicalText(parsed.value));
  };

  test('an integral float keeps its float spelling, and an int keeps its own', () => {
    expect(canonicalOf('{"tax":40.0}')).toBe('{"tax":40.0}');
    expect(canonicalOf('{"tax":40}')).toBe('{"tax":40}');
    expect(canonicalOf('{"size":3.0,"count":3}')).toBe('{"count":3,"size":3.0}');
    // `1E2` has an exponent, so CPython's scanner routes it to `float`.
    expect(canonicalOf('{"a":1E2}')).toBe('{"a":100.0}');
  });

  test('repr, not String(): the exponent window and its two-digit exponent', () => {
    expect(canonicalOf('{"a":1e16}')).toBe('{"a":1e+16}');
    expect(canonicalOf('{"a":1e15}')).toBe('{"a":1000000000000000.0}');
    expect(canonicalOf('{"a":1e-7}')).toBe('{"a":1e-07}');
    expect(canonicalOf('{"a":0.00001}')).toBe('{"a":1e-05}');
    expect(canonicalOf('{"a":0.0001}')).toBe('{"a":0.0001}');
    expect(canonicalOf('{"a":-0.0}')).toBe('{"a":-0.0}');
    expect(canonicalOf('{"a":1e100}')).toBe('{"a":1e+100}');
    expect(canonicalOf('{"a":5e-324}')).toBe('{"a":5e-324}');
  });

  test('an integer wider than a double is carried exactly, not rounded to one', () => {
    // `JSON.parse` answers 10000000000000000000 here — a different order.
    expect(canonicalOf('{"a":10000000000000000001}')).toBe('{"a":10000000000000000001}');
    expect(canonicalOf('{"a":-12345678901234567890}')).toBe('{"a":-12345678901234567890}');
    expect(canonicalOf('{"a":9007199254740993}')).toBe('{"a":9007199254740993}');
  });

  test('two spellings of one order are the same bytes; two orders are not', () => {
    const first = canonicalOf('{"y":72,"name":"London","x":31,"tax":40.0}');
    const second = canonicalOf('{"tax":40.0,"x":31,"name":"London","y":72}');
    expect(first).toBe(second);
    expect(first).toBe('{"name":"London","tax":40.0,"x":31,"y":72}');
    expect(canonicalOf('{"tax":40}')).not.toBe(canonicalOf('{"tax":40.0}'));
  });

  test('a value the port built itself still serializes as the int CPython had', () => {
    expect(Effect.runSync(canonicalText({ schema_version: 2, turn: 3 }))).toBe(
      '{"schema_version":2,"turn":3}'
    );
  });

  test('the persisted arguments survive a round trip through the parser', () => {
    const text = canonicalOf('{"a":1e16,"b":40.0,"c":10000000000000000001,"d":"München"}');
    expect(canonicalOf(text)).toBe(text);
  });
});

// ---------------------------------------------------------------------------
// _persist_batch_for_action
// ---------------------------------------------------------------------------

describe('persistBatchForAction', () => {
  test('the persisted bytes re-canonicalize to themselves and carry no credential', () => {
    const fx = fixture();
    const rev = revision(7);
    ingest(fx, legalPage(rev, [descriptor(rev, ACTION_ONE)]));
    const batchId = ok(
      fx.run(
        persistBatchForAction(fx.sessionPath, fx.session, ACTION_ONE, { city: 'München' }, {
          token: () => 'A'.repeat(24),
        })
      )
    );
    expect(batchId).toBe(`batch_${'A'.repeat(24)}`);
    const stored = persistedBody(fx, batchId);
    // The Python's own assertion: parsing and re-serializing the persisted
    // string must reproduce it byte for byte.
    expect(reCanonical(stored)).toBe(stored);
    expect(stored).not.toContain('secret-token');
    expect(JSON.parse(stored) as JsonObject).toEqual({
      schema_version: 2,
      control_protocol: FULL_CONTROL_V2,
      game_id: FIXTURE_GAME_ID,
      agent_id: FIXTURE_AGENT_ID,
      batch_id: batchId,
      state_revision: { turn: 3, revision: 7, state_token: rev.state_token },
      commands: [{ action_id: ACTION_ONE, arguments: { city: 'München' } }],
    });
  });

  test('an --arguments float reaches .v2-state as the float CPython wrote', () => {
    const fx = fixture();
    const rev = revision(7);
    ingest(fx, legalPage(rev, [descriptor(rev, ACTION_ONE)]));
    const parsed = Effect.runSync(
      parseJsonObject('{"tax":40.0,"ratio":1e-7,"seed":10000000000000000001}', '--arguments')
    );
    const batchId = ok(
      fx.run(
        persistBatchForAction(fx.sessionPath, fx.session, ACTION_ONE, parsed, {
          token: () => 'A'.repeat(24),
        })
      )
    );
    const stored = persistedBody(fx, batchId);
    expect(stored).toContain(
      `"commands":[{"action_id":"${ACTION_ONE}",` +
        '"arguments":{"ratio":1e-07,"seed":10000000000000000001,"tax":40.0}}]'
    );
    // And the record `retry` re-sends is still its own fixed point.
    expect(reCanonical(stored)).toBe(stored);
  });

  /**
   * `_canonical_body` runs *inside* the state lock and *before*
   * `_save_v2_client_state_unlocked`, so its refusal leaves `.v2-state`
   * untouched: there is no batch record, and therefore nothing for `retry` or
   * `receipt` to resolve — because nothing was ever sent.
   */
  test('an --arguments surrogate refuses before anything is written', () => {
    const fx = fixture();
    const rev = revision(7);
    ingest(fx, legalPage(rev, [descriptor(rev, ACTION_ONE)]));
    const before = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
    const parsed = Effect.runSync(parseJsonObject('{"name":"\\ud800"}', '--arguments'));
    const outcome = fx.run(
      persistBatchForAction(fx.sessionPath, fx.session, ACTION_ONE, parsed, {
        token: () => 'A'.repeat(24),
      })
    );
    expect(failure(outcome)).toBe(
      "command batch is not canonical JSON: 'utf-8' codec can't encode character " +
        "'\\ud800' in position 163: surrogates not allowed"
    );
    const after = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
    expect(after.batches).toEqual(before.batches);
    expect(Object.keys(after.batches)).toHaveLength(0);
  });

  test('two identical orders persisted twice differ only in their batch ID', () => {
    const fx = fixture();
    const rev = revision(7);
    ingest(fx, legalPage(rev, [descriptor(rev, ACTION_ONE)]));
    const args = { name: 'London', ready: true };
    const first = ok(
      fx.run(
        persistBatchForAction(fx.sessionPath, fx.session, ACTION_ONE, args, {
          token: () => 'A'.repeat(24),
        })
      )
    );
    const second = ok(
      fx.run(
        persistBatchForAction(
          fx.sessionPath,
          fx.session,
          ACTION_ONE,
          { ready: true, name: 'London' },
          { token: () => 'B'.repeat(24) }
        )
      )
    );
    expect(persistedBody(fx, first).replace(first, 'ID')).toBe(
      persistedBody(fx, second).replace(second, 'ID')
    );
  });

  test('a colliding token is re-minted rather than overwriting a live batch', () => {
    const fx = fixture();
    const rev = revision(7);
    ingest(fx, legalPage(rev, [descriptor(rev, ACTION_ONE)]));
    const tokens = ['A'.repeat(24), 'A'.repeat(24), 'C'.repeat(24)];
    let index = 0;
    const token = (): string => tokens[Math.min(index++, tokens.length - 1)] as string;
    const first = ok(
      fx.run(persistBatchForAction(fx.sessionPath, fx.session, ACTION_ONE, {}, { token }))
    );
    const second = ok(
      fx.run(persistBatchForAction(fx.sessionPath, fx.session, ACTION_ONE, {}, { token }))
    );
    expect(first).toBe(`batch_${'A'.repeat(24)}`);
    expect(second).toBe(`batch_${'C'.repeat(24)}`);
    const state = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
    expect(Object.keys(state.batches).sort()).toEqual([first, second].sort());
  });

  test('an unknown action ID names the enumeration that would make it real', () => {
    const fx = fixture();
    const rev = revision(7);
    ingest(fx, legalPage(rev, [descriptor(rev, ACTION_ONE)]));
    const outcome = fx.run(
      persistBatchForAction(fx.sessionPath, fx.session, ACTION_TWO, {})
    );
    expect(failure(outcome)).toBe(
      'unknown or expired action ID; run the matching `just legal` query'
    );
  });

  test('a staged action is told apart from an expired one, and names its drain', () => {
    const fx = fixture();
    const rev = revision(7);
    // One page of a two-item catalog: the descriptor is cached but not
    // executable, because only a complete catalog is.
    ingest(
      fx,
      legalPage(rev, [descriptor(rev, ACTION_ONE)], {
        total_items: 2,
        next_cursor: FIXTURE_CURSOR,
        cursor_expires_at: '2999-01-01T00:00:00Z',
        scope: { actor_id: UNIT_ONE, actor_type: 'unit' },
        catalog_id: CATALOG,
        catalog_complete: false,
      })
    );
    const outcome = fx.run(
      persistBatchForAction(fx.sessionPath, fx.session, ACTION_ONE, {})
    );
    expect(failure(outcome)).toBe(
      'unknown or expired action ID: this action came from a catalog page that ' +
        'is still incomplete, and only a complete catalog is executable; run ' +
        '`just legal --actor_id u1 --all`'
    );
  });

  test('a descriptor from an older revision is refused, not sent hopefully', () => {
    const fx = fixture();
    const old = revision(7);
    ingest(fx, legalPage(old, [descriptor(old, ACTION_ONE)]));
    // A newer page retires the old catalog; force the stale descriptor back in
    // to prove the revision check, not the cache eviction.
    const state = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
    ok(
      fx.run(
        fx.store.writeState(fx.sessionPath, {
          ...state,
          last_revision: { turn: 3, revision: 9, state_token: revision(9).state_token },
          actions: { [ACTION_ONE]: descriptor(old, ACTION_ONE) },
        })
      )
    );
    const outcome = fx.run(
      persistBatchForAction(fx.sessionPath, fx.session, ACTION_ONE, {})
    );
    expect(failure(outcome)).toBe('the cached action is not from the latest revision');
  });
});
