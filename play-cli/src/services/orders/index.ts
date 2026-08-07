/**
 * U15 — the order resolution engine.
 *
 * `just do "u1 found_city London"` turns human-ish words into concrete
 * `CommandBatch` entries against nothing but this seat's own cached catalog.
 * Import from this module, not from its files.
 *
 * The three names PORT_MAP §1 froze for U16 are `parseOrders`, `resolveOrders`
 * and `unresolvedReport`; everything else is exported because `do` (U16),
 * `turn --end` (U12) and `start` (U18) each need one piece of the matcher.
 */
export {
  OrderUnresolved,
  ORDER_COORDINATE_RE,
  V2_ACTION_FAMILIES,
  V2_MAX_ORDERS,
  V2_MAX_ORDER_WORDS,
  V2_TIER1_VERBS,
  casefold,
  orderUnresolved,
  parseOrders,
  parseOrdersEither,
  parseOrdersMessage,
  pyRepr,
  pySplit,
  type ParseOrdersFailure,
  type Tier1Verb,
} from 'src/services/orders/parse';

export {
  LEGAL_SUBJECT_RESERVED,
  compactText,
  kindHead,
  kindTail,
  orderActor,
  orderDiscriminators,
  orderOperation,
  orderPool,
  orderResolution,
  orderTargetKeys,
  orderVerbs,
  type OrderOutcome,
  type OrdersDeps,
  type ResolvedOrder,
} from 'src/services/orders/match';

export {
  ORDER_BAD,
  defaultArguments,
  isArrayProperty,
  namedTargetId,
  orderArguments,
  orderArrayBounds,
  orderElementResolver,
  orderMatch,
  orderProperties,
  orderValue,
  poolIsListed,
  type ElementResolver,
  type OrderProperties,
  type OrderValue,
} from 'src/services/orders/arguments';

export {
  orderFetchTargets,
  orderOutcomes,
  resolveOrder,
  resolveOrders,
  resolveOrdersFetching,
  type OrderOutcomes,
  type OrdersFetchDeps,
} from 'src/services/orders/resolve';

export {
  drainLegalUnlocked,
  rebindOrder,
  refreshOrders,
  refreshStaleOrderAliases,
  type LegalPageReader,
  type RefreshedOrderAliases,
} from 'src/services/orders/rebind';

export { orderEnumerationCommand, unresolvedReport } from 'src/services/orders/report';
