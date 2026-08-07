/**
 * "This catalog is that one" instead of reprinting it.
 *
 * Ports `_alias_span` (client.py:4203-4214) and `_equivalence_lines`
 * (4217-4248).
 *
 * The claim is only ever made about two complete catalogs read at the same
 * revision whose rows line up one for one, so naming this catalog's own action
 * aliases is enough to act on any of them.  Every row whose decision-relevant
 * detail differs is still printed in full underneath.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { ACTION_ALIAS_RE } from 'src/constants';
import type { AliasMap } from 'src/render/primitives';
import { field, type JsonObject } from 'src/schema/primitives';
import type { PageScope } from 'src/schema/page';
import type { CatalogEquivalence } from 'src/services/catalog-cache';
import type { LegalCompactResult } from 'src/services/legal-compact';
import { legalRows } from 'src/render/legal/rows';

/** Name a run of action aliases as `a3..a7`, listing anything ragged. */
export const aliasSpan = (aliases: ReadonlyArray<string>): string => {
  const numbers: number[] = [];
  for (const alias of aliases) {
    if (!ACTION_ALIAS_RE.test(alias)) return aliases.join(' ');
    numbers.push(Number.parseInt(alias.slice(1), 10));
  }
  const first = numbers[0];
  const consecutive =
    numbers.length > 1 &&
    first !== undefined &&
    numbers.every((value, index) => value === first + index);
  return consecutive ? `${aliases[0]}..${aliases[aliases.length - 1]}` : aliases.join(' ');
};

/** The short line, plus every row that still reads differently. */
export const equivalenceLines = (
  result: LegalCompactResult,
  scope: PageScope | null,
  aliases: AliasMap | null,
  equivalence: CatalogEquivalence
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    const lookup = aliases ?? {};
    const actorId = scope === null ? '' : scope.actor_id;
    const otherId = equivalence.actorId;
    const differing: ReadonlyArray<JsonObject> = equivalence.differing;
    let line =
      `${lookup[actorId] ?? actorId} == ${lookup[otherId] ?? otherId} ` +
      `(rev${result.state_revision.revision})`;
    const own = result.actions.map((compact) => {
      const actionId = field(compact, 'action_id');
      const identifier = typeof actionId === 'string' ? actionId : '';
      return lookup[identifier] ?? identifier;
    });
    if (own.length > 0) line += ` ${aliasSpan(own)}`;
    if (differing.length > 0) {
      line += ` except ${differing.length} row${differing.length === 1 ? '' : 's'}`;
    }
    const lines = [line];
    if (differing.length > 0) lines.push(...(yield* legalRows(differing, scope, aliases)));
    return lines;
  });
