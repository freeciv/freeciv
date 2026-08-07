/**
 * The legal-actions page surface.
 *
 * `_validate_page(..., legal=True)` shares one implementation with the state
 * page, so this module is the named entry point plus the scope vocabulary U11,
 * U15 and U16 read.  Splitting the file keeps the ownership table's promise
 * that `src/schema/legal-page.ts` exists without duplicating the validator.
 */
export {
  decodeLegalPage,
  decodeScope,
  legacyCatalogId,
  type LegalActionPageEnvelope,
  type PageScope,
} from 'src/schema/page';

export { decodeDescriptor, type LegalActionDescriptor } from 'src/schema/descriptor';

import type { PageScope } from 'src/schema/page';

/** Two scopes name the same page when every key matches. */
export const scopesEqual = (
  left: PageScope | null | undefined,
  right: PageScope | null | undefined
): boolean => {
  if (left == null || right == null) return left == null && right == null;
  return (
    left.actor_id === right.actor_id &&
    left.actor_type === right.actor_type &&
    (left.target_id ?? null) === (right.target_id ?? null) &&
    (left.target_type ?? null) === (right.target_type ?? null)
  );
};
