/**
 * What the cached catalog can still answer without the wire.
 *
 * Ports `_promoted_catalog_page` (client.py:3030-3051), `_catalog_signature`
 * (3984-4002), `_cached_actor_catalog` (4005-4014), `_catalog_equivalence`
 * (4017-4066), `_cached_descriptors` (7979-7986), `_kind_list` (7988-7990) and
 * `_cached_kind_scopes` (7993-8014).
 *
 * `.v2-state` holds every descriptor promoted at the newest revision this seat
 * knows.  That is enough to answer three questions with zero requests: which
 * descriptors are still live, which *other* actor already offers the kind a
 * failed query went looking for, and whether two actors are offering literally
 * the same menu — the last of which is what lets a stack of identical units
 * print one catalog plus a list of names instead of N copies.
 *
 * The row renderers themselves are U11's.  They arrive through
 * {@link CatalogRenderDeps} so this unit stays in wave 1.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import type { PageScope } from 'src/schema/page';
import { field, isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import type { Revision } from 'src/schema/revision';
import type { LegalActionDescriptor } from 'src/schema/descriptor';
import type { LegalActionPageEnvelope } from 'src/schema/page';
import { idPrefix, isActorType } from 'src/schema/ids';
import { actionTargetKey, aliasMap, jsonEquals, type AliasMap } from 'src/services/aliases';
import type { V2ClientState } from 'src/services/session-store';

/**
 * client.py:7976.  `src/constants.ts` is core-owned and did not carry it; see
 * NOTES.md — move it there when the integrator next touches that file.
 */
export const V2_KIND_LIST_MAX = 24;

// ---------------------------------------------------------------------------
// The U11 seam
// ---------------------------------------------------------------------------

/** The three U11 renderers a cached-catalog comparison is defined in terms of. */
export interface CatalogRenderDeps {
  /** `_compact_legal_action` — the leak-guarded projection rows are read from. */
  readonly compactLegalAction: (descriptor: JsonObject) => Effect.Effect<JsonObject, PlayerError>;
  /** `_action_kind_key` — kind plus operation, with no opaque handle in it. */
  readonly actionKindKey: (compact: JsonObject) => Effect.Effect<string, PlayerError>;
  /** `_legal_row` — the full row; the signature uses cells 1..3. */
  readonly legalRow: (
    alias: string,
    compact: JsonObject,
    scope: PageScope | null,
    aliases: AliasMap | null
  ) => Effect.Effect<ReadonlyArray<string>, PlayerError>;
  /** `_kind_selector_matches` — does this raw descriptor answer to a selector? */
  readonly kindSelectorMatches: (descriptor: JsonObject, selector: string) => boolean;
}

/** The `legal --all` result object `_catalog_equivalence` reads. */
export interface CompactLegalResult {
  readonly state_revision: Revision;
  readonly offset: number;
  readonly truncated: boolean;
  readonly byte_limited: boolean;
  readonly actions: ReadonlyArray<JsonObject>;
}

/** One catalog signed twice: by choice offered, and by rendered row. */
export interface CatalogSignature {
  /** `[kindKey, targetKey]` per action — what "the same options" means. */
  readonly choices: ReadonlyArray<readonly [string, string]>;
  /** The row the agent would read, so cost and legality still differ visibly. */
  readonly rows: ReadonlyArray<ReadonlyArray<string>>;
}

// ---------------------------------------------------------------------------
// _promoted_catalog_page
// ---------------------------------------------------------------------------

/**
 * Return the page the mirror should project for one legal-actions page.
 *
 * While a scoped catalog is still being drained its descriptors carry no `aN`
 * name, because numbering a staged capability would advertise a handle
 * `persistBatchForAction` must refuse.  The pages mirrored during the drain
 * therefore render `-` in the alias column.  When the final page promotes the
 * accumulation the whole catalog is handed to the mirror in one piece, so every
 * one of those rows is rewritten with the name it just earned.  The returned
 * object is still the validated page — only the item list widens, and only to
 * descriptors this same response ingested.
 */
export const promotedCatalogPage = (
  page: LegalActionPageEnvelope,
  promoted: ReadonlyArray<LegalActionDescriptor> | null
): LegalActionPageEnvelope =>
  promoted === null ? page : { ...page, page: { ...page.page, items: promoted } };

// ---------------------------------------------------------------------------
// _cached_descriptors / _cached_actor_catalog
// ---------------------------------------------------------------------------

const revisionValue = (revision: Revision | null): JsonValue =>
  revision === null
    ? null
    : { turn: revision.turn, revision: revision.revision, state_token: revision.state_token };

/** Every raw descriptor this seat still holds at the newest revision. */
export const cachedDescriptors = (state: V2ClientState): ReadonlyArray<JsonObject> => {
  const revision = revisionValue(state.last_revision);
  return Object.values(state.actions).filter((descriptor): descriptor is JsonObject => {
    if (!isJsonObject(descriptor)) return false;
    return jsonEquals(field(descriptor, 'state_revision'), revision);
  });
};

const descriptorActorId = (descriptor: JsonObject): string | null => {
  const subject = field(descriptor, 'subject');
  const actor = isJsonObject(subject) ? field(subject, 'actor') : null;
  const actorId = isJsonObject(actor) ? field(actor, 'id') : null;
  return typeof actorId === 'string' ? actorId : null;
};

/** Every cached descriptor whose subject names this actor. */
export const cachedActorCatalog = (
  state: V2ClientState,
  actorId: string
): ReadonlyArray<JsonObject> =>
  Object.values(state.actions).filter((descriptor): descriptor is JsonObject => {
    if (!isJsonObject(descriptor)) return false;
    return descriptorActorId(descriptor) === actorId;
  });

