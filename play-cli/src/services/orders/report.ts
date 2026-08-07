/**
 * The refusal an unresolved batch prints — the agent's primary recovery surface.
 *
 * Ports `play/client.py` 9191-9198 (`_order_enumeration_command`) and 9201-9233
 * (`_unresolved_report`).
 *
 * Every line here exists to turn one refusal into one next command.  The
 * per-order table says which orders *did* resolve, so a partially wrong line is
 * repaired rather than rewritten; each unresolved order contributes the exact
 * enumeration command that would make it resolvable, in the alias dialect the
 * agent already types, deduplicated in first-seen order.
 *
 * The one case that suppresses the remedies is a phase that is already over:
 * an order refused while this seat's own phase is closed is not a cache miss,
 * and sending the agent to re-enumerate would only produce a fresh catalog it
 * still cannot act on.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { revisionLabel } from 'src/render/primitives';
import { aliasMap } from 'src/services/aliases';
import { aliasRefreshCommand } from 'src/services/alias-expand';
import { cachedPhaseNote } from 'src/services/mirror/store';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, type V2ClientState } from 'src/services/session-store';
import type { OrderOutcome } from 'src/services/orders/match';

// ---------------------------------------------------------------------------
// _order_enumeration_command
// ---------------------------------------------------------------------------

/**
 * Name the drain that would make this order resolvable, in the agent's own
 * alias dialect when one is already assigned.
 */
export const orderEnumerationCommand = (
  sessionPath: string,
  state: V2ClientState,
  actorId: string
): Effect.Effect<string, PlayerError, SessionStore> =>
  Effect.gen(function* () {
    if (actorId === '') return yield* aliasRefreshCommand(sessionPath, '');
    const aliases = yield* aliasMap(state);
    // CPython's `aliases.get(actor_id, actor_id)`: a server-supplied ID that
    // happens to spell an inherited name must fall back to the raw ID, not to
    // whatever `Object.prototype` carries under that name.
    const alias = Object.hasOwn(aliases, actorId) ? aliases[actorId] : undefined;
    return yield* aliasRefreshCommand(sessionPath, alias ?? actorId);
  });

// ---------------------------------------------------------------------------
// _unresolved_report
// ---------------------------------------------------------------------------

/** Say which orders resolved, which did not, and what to run next. */
export const unresolvedReport = (
  sessionPath: string,
  state: V2ClientState,
  outcomes: ReadonlyArray<OrderOutcome>
): Effect.Effect<ReadonlyArray<string>, PlayerError, SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    const revision = state.last_revision;
    const label = revision === null ? 'no revision' : revisionLabel(revision);
    const failed = outcomes.filter((outcome) => outcome.resolved === null).length;
    const lines: string[] = [
      `${failed} of ${outcomes.length} orders did not resolve against the ` +
        `cached ${label} catalog; nothing was sent`,
    ];
    const remedies: string[] = [];
    for (const [index, outcome] of outcomes.entries()) {
      const position = index + 1;
      if (outcome.resolved !== null) {
        lines.push(
          `  ${position} resolved    ${outcome.text}  ->  ${outcome.resolved.kind} ` +
            `${outcome.resolved.label}`
        );
        continue;
      }
      lines.push(`  ${position} unresolved  ${outcome.text}  --  ${outcome.reason}`);
      const remedy = yield* orderEnumerationCommand(sessionPath, state, outcome.actor);
      if (!remedies.includes(remedy)) remedies.push(remedy);
    }
    const stalled = yield* cachedPhaseNote(sessionPath);
    if (stalled !== '') return [stalled, ...lines];
    return [...lines, ...remedies.map((remedy) => `enumerate with: ${remedy}`)];
  });
