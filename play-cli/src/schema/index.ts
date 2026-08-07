/**
 * The wire schema layer's public surface.
 *
 * Workers import from `src/schema/index` and nowhere else inside `schema/`, so
 * a decoder can move between files without touching a unit.
 *
 * HARD RULE (PLAN.md "Known traps"): decoding is permissive about *values* and
 * closed about *keys* only where the Python already was.  Never add a new
 * exact-field check on a server response; widen the expected key set before
 * calling `exact` instead, the way every optional-if-present field below does.
 */

export {
  decodeEvaluationContext,
  exact,
  field,
  hasField,
  isFiniteNumber,
  isJsonArray,
  isJsonObject,
  isNonEmptyString,
  isWholeNumber,
  jsonObject,
  jsonValue,
  opaque,
  safeNumber,
  sortedNames,
  type EvaluationContext,
  type EvaluationOptions,
  type JsonArray,
  type JsonObject,
  type JsonValue,
  type MutableJsonObject,
  type SafeNumberOptions,
  type SessionIdentity,
} from 'src/schema/primitives';

export {
  ACTOR_TYPES,
  entityAliasIdMatches,
  idPrefix,
  isActorId,
  isActorType,
  isCatalogId,
  isCityId,
  isControllerName,
  isCursor,
  isGameId,
  isOpaqueId,
  isRelationId,
  isTileId,
  type ActorType,
} from 'src/schema/ids';

export {
  compareRevisions,
  decodeRevision,
  revisionOrder,
  revisionsEqual,
  type Revision,
} from 'src/schema/revision';

export {
  decodeError,
  decodeV2Header,
  v2ErrorMessage,
  type StructuredError,
  type StructuredErrorBody,
} from 'src/schema/error';

export { decodeDescriptor, type LegalActionDescriptor } from 'src/schema/descriptor';

export {
  cursorExpiry,
  cursorExpiryMillis,
  decodeLegalPage,
  decodePage,
  decodeScope,
  legacyCatalogId,
  type LegalActionPageEnvelope,
  type PageBody,
  type PageEnvelope,
  type PageEnvelopeOf,
  type PageScope,
} from 'src/schema/page';

export { scopesEqual } from 'src/schema/legal-page';

export {
  decodeInvestigation,
  decodeReceipt,
  type CityFeeling,
  type CityInvestigationObservation,
  type CommandReceipt,
  type ReceiptOptions,
  type ReceiptState,
} from 'src/schema/receipt';

export {
  decodeHealth,
  decodePhaseEndEvent,
  decodeRecoveryEvent,
  type AutoEnd,
  type HealthEnvelope,
  type HealthSeat,
  type PhaseBlock,
  type PhaseEndEvent,
  type PhaseTiming,
  type PriorEnd,
  type RecoveryEvent,
  type WaitingOn,
  type WaitingOnSeat,
} from 'src/schema/health';

export {
  decodeWait,
  type WaitContract,
  type WaitEnvelope,
  type WaitUntil,
  type WakeReason,
} from 'src/schema/wait';

export {
  decodeBatchDisposition,
  decodeCommand,
  decodeCommandBatch,
  type BatchDisposition,
  type Command,
  type CommandBatch,
  type Disposition,
} from 'src/schema/batch';
