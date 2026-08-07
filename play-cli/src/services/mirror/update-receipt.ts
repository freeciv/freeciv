/**
 * `update_from_receipt` — record one validated command receipt in the digest.
 *
 * Ports `play/state_mirror.py:1471-1514`.
 *
 * A receipt changes the world without carrying the new world with it, so the
 * only honest projection is prose: what state the batch reached, at which
 * revision, and — when the tables beside it are older than that revision — the
 * sentence that says the tables are now behind and names the command that
 * refreshes them.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject } from 'src/schema/primitives';
import type { PrivateFs } from 'src/services/private-fs';
import {
  OVERVIEW_FILE,
  cell,
  dig,
  isNewer,
  mirrorDir,
  mirrorGuard,
  readMirror,
  revisionPair,
} from 'src/services/mirror/store';
import { parseTable } from 'src/services/mirror/table';
import { updateDelta } from 'src/services/mirror/delta';

/** The refusal or observation a receipt carried, appended to its digest line. */
const detail = (receipt: unknown): string => {
  const error = dig(receipt, 'error');
  const inner = isJsonObject(error) ? error['error'] : undefined;
  const refusal = isJsonObject(inner)
    ? `: ${cell(inner['code'])} - ${cell(inner['message'])}`
    : '';
  const observation = dig(receipt, 'observation');
  const city = isJsonObject(observation) ? observation['city'] : undefined;
  const investigated = isJsonObject(city)
    ? `: investigated ${cell(city['name'])} sz${cell(city['size'])} building ${cell(
        dig(city, 'production', 'name')
      )}`
    : '';
  return `${refusal}${investigated}`;
};

/**
 * `update_from_receipt` — append one receipt line to `state/delta.md`.
 *
 * Returns the digest path when the digest moved, and nothing when it did not:
 * a receipt at a revision the digest already holds says nothing new.
 */
export const updateFromReceipt = (
  dir: string,
  command: string,
  receipt: unknown
): Effect.Effect<ReadonlyArray<string>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const revision = yield* revisionPair(dig(receipt, 'state_revision'));
    const state = cell(dig(receipt, 'receipt_state'));
    const batch = cell(dig(receipt, 'batch_id'));
    const replay = dig(receipt, 'idempotent') === true ? ' (idempotent replay)' : '';
    const entry = `${state} batch ${batch.slice(0, 16)} at rev ${revision.revision}${replay}${detail(
      receipt
    )}`;
    const table = parseTable(yield* readMirror(dir, OVERVIEW_FILE));
    const lag =
      table.revision !== null && isNewer(revision, table.revision)
        ? [
            `state files still show rev ${table.revision.revision}; ` +
              'run `just turn` to refresh them',
          ]
        : [];
    const delta = yield* updateDelta(dir, revision, command, 'receipts', [entry, ...lag]);
    return delta === null ? [] : [delta];
  });

/**
 * `_mirror_receipt` (client.py:3054-3060) — the client-side bridge: record one
 * receipt for a session, and never fail the command that produced it.
 */
export const mirrorReceipt = (
  sessionPath: string,
  receipt: unknown,
  command = 'batch'
): Effect.Effect<void, never, PrivateFs> =>
  mirrorGuard(
    Effect.flatMap(mirrorDir(sessionPath), (dir) => updateFromReceipt(dir, command, receipt))
  );