// ---------------------------------------------------------------------------
// _kind_list / _cached_kind_scopes
// ---------------------------------------------------------------------------

/** A bounded, space-joined kind list with `…` when it was cut short. */
export const kindList = (kinds: ReadonlyArray<string>): string => {
  const shown = kinds.slice(0, V2_KIND_LIST_MAX);
  return shown.join(' ') + (kinds.length > shown.length ? ' …' : '');
};

/**
 * Name the actors whose cached catalog already offers this kind.
 *
 * The scope that was just searched is never named back: a remedy that repeats
 * the command which failed is not a remedy.
 */
export const cachedKindScopes = (
  state: V2ClientState,
  selector: string,
  asked: string,
  kindSelectorMatches: (descriptor: JsonObject, selector: string) => boolean
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.map(aliasMap(state), (aliases) => {
    const found: string[] = [];
    for (const descriptor of cachedDescriptors(state)) {
      if (!kindSelectorMatches(descriptor, selector)) continue;
      const actorId = descriptorActorId(descriptor);
      if (actorId === null || actorId === asked) continue;
      const name = aliases[actorId] ?? actorId;
      if (!found.includes(name)) found.push(name);
    }
    return found;
  });

// ---------------------------------------------------------------------------
// _catalog_signature
// ---------------------------------------------------------------------------

/**
 * Sign one catalog twice: by choice offered, and by rendered row.
 *
 * The first signature is what "the same options" means — kind, operation and
 * target by coordinate, with every revision-bound hash excluded.  The second is
 * the row the agent would read, so any difference in probability, legality,
 * cost or arguments is still visible as a differing row.
 */
export const catalogSignature = (
  compacts: ReadonlyArray<JsonObject>,
  scope: PageScope | null,
  aliases: AliasMap | null,
  deps: CatalogRenderDeps
): Effect.Effect<CatalogSignature, PlayerError> =>
  Effect.gen(function* () {
    const choices: Array<readonly [string, string]> = [];
    const rows: Array<ReadonlyArray<string>> = [];
    for (const compact of compacts) {
      const kindKey = yield* deps.actionKindKey(compact);
      const targetKey = yield* actionTargetKey(field(compact, 'target'));
      choices.push([kindKey, targetKey]);
      const row = yield* deps.legalRow('', compact, scope, aliases);
      rows.push(row.slice(1, 4));
    }
    return { choices, rows };
  });

const sameCells = (left: ReadonlyArray<string>, right: ReadonlyArray<string>): boolean =>
  left.length === right.length && left.every((cell, index) => cell === right[index]);

const sameChoices = (
  left: ReadonlyArray<readonly [string, string]>,
  right: ReadonlyArray<readonly [string, string]>
): boolean =>
  left.length === right.length &&
  left.every((pair, index) => {
    const other = right[index];
    return other !== undefined && pair[0] === other[0] && pair[1] === other[1];
  });

// ---------------------------------------------------------------------------
// _catalog_equivalence
// ---------------------------------------------------------------------------

/** The actor whose catalog matches, plus the rows that still read differently. */
export interface CatalogEquivalence {
  readonly actorId: string;
  readonly differing: ReadonlyArray<JsonObject>;
}

/**
 * Find another actor whose catalog offers exactly the same choices.
 *
 * Only complete catalogs drained at the newest revision this client knows are
 * compared, and the comparison never crosses a revision: two catalogs built
 * from different revisions describe different capabilities even when they read
 * identically.  Row *order* must match too — the short line claims "same
 * options in the same order", which is what makes the named aliases usable
 * without reprinting the rows they stand for.
 */
export const catalogEquivalence = (
  state: V2ClientState,
  result: CompactLegalResult,
  scope: PageScope | null,
  aliases: AliasMap | null,
  deps: CatalogRenderDeps
): Effect.Effect<CatalogEquivalence | null, PlayerError> =>
  Effect.gen(function* () {
    if (scope === null || scope.target_id !== undefined || result.truncated) return null;
    if (result.offset !== 0 || result.byte_limited) return null;
    const actorId = scope.actor_id;
    const drained = state.drained_actors;
    const last = state.last_revision;
    if (
      last === null ||
      !jsonEquals(revisionValue(last), revisionValue(result.state_revision)) ||
      !drained.includes(actorId)
    ) {
      return null;
    }
    const signature = yield* catalogSignature(result.actions, scope, aliases, deps);
    for (const otherValue of drained) {
      if (typeof otherValue !== 'string' || otherValue === actorId) continue;
      const descriptors = cachedActorCatalog(state, otherValue);
      if (descriptors.length !== signature.choices.length) continue;
      const otherType = idPrefix(otherValue);
      if (!isActorType(otherType)) continue;
      const otherScope: PageScope = { actor_id: otherValue, actor_type: otherType };
      const compacts: JsonObject[] = [];
      for (const descriptor of descriptors) {
        compacts.push(yield* deps.compactLegalAction(descriptor));
      }
      const other = yield* catalogSignature(compacts, otherScope, aliases, deps);
      if (!sameChoices(other.choices, signature.choices)) continue;
      const differing = result.actions.filter((_compact, index) => {
        const row = signature.rows[index];
        const otherRow = other.rows[index];
        return row === undefined || otherRow === undefined || !sameCells(row, otherRow);
      });
      return { actorId: otherValue, differing };
    }
    return null;
  });
