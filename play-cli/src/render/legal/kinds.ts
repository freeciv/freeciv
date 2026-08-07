/**
 * The kind taxonomy the `kind` column prints, and what a bounded window hid.
 *
 * Ports `_action_kind_key` (client.py:3913-3925), `_descriptor_kind_key`
 * (3928-3937), `_kind_selector_matches` (3940-3967) and `_hidden_kind_lines`
 * (4149-4200).
 *
 * `--kind` must accept exactly what the column prints and no more: a bare kind
 * selects every operation under it, and the `kind/operation` form selects the
 * one row it was copied from.  The filter therefore accepts its own output.
 */
import { V2_HIDDEN_KIND_MAX } from 'src/constants';
import { drift } from 'src/render/primitives';
import type { AliasMap } from 'src/render/primitives';
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { field, isJsonObject, type JsonObject } from 'src/schema/primitives';
import type { PageScope } from 'src/schema/page';
import { kindWithOperation } from 'src/render/legal/rows';
import type { CompactAction, LegalCompactResult } from 'src/services/legal-compact';

/** Name one *compacted* action's kind and operation, with no handle in it. */
export const actionKindKey = (compact: CompactAction): Effect.Effect<string, PlayerError> => {
  const subject = field(compact, 'subject');
  if (!isJsonObject(subject)) return Effect.fail(drift('legal action subject'));
  const kind = field(compact, 'kind');
  if (typeof kind !== 'string') return Effect.fail(drift('legal action kind'));
  return Effect.succeed(kindWithOperation(kind, field(subject, 'operation')));
};

/**
 * Name a raw descriptor's kind exactly as the kind column prints it.
 *
 * Total, unlike {@link actionKindKey}: every caller feeds it a descriptor that
 * validation already proved carries a string `kind`, and the empty string it
 * returns for the unreachable case matches no selector, so a drifted cache
 * entry is skipped rather than crashing a remedy sentence.
 */
export const descriptorKindKey = (descriptor: JsonObject): string => {
  const kind = field(descriptor, 'kind');
  if (typeof kind !== 'string') return '';
  const subject = field(descriptor, 'subject');
  return kindWithOperation(kind, isJsonObject(subject) ? field(subject, 'operation') : null);
};

/**
 * Report whether one descriptor answers to a printed kind selector.
 *
 * `unit.sentry` prints without a suffix because its operation is already the
 * kind's tail; `unit.sentry/sentry` is still the same row.
 */
export const kindSelectorMatches = (descriptor: JsonObject, selector: string): boolean => {
  const slash = selector.indexOf('/');
  const kind = slash === -1 ? selector : selector.slice(0, slash);
  const operation = slash === -1 ? '' : selector.slice(slash + 1);
  if (field(descriptor, 'kind') !== kind) return false;
  if (operation === '') return true;
  const subject = field(descriptor, 'subject');
  const found = isJsonObject(subject) ? field(subject, 'operation') : null;
  if (found === operation) return true;
  const tail = kind.includes('.') ? kind.slice(kind.indexOf('.') + 1) : kind;
  return found === null && tail === operation;
};

/**
 * Name, by kind, what a bounded window matched but did not print.
 *
 * A window that stops at the byte cap is silent about the tail, and silence
 * reads as absence: the government controls sort last in this seat's largest
 * catalog, so an agent that greps a truncated `--actor_id … --all` for
 * `government` concludes the rules forbid a revolution.  The header's `more:`
 * hint is one line at the top and does not survive a pipe.  This line prints
 * below the rows, names the withheld kinds in full, and carries the command
 * that prints them.
 */
export const hiddenKindLines = (
  result: LegalCompactResult,
  scope: PageScope | null,
  aliases: AliasMap | null
): ReadonlyArray<string> => {
  const hidden = Object.entries(result.hidden_kinds);
  if (hidden.length === 0) return [];
  const shown = hidden
    .slice(0, V2_HIDDEN_KIND_MAX)
    .map(([name, count]) => `${name} (${count})`);
  if (hidden.length > V2_HIDDEN_KIND_MAX) shown.push('…');
  const lookup = aliases ?? {};
  const named = scope === null ? '' : (lookup[scope.actor_id] ?? scope.actor_id);
  // A relation window is a two-parameter query; naming only the actor would
  // print a command that enumerates a different catalog than the one that
  // withheld these rows.
  const target = scope?.target_id ?? '';
  const kind = result.kind;
  const command =
    'just legal' +
    (kind === null ? '' : ` --kind ${kind}`) +
    (named === '' ? '' : ` --actor_id ${named}`) +
    (target === '' ? '' : ` --target_id ${lookup[target] ?? target}`) +
    ' --all';
  if (result.has_more) {
    return [`not shown: ${shown.join(' ')} — ${command} --offset ${result.next_offset}`];
  }
  // Nothing follows this window, so what it withheld sits before it: the
  // remedy is the same query without the offset, never a later one.
  return [`not shown (earlier in this catalog): ${shown.join(' ')} — ${command}`];
};
