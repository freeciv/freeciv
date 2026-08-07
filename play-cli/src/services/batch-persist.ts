/**
 * Writing a `CommandBatch` down before it is sent.
 *
 * Ports `_persist_batch_for_action` (client.py:8423-8471).
 *
 * The whole point of this file is the *order* of two side effects.  The batch —
 * its ID, its revision, its exact canonical bytes — is committed to `.v2-state`
 * **before** the POST leaves, so a process killed anywhere between the write and
 * the response still leaves a record `retry` can resolve by receipt.  A client
 * that minted the ID after the send would, in that window, have issued an order
 * it cannot name.
 *
 * Everything else here is the fail-closed reading of the local catalog: an
 * action ID that is not cached at the newest revision this seat knows is refused
 * with the enumeration command that would make it real, never sent hopefully.
 *
 * `args` is a {@link PyObject}, which every `JsonObject` already is: a caller
 * that has no Python number model (U16's order parser, U18's pregame) passes
 * one unchanged, and `batch`'s own `--arguments` passes a tree whose numbers
 * still know whether they were written as `40` or `40.0` (NOTES.md §17.9).
 */
import { Effect } from 'effect';
import { FULL_CONTROL_V2 } from 'src/constants';
import { playerError, type LockTimeoutError, type PlayerError } from 'src/errors';
import { revisionsEqual, type Revision } from 'src/schema/revision';
import { decodeRevision } from 'src/schema/revision';
import { isJsonObject, type MutableJsonObject } from 'src/schema/primitives';
import { aliasMap } from 'src/services/aliases';
import { canonicalText, type PyObject, type PyValue } from 'src/services/canonical-body';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, type Session, type V2ClientState } from 'src/services/session-store';

// ---------------------------------------------------------------------------
// Batch identifiers
// ---------------------------------------------------------------------------

const TOKEN_BYTES = 24;

/** `secrets.token_urlsafe(24)` — 24 random bytes, base64url, unpadded. */
export const batchToken = (): string => {
  const bytes = crypto.getRandomValues(new Uint8Array(TOKEN_BYTES));
  let binary = '';
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
};

/** A collision is astronomically unlikely; an infinite loop is not a remedy. */
const MAX_TOKEN_ATTEMPTS = 1024;

export interface PersistOptions {
  /** Injected so a test can pin the ID the way CPython's tests patch `secrets`. */
  readonly token?: () => string;
}

// ---------------------------------------------------------------------------
// _persist_batch_for_action
// ---------------------------------------------------------------------------

const UNKNOWN_ACTION =
  'unknown or expired action ID; run the matching `just legal` query';

const stagedScope = (state: V2ClientState, actionId: string): string | null => {
  for (const entry of Object.values(state.pending_catalogs)) {
    if (!isJsonObject(entry)) continue;
    const items = entry['items'];
    if (!isJsonObject(items) || !Object.prototype.hasOwnProperty.call(items, actionId)) {
      continue;
    }
    const scope = entry['scope'];
    const actor = isJsonObject(scope) ? scope['actor_id'] : null;
    return typeof actor === 'string' ? actor : '';
  }
  return null;
};

/**
 * Persist one single-command batch and yield its ID.
 *
 * Refusals, in the order they can happen:
 *
 * 1. The action is *staged* — it came from a catalog page whose drain never
 *    finished.  A staged action is not expired, it is merely not executable
 *    yet, so the sentence says which of the two happened and names the drain
 *    that fixes it.
 * 2. The action is unknown or expired outright.
 * 3. The cached descriptor is from an older revision than this seat now holds,
 *    which means the world moved under it; re-enumerate rather than send a
 *    handle the server has already retired.
 */
export const persistBatchForAction = (
  sessionPath: string,
  session: Session,
  actionId: string,
  args: PyObject,
  options: PersistOptions = {}
): Effect.Effect<string, PlayerError | LockTimeoutError, SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const files = yield* PrivateFs;
    const token = options.token ?? batchToken;
    return yield* store.withStateLock(sessionPath, session, (state) =>
      Effect.gen(function* () {
        const descriptor = state.actions[actionId];
        if (!isJsonObject(descriptor)) {
          const scopeId = stagedScope(state, actionId);
          if (scopeId !== null) {
            const aliases = yield* aliasMap(state);
            const named = aliases[scopeId] ?? scopeId;
            return yield* Effect.fail(
              playerError(
                'unknown or expired action ID: this action came from a catalog ' +
                  'page that is still incomplete, and only a complete catalog is ' +
                  `executable; run \`just legal --actor_id ${named} --all\``
              )
            );
          }
          return yield* Effect.fail(playerError(UNKNOWN_ACTION));
        }
        const revision: Revision = yield* Effect.mapError(
          decodeRevision(descriptor['state_revision'] ?? null),
          (error) => playerError(error.message)
        );
        const last = state.last_revision;
        if (last === null || !revisionsEqual(last, revision)) {
          return yield* Effect.fail(
            playerError('the cached action is not from the latest revision')
          );
        }
        const batchId = yield* Effect.sync(() => {
          let candidate = `batch_${token()}`;
          for (
            let attempt = 0;
            attempt < MAX_TOKEN_ATTEMPTS && candidate in state.batches;
            attempt += 1
          ) {
            candidate = `batch_${token()}`;
          }
          return candidate;
        });
        if (batchId in state.batches) {
          return yield* Effect.fail(playerError('could not mint a fresh command batch ID'));
        }
        const body: PyValue = {
          schema_version: 2,
          control_protocol: FULL_CONTROL_V2,
          game_id: session.gameId,
          agent_id: session.agentId,
          batch_id: batchId,
          state_revision: {
            turn: revision.turn,
            revision: revision.revision,
            state_token: revision.state_token,
          },
          commands: [{ action_id: actionId, arguments: args }],
        };
        const batches: MutableJsonObject = { ...state.batches };
        batches[batchId] = yield* canonicalText(body);
        const next: V2ClientState = { ...state, batches };
        yield* files.writeJson(store.statePath(sessionPath), next);
        return batchId;
      })
    );
  });
