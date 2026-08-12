/**
 * The **legal-actions page**, and the descriptors it carries.
 *
 * Clean-room Effect Schema re-expression of `_validate_page(..., legal=True)`
 * and `_validate_descriptor` (`play/client.py:1426-1556` and `:1369-1401`),
 * read a second time through `play-cli/src/schema/legal-page.ts` and
 * `play-cli/src/schema/descriptor.ts`.  Nothing here imports from `play-cli`.
 *
 * The Python has one page validator and a `legal` flag.  The rules both
 * flavours share live in `./page.ts`; this module supplies only the three
 * things the flag switches:
 *
 * - the section is the literal `"legal_actions"`, not a member of
 *   `V2_SECTIONS` (`:1454-1456`),
 * - items are {@link LegalActionDescriptor}s rather than opaque JSON (`:1517`),
 * - a `scope` is *permitted* here (`:1520-1521`), which is in turn what
 *   unlocks the catalog metadata.
 *
 * ## Where the descriptor lives
 *
 * In `./descriptor.ts`, which owns the struct and the standalone
 * revision-bound decode; this module imports it and re-exports it, the way
 * `play-cli/src/schema/legal-page.ts` re-exports its own.  A second
 * declaration here would give `@arena/wire` two `LegalActionDescriptor` types
 * that are structurally equal and nominally distinct — the worst of both.
 *
 * ## The revision agreement
 *
 * `_validate_descriptor` compares each descriptor's own `state_revision`
 * against the page's and refuses the whole page when they differ (`:1388`).  A
 * descriptor is a capability minted against one exact state; a page that mixed
 * revisions would invite an agent to spend one that is already stale, so the
 * page is refused rather than the item dropped.  All three fields are
 * compared, `state_token` included — the same counters with a fresh token is a
 * republished state, not the same one.
 *
 * ## The catalog a page belongs to
 *
 * A scoped page is one window onto one actor's catalog.  `catalog_id` names
 * that catalog and `catalog_complete` says whether this window closed it, so a
 * cache can stage a prefix and refuse to execute from it until the terminal
 * page arrives.  An older supervisor sends neither and the client infers an
 * identity instead: see `legacyCatalogSeed` in `./page.ts`, and
 * {@link scopesEqual} for the comparison that decides whether two pages
 * describe the same catalog.
 *
 * @module
 */

import { Schema } from 'effect';
import {
  decodeTolerant,
  encodeTolerant,
  type TolerantDecoder,
  type TolerantEncoder,
} from '../tolerant.ts';
import { LegalActionDescriptor } from './descriptor.ts';
import {
  pageBodyCatalogFields,
  pageBodyIssues,
  pageBodyPaginationFields,
  V2_PAGE_MAX_ITEMS,
  v2PageHeaderFields,
  type PageScope,
} from './page.ts';
import { sessionMismatch, type V2Session } from './protocol.ts';
import { revisionsEqual, StateRevision, type Revision } from './revision.ts';

/**
 * The descriptor vocabulary, re-exported the way
 * `play-cli/src/schema/legal-page.ts` re-exports its own: a caller that has a
 * page has descriptors, and should not have to name a second module to decode
 * one on its own.  Aliases, not copies — `./descriptor.ts` owns the schema.
 */
export {
  decodeDescriptorAt,
  decodeLegalActionDescriptor,
  descriptorLabelText,
  LegalActionDescriptor,
  legalActionDescriptorAt,
  type LegalActionDescriptorEncoded,
} from './descriptor.ts';

// ---------------------------------------------------------------------------
// The page — play/client.py:1426-1556 with legal=True
// ---------------------------------------------------------------------------

/** `section != "legal_actions"` refuses (`play/client.py:1454-1456`). */
export const LEGAL_ACTIONS_SECTION = 'legal_actions' as const;

/** The section literal a legal-actions page always carries. */
export const LegalActionsSection = Schema.Literal(LEGAL_ACTIONS_SECTION).annotations({
  identifier: 'LegalActionsSection',
  description: 'The one section name a legal-actions page may carry',
});
/** The one value {@link LegalActionsSection} admits. */
export type LegalActionsSection = typeof LegalActionsSection.Type;

/**
 * The body of a legal-actions page.
 *
 * Same pagination and catalog rules as a state page — they are literally the
 * same function — with a fixed section and validated items.
 */
