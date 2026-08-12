/**
 * State revisions — the agent's position in the game's history.
 *
 * Clean-room Effect Schema re-expression of `play-cli/src/schema/revision.ts`,
 * porting `_validate_revision` (`play/client.py:1304-1316`) and
 * `_revision_order` (`play/client.py:2407-2408`).
 *
 * A revision is the pair `(turn, revision)` plus the server's opaque
 * `state_token`.  The pair is the ordering — a turn boundary resets nothing, so
 * `revision` is only meaningful within its `turn` — and the token is the
 * server's own name for that exact state, which requests echo back so a stale
 * client is told it is stale instead of acting on a state that moved.
 *
 * @module
 */

import { Either, Schema } from 'effect';
import type { CanonRecord } from '../canon.ts';
import { decodeTolerant, type TolerantDecoder, type WireDecodeError } from '../tolerant.ts';
import { OpaqueId } from './ids.ts';
import { driftError, exactFields } from './primitives.ts';

/**
 * A non-negative counter.
 *
 * Python's check is `isinstance(x, bool) or not isinstance(x, int) or x < 0`
 * (`play/client.py:1306-1310`) — note it excludes `bool` explicitly, because
 * `True` is an `int` in Python and `{"turn": true}` would otherwise decode to
 * turn 1.  JavaScript has no such hazard: `Schema.Number` already refuses a
 * boolean.  `Schema.int()` supplies the rest, and rejects a float `5.5` that
 * Python's `isinstance(x, int)` would also reject.
 */
export const RevisionCounter = Schema.Number.pipe(
  Schema.int(),
  Schema.nonNegative(),
).annotations({
  identifier: 'RevisionCounter',
  description: 'A non-negative integer revision counter (client.py _validate_revision)',
});
/** A non-negative counter. */
export type RevisionCounter = typeof RevisionCounter.Type;

/**
 * The three-field state revision.
 *
 * Keys stay snake_case: `--json` prints the *validated* envelope, so the
 * decoded object has to be the object that gets serialized, byte for byte.
 */
export const Revision = Schema.Struct({
  turn: RevisionCounter,
  revision: RevisionCounter,
  state_token: OpaqueId,
}).annotations({
  identifier: 'Revision',
  description: 'A v2 state revision: (turn, revision) plus the server state token',
});
/** The three-field state revision. */
export type Revision = typeof Revision.Type;

/**
 * The wire's own name for {@link Revision}: envelopes carry it under the key
 * `state_revision`, and several decoders read better spelling the schema the
 * same way as the field.  One schema, two names — never two definitions.
 */
export const StateRevision: typeof Revision = Revision;
/** The wire's own name for {@link Revision}. */
export type StateRevision = Revision;

/** The three keys `_validate_revision` demands, exactly (`client.py:1305`). */
export const REVISION_FIELDS: ReadonlySet<string> = new Set([
  'turn',
  'revision',
  'state_token',
]);

/**
 * Decode a state revision, tolerantly: the three fields are required and typed
 * exactly as the Python types them, and a field a newer server added is
 * preserved rather than refused.
 */
export const decodeRevision: TolerantDecoder<Revision> = decodeTolerant(Revision, 'Revision');

/**
 * The two counters on their own — the `for name in ("turn", "revision")` loop
 * of `play/client.py:1306-1310`, which reports one sentence for both.
 */
const RevisionCounters = Schema.Struct({
  turn: RevisionCounter,
  revision: RevisionCounter,
}).annotations({ identifier: 'RevisionCounters' });

const decodeRevisionCounters = decodeTolerant(RevisionCounters, 'state revision counters');

const decodeStateToken = decodeTolerant(OpaqueId, 'state token');

/**
 * Decode a state revision the way `_validate_revision` does, closed key set and
 * verbatim refusal sentences included.
 *
 * This is the parity door, not the everyday one.  It refuses an object that
 * gained a field — which is the one thing `@arena/wire` otherwise never does —
 * so use {@link decodeRevision} unless you are diffing this port against
 * `play/client.py` or deliberately running in strict mode.
 *
 * The three sentences it can produce are the Python's, character for
 * character; `play-cli/test/schema.test.ts:89-105` and
 * `play-cli/test/aliases.test.ts:626` assert them:
 *
 * ```text
 * invalid state revision: missing state_token. Expected exactly revision, state_token, turn
 * invalid state revision counters
 * invalid state token
 * ```
 */
export const decodeRevisionExact = (
  value: unknown,
): Either.Either<Revision, WireDecodeError> =>
  Either.gen(function* () {
    const raw = yield* exactFields(value, REVISION_FIELDS, 'state revision');
    const counters = yield* Either.mapLeft(
      decodeRevisionCounters({ turn: raw['turn'], revision: raw['revision'] }),
      (error) => driftError('state revision counters', undefined, error.issues),
    );
    const token = yield* Either.mapLeft(decodeStateToken(raw['state_token']), (error) =>
      driftError('state token', undefined, error.issues),
    );
    return { turn: counters.turn, revision: counters.revision, state_token: token };
  });

/**
 * `(turn, revision)` — the tuple Python compares revisions by
 * (`_revision_order`, `play/client.py:2407-2408`).
 */
export const revisionOrder = (value: Revision): readonly [number, number] => [
  value.turn,
  value.revision,
];

/**
 * Total order over revisions: `-1` when `left` is older than `right`.
 *
 * `state_token` is deliberately not part of the order.  Two revisions with the
 * same `(turn, revision)` and different tokens are the same *position* reported
 * by a server that restarted; ordering them by token would invent a history.
 * {@link revisionsEqual} is the check that notices the difference.
 */
export const compareRevisions = (left: Revision, right: Revision): -1 | 0 | 1 => {
  if (left.turn !== right.turn) return left.turn < right.turn ? -1 : 1;
  if (left.revision !== right.revision) return left.revision < right.revision ? -1 : 1;
  return 0;
};

/** Structural equality, exactly what Python's `dict == dict` did. */
export const revisionsEqual = (left: Revision, right: Revision): boolean =>
  left.turn === right.turn &&
  left.revision === right.revision &&
  left.state_token === right.state_token;

/**
 * The revision as a {@link CanonRecord}, with the two counters spelled
 * `bigint`.
 *
 * A revision is not only compared — it is *hashed*.  `_legacy_catalog_id`
 * (`play/client.py:1416-1423`) builds a catalog id from
 *
 * ```python
 * json.dumps([game_id, agent_id, revision, scope],
 *            sort_keys=True, separators=(",", ":"), ensure_ascii=True)
 * ```
 *
 * and takes SHA-256 over the bytes.  `turn` and `revision` are Python `int`s
 * there, so they must print `5`, not `5.0` — and `../canon.ts` spells a Python
 * `int` as `bigint` precisely so that distinction survives.  Feeding a decoded
 * {@link Revision} straight to `canonicalBytes` would type-error rather than
 * quietly emit floats; this is the conversion that makes it well-typed, and it
 * is the only supported way to canonicalize a revision.
 */
export const revisionCanonRecord = (value: Revision): CanonRecord => ({
  turn: BigInt(value.turn),
  revision: BigInt(value.revision),
  state_token: value.state_token,
});
