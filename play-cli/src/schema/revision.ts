/**
 * State revisions.
 *
 * Ports `_validate_revision` (client.py:1303-1315) and `_revision_order`
 * (client.py:2408-2409).
 */
import { Effect } from 'effect';
import { DriftError, invalid } from 'src/errors';
import { exact, field, isWholeNumber, opaque } from 'src/schema/primitives';

export interface Revision {
  readonly turn: number;
  readonly revision: number;
  readonly state_token: string;
}

const REVISION_FIELDS: ReadonlySet<string> = new Set(['turn', 'revision', 'state_token']);

export const decodeRevision = (value: unknown): Effect.Effect<Revision, DriftError> =>
  Effect.gen(function* () {
    const raw = yield* exact(value, REVISION_FIELDS, 'state revision');
    const turn = field(raw, 'turn');
    const revision = field(raw, 'revision');
    if (!isWholeNumber(turn) || turn < 0 || !isWholeNumber(revision) || revision < 0) {
      return yield* Effect.fail(invalid('state revision counters'));
    }
    const token = yield* opaque(field(raw, 'state_token'), 'state token');
    return { turn, revision, state_token: token };
  });

/** `(turn, revision)` — the tuple Python compares revisions by. */
export const revisionOrder = (value: Revision): readonly [number, number] => [
  value.turn,
  value.revision,
];

/** Total order over revisions: `-1` when `left` is older than `right`. */
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
