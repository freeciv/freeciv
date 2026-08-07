/**
 * The two `legal` renderings: one server page, and one drained catalog.
 *
 * Ports `_render_legal_page` (client.py:4069-4090) and `_render_legal_compact`
 * (4093-4146).
 *
 * Both lead with one envelope line — revision, scope, and how much of the
 * catalog this is — and never repeat any of it inside the body.  Which body
 * they print is a three-way choice: the equivalence claim when another actor
 * already offers this exact menu, the grouped global catalog when nothing
 * narrowed the query, and the flat rows otherwise.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import {
  pageStatus,
  revisionLabel,
  scopeText,
  type AliasMap,
} from 'src/render/primitives';
import type { PageScope, LegalActionPageEnvelope } from 'src/schema/page';
import type { CatalogEquivalence, CatalogRenderDeps } from 'src/services/catalog-cache';
import {
  compactLegalAction,
  descriptorToJson,
  type CompactAction,
  type LegalCompactResult,
} from 'src/services/legal-compact';
import { legalRow, legalRows } from 'src/render/legal/rows';
import { actionKindKey, hiddenKindLines, kindSelectorMatches } from 'src/render/legal/kinds';
import { groupedLegalLines } from 'src/render/legal/grouped';
import { equivalenceLines } from 'src/render/legal/equivalence';

/**
 * The four renderers U03's cached-catalog comparison is defined in terms of.
 *
 * `catalog-cache.ts` is wave 1 and cannot import a wave-2 renderer, so it takes
 * them as a parameter; this is the live bundle every caller passes.
 */
export const catalogRenderDeps: CatalogRenderDeps = {
  compactLegalAction,
  actionKindKey,
  legalRow,
  kindSelectorMatches,
};

const compactAll = (
  page: LegalActionPageEnvelope
): Effect.Effect<ReadonlyArray<CompactAction>, PlayerError> =>
  Effect.forEach(page.page.items, (item) => compactLegalAction(descriptorToJson(item)));

/** One validated legal-actions page as text. */
export const renderLegalPage = (
  value: LegalActionPageEnvelope,
  aliases: AliasMap | null = null,
  options: { readonly full?: boolean } = {}
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    const page = value.page;
    const scope = page.scope ?? null;
    const lines = [
      `${revisionLabel(value.state_revision)} legal ` +
        `${scopeText(scope, aliases ?? undefined)} ${pageStatus(page)}`,
    ];
    if (page.items.length === 0) {
      lines.push('(no legal actions on this page)');
      return lines;
    }
    const compacts = yield* compactAll(value);
    if (scope === null && options.full !== true) {
      lines.push(...(yield* groupedLegalLines(compacts, aliases)));
      return lines;
    }
    lines.push(...(yield* legalRows(compacts, scope, aliases)));
    return lines;
  });

/** One drained catalog as text. */
export const renderLegalCompact = (
  result: LegalCompactResult,
  scope: PageScope | null,
  aliases: AliasMap | null = null,
  equivalence: CatalogEquivalence | null = null,
  options: { readonly full?: boolean } = {}
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    const kind = result.kind;
    let header =
      `${revisionLabel(result.state_revision)} legal ` +
      (kind === null ? '' : `kind=${kind} `) +
      `${scopeText(scope, aliases ?? undefined)} ` +
      `${result.shown}/${result.matched} matched ` +
      `(catalog ${result.catalog_total} complete, pages ${result.pages_read})`;
    if (result.has_more) {
      // The continuation is printed as the command that runs, not as a bare
      // flag fragment: an error or a limit always names its own remedy.
      const lookup = aliases ?? {};
      const named = scope === null ? '' : (lookup[scope.actor_id] ?? scope.actor_id);
      header +=
        ' more: just legal' +
        (kind === null ? '' : ` --kind ${kind}`) +
        (named === '' ? '' : ` --actor_id ${named}`) +
        ` --all --offset ${result.next_offset}`;
    }
    if (result.byte_limited) header += ' byte_limited';
    if (result.oversized_single) header += ' oversized_single';
    const lines = [header];
    if (result.actions.length === 0) {
      // A kind that matched nothing never reaches a renderer — it refuses,
      // naming the kinds that do exist — so an empty window here is either an
      // empty scope or an offset past the end, and it says which.
      lines.push(
        result.matched > 0
          ? `(offset ${result.offset} is past the ${result.matched} matched actions; drop --offset)`
          : '(no legal actions in this catalog)'
      );
      return lines;
    }
    if (equivalence !== null) {
      lines.push(...(yield* equivalenceLines(result, scope, aliases, equivalence)));
    } else if (scope === null && kind === null && options.full !== true) {
      lines.push(...(yield* groupedLegalLines(result.actions, aliases)));
    } else {
      lines.push(...(yield* legalRows(result.actions, scope, aliases)));
    }
    lines.push(...hiddenKindLines(result, scope, aliases));
    return lines;
  });
