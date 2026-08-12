/**
 * `@arena/wire` — the packet schemas and codecs every arena package shares.
 *
 * Consumers import this file and nothing under `src/`.  It depends on `effect`
 * and on nothing else: no platform APIs, no `play-cli`, no filesystem.  Give it
 * a JSON value and it will tell you what the value is; hand it back and it will
 * reproduce the bytes CPython wrote.
 *
 * ## Two shapes of export, and why
 *
 * The **substrate is flat.**  `canonicalBytes`, `decodeTolerant`, `JsonValue`,
 * `WireInt`, `GameId`, `RunState` and their neighbours are the vocabulary both
 * halves of the protocol are written in, they collide with nothing, and
 * qualifying them would only add noise:
 *
 * ```ts
 * import { canonicalBytes, CANON_UTF8, decodeGameId } from '@arena/wire';
 * ```
 *
 * The **two protocol families are namespaces.**  `Gateway` is what a spectator
 * reads (`replay_gateway.py` and the four on-disk run files); `Agent` is what a
 * seat says to the supervisor (`play/client.py`'s `full-control-v2`):
 *
 * ```ts
 * import { Agent, Gateway } from '@arena/wire';
 * const manifest = Gateway.decodeManifest(document);
 * const receipt = Agent.decodeReceipt(envelope);
 * ```
 *
 * The namespaces are load-bearing, not cosmetic.  The two families name some of
 * the same concepts differently on purpose — `Gateway.GameId` is a different
 * regex from `Agent.PlayGameId` — and in one case name the same concept the
 * same way with a *different meaning*: a decoded `Gateway` integer is a
 * `bigint` (a Python `int`, which is what `canonicalText` needs to spell it
 * without a `.0`), while a decoded `Agent` integer is a `number` (a counter the
 * port does arithmetic on, converted at the hashing boundary).  Flattening the
 * two would have silently picked one.  `./numeric.ts` is the long version.
 *
 * That `Gateway` rule is a claim this file used to make and two of the six
 * gateway modules did not keep: `identity.ts` and `archive.ts` reached for
 * `Schema.Int`, which decodes to a JS `number`, so re-canonicalizing a decoded
 * `/health` produced `"pid":77917.0` where CPython wrote `"pid":77917`.  It is
 * true now, and `test/schema-shape.test.ts` fails if it stops being true —
 * a rule the barrel states categorically should not rest on everyone
 * remembering it.  The exceptions are named there: a query parameter and a
 * frame index are not fields in a canonicalized body.
 *
 * ## The rule underneath all of it
 *
 * `./canon.ts` is the foundation: every identity in this protocol is a hash
 * over the *bytes* of a `json.dumps(...)`, so a schema here is not finished
 * when it decodes — it is finished when re-encoding reproduces the bytes.  That
 * is why `bigint` means a Python `int`, why `Schema.optional(Schema.NullOr(x))`
 * appears everywhere instead of the shorter `Schema.optionalWith`, and why the
 * fixtures are raw Python output rather than anything this package wrote.
 *
 * @module
 */

export {
  CANON_ASCII,
  CANON_MAX_DEPTH,
  CANON_UTF8,
  type CanonError,
  canonicalBytes,
  canonicalText,
  type CanonOptions,
  type CanonPath,
  type CanonPathSegment,
  type CanonRecord,
  type CanonValue,
  compareCodePoints,
  encodeString,
  encodeUtf8,
  findLoneSurrogate,
  formatCanonPath,
  formatFloat,
  LoneSurrogateError,
  MaxDepthExceededError,
  NonFiniteFloatError,
  UnsupportedValueError,
} from './canon.ts';

export {
  fnv1a64,
  FNV1A64_OFFSET_BASIS,
  FNV1A64_PRIME,
  FNV1A64_TEXT_PATTERN,
  fnv1a64Update,
  fnv1a64Utf8,
  formatFnv1a64,
  parseFnv1a64,
  U64_MASK,
} from './fnv1a64.ts';

export {
  decodeTolerant,
  encodeTolerant,
  formatIssuePath,
  isTolerant,
  schemaLabel,
  TOLERANT_PARSE_OPTIONS,
  type TolerantDecoder,
  type TolerantEncoder,
  WireDecodeError,
  WireEncodeError,
  type WireIssue,
} from './tolerant.ts';

export {
  decodeJsonArray,
  decodeJsonObject,
  decodeJsonValue,
  decodeJsonValueFromString,
  isJsonArray,
  isJsonObject,
  isJsonValue,
  JsonArray,
  jsonField,
  JsonObject,
  JsonValue,
  JsonValueFromString,
} from './json.ts';

export {
  WireFloat,
  WireInt,
  WireNonNegativeInt,
  WireNumber,
} from './numeric.ts';

export {
  CONTROL_PROTOCOLS,
  ControlProtocol,
  FULL_CONTROL_V2,
  isControlProtocol,
  STRATEGIC_V1,
} from './control-protocol.ts';

export {
  decodeFrameIndex,
  decodeFrameIndexFromPngName,
  decodeFrameIndexFromString,
  decodeGameId,
  decodeRunState,
  decodeRunStateTolerant,
  DERIVED_RUN_STATES,
  FRAME_INDEX_DIGITS_RE,
  FRAME_INDEX_RE,
  FrameIndex,
  FrameIndexFromPngName,
  FrameIndexFromString,
  GAME_ID_RE,
  GameId,
  isGameId,
  isKnownRunState,
  isTerminalRunState,
  LIVE_RUN_STATES,
  RunState,
  RunStateTolerant,
  TERMINAL_RUN_STATES,
  UnrecognizedRunState,
} from './ids.ts';

export * as Agent from './agent/index.ts';
export * as Gateway from './gateway/index.ts';

/** Identity of this package, used by the harness to report its stack. */
export const WIRE_PACKAGE = '@arena/wire' as const;

/** The wire format revision this build speaks.  Bumped when packets change. */
export const WIRE_REVISION = 0 as const;