export const LegalActionPageBody = Schema.Struct({
  section: LegalActionsSection,
  items: Schema.Array(LegalActionDescriptor).pipe(Schema.maxItems(V2_PAGE_MAX_ITEMS)),
  ...pageBodyPaginationFields,
  ...pageBodyCatalogFields,
})
  .pipe(Schema.filter((body) => pageBodyIssues(body, true)))
  .annotations({
    identifier: 'LegalActionPageBody',
    description: "One page of one actor's legal actions (client.py _validate_page, legal=True)",
  });
/** A decoded legal-actions page body. */
export type LegalActionPageBody = typeof LegalActionPageBody.Type;

/**
 * `_validate_descriptor`'s revision comparison (`play/client.py:1388`), lifted
 * to the envelope so the offending item can be named by index — which the
 * Python's one fixed sentence could not do.
 */
const revisionAgreementIssues = (envelope: {
  readonly state_revision: Revision;
  readonly page: { readonly items: ReadonlyArray<LegalActionDescriptor> };
}): ReadonlyArray<Schema.FilterIssue> =>
  envelope.page.items.flatMap((item, index) =>
    revisionsEqual(item.state_revision, envelope.state_revision)
      ? []
      : [
          {
            path: ['page', 'items', index, 'state_revision'],
            message: 'descriptor was minted at a different state revision than its page',
          },
        ],
  );

/** A legal-actions page: the window an agent chooses its next move from. */
export const LegalActionPageEnvelope = Schema.Struct({
  ...v2PageHeaderFields,
  state_revision: StateRevision,
  page: LegalActionPageBody,
})
  // Wrapped rather than passed by name: `Schema.filter` infers the schema it
  // refines from its predicate's parameter, and a pre-annotated function would
  // widen that inference to `Schema.Any`.
  .pipe(Schema.filter((envelope) => revisionAgreementIssues(envelope)))
  .annotations({
    identifier: 'LegalActionPageEnvelope',
    description: 'A full-control-v2 legal-actions page (client.py _validate_page, legal=True)',
  });
/** A decoded legal-actions page envelope. */
export type LegalActionPageEnvelope = typeof LegalActionPageEnvelope.Type;
/** The wire form of a {@link LegalActionPageEnvelope}. */
export type LegalActionPageEnvelopeEncoded = typeof LegalActionPageEnvelope.Encoded;

/** Decode a legal-actions page, without checking which session it belongs to. */
export const decodeLegalPage: TolerantDecoder<LegalActionPageEnvelope> = decodeTolerant(
  LegalActionPageEnvelope,
  'LegalActionPageEnvelope',
);

/** Re-encode a legal-actions page, unknown server fields included. */
export const encodeLegalPage: TolerantEncoder<
  LegalActionPageEnvelope,
  LegalActionPageEnvelopeEncoded
> = encodeTolerant(LegalActionPageEnvelope, 'LegalActionPageEnvelope');

/** A legal-actions page bound to one session (`play/client.py:1321-1326`). */
export const legalPageFor = (
  session: V2Session,
): Schema.Schema<LegalActionPageEnvelope, LegalActionPageEnvelopeEncoded> =>
  LegalActionPageEnvelope.pipe(
    Schema.filter((envelope) => sessionMismatch(session, envelope)),
  ).annotations({ identifier: 'LegalActionPageEnvelope' });

/** Decode a legal-actions page that must belong to `session`. */
export const decodeLegalPageFor = (
  session: V2Session,
): TolerantDecoder<LegalActionPageEnvelope> =>
  decodeTolerant(legalPageFor(session), 'LegalActionPageEnvelope');

// ---------------------------------------------------------------------------
// Scope identity
// ---------------------------------------------------------------------------

/**
 * Two scopes name the same catalog when every protocol key matches.
 *
 * An absent target and a `null` target are the same thing — a scope that names
 * no target — so the comparison normalizes before comparing rather than
 * reading `undefined !== null` as a difference.  Keys carried through by
 * tolerance are ignored: the four keys the protocol defines are the identity,
 * and a supervisor that adds a fifth must not split one cached catalog in two.
 */
export const scopesEqual = (
  left: PageScope | null | undefined,
  right: PageScope | null | undefined,
): boolean => {
  if (left === null || left === undefined || right === null || right === undefined) {
    return (left ?? null) === null && (right ?? null) === null;
  }
  return (
    left.actor_id === right.actor_id &&
    left.actor_type === right.actor_type &&
    (left.target_id ?? null) === (right.target_id ?? null) &&
    (left.target_type ?? null) === (right.target_type ?? null)
  );
};
