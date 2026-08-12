/**
 * The replay gateway's problem shape — `{"error": "<message>"}` — and the
 * verbatim catalogue of every message it can put inside one.
 *
 * ## One shape, no negotiation
 *
 * `_problem` (`agent_eval/replay_gateway.py:1381`) is the single funnel for
 * every non-2xx body the gateway writes: `self._json(status, {"error":
 * message})`.  There is no RFC-7807 problem document here, no `code`, no
 * `detail`, no `type` — one key, a string value, serialized by `_canonical`
 * (sorted keys, `(",", ":")` separators, `ensure_ascii=False`) with
 * `Cache-Control: no-store`.  The 405 handler builds the same body by hand
 * (`:2047`) rather than routing through `_problem`, and it is byte-identical.
 *
 * So {@link GatewayProblem} decodes *every* error the gateway can produce, and
 * {@link GATEWAY_PROBLEM_STATUS} is the whole error surface of the service.
 *
 * ## The catalogue is the parity contract
 *
 * A port that returns "Not Found" where Python returns `not found` is a
 * regression the viewer sees (`viewer/src/api.ts:21-34` renders
 * `payload.error` verbatim, falling back to `"{status} {statusText}"`).  Every
 * literal below is therefore transcribed character-for-character from a
 * `raise GatewayProblem(...)` site, and `test/gateway/problem.test.ts`
 * re-extracts them from the Python source in both directions: a message the
 * gateway gained, lost, or reworded fails the suite.
 *
 * Line citations are the `raise` statement in
 * `agent_eval/replay_gateway.py` at the commit this file was written against.
 *
 * ## What this schema deliberately does *not* do
 *
 * It does not prove a payload *is* an error.  Success payloads carry a
 * top-level string `error` too — a manifest's failure note (`manifest.json`
 * `error`), the archive status doc's constant `"Archived game ended with an
 * error."` (`:859`, trap T6), the supervisor's live status — and those decode
 * here perfectly well, because tolerant decoding preserves the rest of the
 * document rather than rejecting it.  Discriminate on the HTTP status, or on
 * {@link isBareGatewayProblem}, which encodes the one structural fact that
 * separates them today: a problem body has exactly one key.
 *
 * Nor does it cover full-control-v2 structured errors.  Those are a different
 * envelope entirely — `{schema_version, control_protocol, error: {code,
 * message, retryable, details}, state_revision}` from
 * `agent_eval/full_control_v2.py:522-541`, carried as `APIProblem.payload` by
 * the *supervisor* (`supervisor.py:11411`) — where `error` is an object, not a
 * string.  {@link decodeGatewayProblem} rejects those, which is the correct
 * answer: they belong to the v2 agent contract, not to this one.
 *
 * @module
 */

import { Either, Option, Schema } from 'effect';
import { CANON_UTF8, type CanonError, canonicalBytes, canonicalText } from '../canon.ts';
import {
  decodeTolerant,
  encodeTolerant,
  isTolerant,
  type TolerantDecoder,
  type TolerantEncoder,
} from '../tolerant.ts';

// ---------------------------------------------------------------------------
// Transport constants
// ---------------------------------------------------------------------------

/** `Content-Type` on every JSON body the gateway writes (`:1369`, `:1379`). */
export const GATEWAY_PROBLEM_CONTENT_TYPE = 'application/json; charset=utf-8';

/** `Cache-Control` on every gateway-built JSON body, errors included (`:1360`). */
export const GATEWAY_PROBLEM_CACHE_CONTROL = 'no-store';

/**
 * The `Allow` header that rides with the 405 (`:2050`).  Every non-GET verb —
 * **including HEAD** — lands there (`do_HEAD = _method_not_allowed`, `:2059`).
 */
export const GATEWAY_METHOD_NOT_ALLOWED_ALLOW = 'GET';

// ---------------------------------------------------------------------------
// The message catalogue
// ---------------------------------------------------------------------------

/**
 * Every fixed message the gateway can emit, under a stable symbolic name.
 *
 * Names are ours; the strings are Python's.  Reach for a name when writing a
 * port (`GATEWAY_PROBLEM_MESSAGES.boardTurnNotPositive`) so a reworded upstream
 * message is a one-line change here instead of a grep across the harness.
 *
 * The two templated forms — `f"{label} not found"` / `f"{label} is
 * unavailable"` (`:580`, `:584`/`:590`/`:596`) — are listed by expansion
 * rather than by template, because `_read_archive_json` is only ever called
 * with two labels: `"game manifest"` (`:567`) and `"game report"` (`:781`).
 * {@link archiveJsonNotFound} and {@link archiveJsonUnavailable} rebuild them
 * for a port that would rather interpolate.  The one genuinely open form,
 * `f"upstream returned HTTP {status}"`, is {@link upstreamReturnedHttp}.
 */
export const GATEWAY_PROBLEM_MESSAGES = {
  // 400 — request-shape rejections
  /** `:1390` — `Content-Length` present but not an integer. */
  invalidContentLength: 'invalid Content-Length',
  /** `:1394` — a GET carrying a body or `Transfer-Encoding`. */
  getRequestBody: 'GET request bodies are not accepted',
  /** `:1972` — `/health` takes no query string at all. */
  healthQuery: 'health does not accept query parameters',
  /** `:1980` — `/v1/games` takes no query string at all. */
  gamesIndexQuery: 'games index does not accept query parameters',
  /** `:2006` — every subroute except replay/board/events rejects any query. */
  viewerRouteQuery: 'this viewer route does not accept query parameters',
  /** `:1729` — events.json rejects *any* query string, `limit` included. */
  eventsQuery: 'game events do not accept query parameters',
  /** `:1560` — `after_turn` or `limit` repeated, or an unknown parameter. */
  replayQueryDuplicates: 'replay query accepts one after_turn and one limit',
  /** `:1568` — non-integer `after_turn`/`limit`. */
  replayQueryNotIntegers: 'after_turn and limit must be integers',
  /** `:1573` — the pagination bounds, spelled exactly like this. */
  replayQueryOutOfRange: 'after_turn must be >= 0 and limit must be in [1, 250]',
  /** `:1803` — board.json requires `turn`; absent is as wrong as duplicated. */
  boardQueryTurn: 'board query requires exactly one turn',
  /** `:1810` — non-integer `turn`. */
  boardTurnNotInteger: 'turn must be an integer',
  /** `:1814` — `turn` is 1-based; `0` is rejected, not clamped. */
  boardTurnNotPositive: 'turn must be greater than zero',

  // 404 — nothing to serve
  /** `:1932`, `:1993`, `:2037` — unroutable path, or a game id failing `GAME_ID_RE`. */
  notFound: 'not found',
  /** `:559`, `:562`, `:565`, `:569` — id valid, run dir missing/symlinked/mismatched. */
  gameNotFound: 'game not found',
  /** `:580` with the `:567` label — `manifest.json` could not be opened. */
  manifestNotFound: 'game manifest not found',
  /** `:580` with the `:781` label — `report.json` could not be opened. */
  reportNotFound: 'game report not found',
  /** `:779`, `:787` — the run exists but is not in a terminal state. */
  terminalArchiveNotFound: 'terminal archive not found',
  /** `:876`, `:878`, `:881` — archive JSON the status route needs is gone. */
  archiveDataNotFound: 'archive data not found',
  /** `:1499`, `:1503` — a binary archive file (png/mp4) is gone. */
  archiveFileNotFound: 'archive file not found',
  /** `:1014` — `latest.png` with no frames on disk. */
  noMapFrames: 'no map frames are available',
  /** `:1019` — a frame index outside the archive. */
  mapFrameDoesNotExist: 'map frame does not exist',
  /** `:1027`, `:1029` — no `game.mp4`. */
  replayVideoNotFound: 'replay video not found',
  /** `:1712` — the replay loader found no telemetry for this game. */
  replayArtifactsNotFound: 'game replay artifacts not found',
  /** `:1784` — the events loader found no telemetry for this game. */
  eventArtifactsNotFound: 'game event artifacts not found',
  /** `:1866` — no autosave for the requested turn. */
  boardSnapshotNotFound: 'board snapshot not found',

  // 405
  /** `:2047` — built without `_problem`, same bytes, plus `Allow: GET`. */
  methodNotAllowed: 'method not allowed',

  // 500
  /** `:2041` — the catch-all; never leaks the exception text. */
  internalError: 'the replay gateway encountered an internal error',

  // 502 — the upstream supervisor misbehaved
  /** `:1431`, `:1437` — connect/read failure reaching the supervisor. */
  upstreamUnavailable: 'the upstream Freeciv service is unavailable',
  /** `:1527`, `:1535` — upstream body over `MAX_PROXY_JSON_BYTES` (8 MiB). */
  upstreamJsonTooLarge: 'the upstream JSON response is too large',
  /** `:1617`, `:1623` — upstream `/v1/games` was not a decodable index. */
  upstreamGamesIndexInvalid: 'the upstream games index is invalid',
  /** `:1456` and peers — a 3xx from upstream, downgraded to 502 here. */
  upstreamRedirect: 'upstream redirects are not allowed',

  // 503 — present but unusable
  /** `:584`/`:590`/`:596` with the `:567` label — manifest unreadable or not an object. */
  manifestUnavailable: 'game manifest is unavailable',
  /** `:584`/`:590`/`:596` with the `:781` label — report unreadable or not an object. */
  reportUnavailable: 'game report is unavailable',
  /** `:1375` — the gateway's own body exceeded 8 MiB. */
  archiveJsonTooLarge: 'archive JSON response is too large',
  /** `:1716`, `:1721` — the replay loader raised. */
  replayTelemetryUnavailable: 'replay telemetry is temporarily unavailable',
  /** `:1788`, `:1793` — the events loader raised. */
  eventsUnavailable: 'game events are temporarily unavailable',
  /** `:1870`, `:1875` — the board loader raised. */
  boardSnapshotUnavailable: 'board snapshot is temporarily unavailable',
} as const satisfies Record<string, string>;

/** Symbolic name of a fixed gateway problem message. */
export type GatewayProblemName = keyof typeof GATEWAY_PROBLEM_MESSAGES;

/** The literal union of every fixed message the gateway emits. */
export type GatewayProblemMessage = (typeof GATEWAY_PROBLEM_MESSAGES)[GatewayProblemName];

/**
 * The HTTP status each fixed message is sent with.
 *
 * Keyed by the message rather than by the symbolic name so that a response
 * seen on the wire maps straight to the status it *should* have carried —
 * which is exactly the assertion a parity rig wants to make.  The mapped type
 * makes the table total: a message added above without a status here is a type
 * error.
 *
 * Statuses are `HTTPStatus` members in the Python; `upstream returned HTTP
 * {status}` is the one message whose status is not fixed (see
 * {@link upstreamReturnedHttp}).
 */
export const GATEWAY_PROBLEM_STATUS: { readonly [M in GatewayProblemMessage]: number } = {
  'invalid Content-Length': 400,
  'GET request bodies are not accepted': 400,
  'health does not accept query parameters': 400,
  'games index does not accept query parameters': 400,
  'this viewer route does not accept query parameters': 400,
  'game events do not accept query parameters': 400,
  'replay query accepts one after_turn and one limit': 400,
  'after_turn and limit must be integers': 400,
  'after_turn must be >= 0 and limit must be in [1, 250]': 400,
  'board query requires exactly one turn': 400,
  'turn must be an integer': 400,
  'turn must be greater than zero': 400,
  'not found': 404,
  'game not found': 404,
  'game manifest not found': 404,
  'game report not found': 404,
  'terminal archive not found': 404,
  'archive data not found': 404,
  'archive file not found': 404,
  'no map frames are available': 404,
  'map frame does not exist': 404,
  'replay video not found': 404,
  'game replay artifacts not found': 404,
  'game event artifacts not found': 404,
  'board snapshot not found': 404,
  'method not allowed': 405,
  'the replay gateway encountered an internal error': 500,
  'the upstream Freeciv service is unavailable': 502,
  'the upstream JSON response is too large': 502,
  'the upstream games index is invalid': 502,
  'upstream redirects are not allowed': 502,
  'game manifest is unavailable': 503,
  'game report is unavailable': 503,
  'archive JSON response is too large': 503,
  'replay telemetry is temporarily unavailable': 503,
  'game events are temporarily unavailable': 503,
  'board snapshot is temporarily unavailable': 503,
};

const KNOWN_MESSAGES: ReadonlySet<string> = new Set<string>(
  Object.values(GATEWAY_PROBLEM_MESSAGES),
);

/** True when `message` is one of the fixed messages the gateway emits. */
export const isKnownGatewayProblemMessage = (message: string): message is GatewayProblemMessage =>
  KNOWN_MESSAGES.has(message);

// ---------------------------------------------------------------------------
// The templated messages
// ---------------------------------------------------------------------------

/**
 * The two labels `_read_archive_json` is ever called with (`:567`, `:781`).
 * They are what fills the `f"{label} ..."` templates at `:580` and `:584`.
 */
export const ARCHIVE_JSON_LABELS = ['game manifest', 'game report'] as const;

/** A label a `{label} not found` / `{label} is unavailable` message can carry. */
export type ArchiveJsonLabel = (typeof ARCHIVE_JSON_LABELS)[number];

/** `f"{label} not found"` — the 404 from `_read_archive_json` (`:580`). */
export const archiveJsonNotFound = (label: ArchiveJsonLabel): GatewayProblemMessage =>
  label === 'game manifest'
    ? GATEWAY_PROBLEM_MESSAGES.manifestNotFound
    : GATEWAY_PROBLEM_MESSAGES.reportNotFound;

/** `f"{label} is unavailable"` — the 503 from `_read_archive_json` (`:584`). */
export const archiveJsonUnavailable = (label: ArchiveJsonLabel): GatewayProblemMessage =>
  label === 'game manifest'
    ? GATEWAY_PROBLEM_MESSAGES.manifestUnavailable
    : GATEWAY_PROBLEM_MESSAGES.reportUnavailable;

/**
 * `f"upstream returned HTTP {status}"` (`:1460` and its five peers) — the one
 * message with an open value space.
 *
 * The status is relayed *as the downstream status too*: a 409 from the
 * supervisor's `/result` surfaces as HTTP 409 carrying this body.  The sole
 * exception is 3xx, which becomes a 502 with
 * {@link GATEWAY_PROBLEM_MESSAGES.upstreamRedirect} instead — see
 * {@link upstreamProblem}.
 *
 * `status` must be an integer: Python formats an `int`, so a fractional value
 * here would produce a message the gateway can never emit.
 */
export const upstreamReturnedHttp = (status: number): string =>
  `upstream returned HTTP ${String(status)}`;

/** Matches {@link upstreamReturnedHttp}; group 1 is the upstream status. */
export const UPSTREAM_STATUS_MESSAGE_RE: RegExp = /^upstream returned HTTP (\d+)$/;

/** The upstream status carried by an `upstream returned HTTP n` message. */
export const parseUpstreamHttpStatus = (message: string): Option.Option<number> => {
  const digits = UPSTREAM_STATUS_MESSAGE_RE.exec(message)?.[1];
  return digits === undefined ? Option.none() : Option.some(Number(digits));
};

/** A downstream status paired with the body the gateway sends alongside it. */
export interface GatewayProblemResponse {
  readonly status: number;
  readonly message: string;
}

/**
 * `_stream_upstream`'s non-2xx branch (`:1450-1460`), as a value: 3xx becomes
 * a 502 `upstream redirects are not allowed`; anything else is relayed with
 * its own status and the templated message.
 */
export const upstreamProblem = (upstreamStatus: number): GatewayProblemResponse =>
  upstreamStatus >= 300 && upstreamStatus < 400
    ? { status: 502, message: GATEWAY_PROBLEM_MESSAGES.upstreamRedirect }
    : { status: upstreamStatus, message: upstreamReturnedHttp(upstreamStatus) };

// ---------------------------------------------------------------------------
// The schema
// ---------------------------------------------------------------------------

/**
 * `{"error": "<message>"}` — the gateway's only error body (`:1381-1382`).
 *
 * Tolerant like every schema in this package: a future `{"error": ...,
 * "code": ...}` still decodes, and the unknown key survives a re-encode.
 */
export const GatewayProblem = Schema.Struct({
  /** The public, intentionally non-sensitive message. See {@link GATEWAY_PROBLEM_MESSAGES}. */
  error: Schema.String,
}).annotations({
  identifier: 'GatewayProblem',
  description: 'Replay gateway error body: {"error": "<message>"}',
});
/** A decoded gateway problem body. */
export type GatewayProblem = typeof GatewayProblem.Type;

/** Decode an error body the gateway sent. */
export const decodeGatewayProblem: TolerantDecoder<GatewayProblem> = decodeTolerant(
  GatewayProblem,
  'GatewayProblem',
);

/** Re-encode a problem body, unknown keys and their order intact. */
export const encodeGatewayProblem: TolerantEncoder<
  GatewayProblem,
  typeof GatewayProblem.Encoded
> = encodeTolerant(GatewayProblem, 'GatewayProblem');

/** True when `input` has a string `error`. Not proof it is an *error* body — see {@link isBareGatewayProblem}. */
export const isGatewayProblem: (input: unknown) => input is GatewayProblem =
  isTolerant(GatewayProblem);

/** The problem body parsed straight from response text. */
export const GatewayProblemFromString: Schema.Schema<GatewayProblem, string> =
  Schema.parseJson(GatewayProblem).annotations({ identifier: 'GatewayProblemFromString' });

/** Decode an error body from the raw response text. */
export const decodeGatewayProblemFromString: TolerantDecoder<GatewayProblem> = decodeTolerant(
  GatewayProblemFromString,
  'GatewayProblemFromString',
);

/**
 * True when the payload is a problem body *and nothing else*: one own key,
 * `error`, holding a string.
 *
 * This is the structural test that separates an error response from the many
 * success payloads that also carry a top-level `error` string — a manifest's
 * failure note, `report.json`'s, the archive status doc's constant
 * `"Archived game ended with an error."` (`:859`).  It is a fact about today's
 * gateway rather than a promise: prefer the HTTP status when you have it, and
 * expect this predicate to go false the day the gateway adds a second key.
 */
export const isBareGatewayProblem = (input: unknown): input is GatewayProblem =>
  isGatewayProblem(input) && Object.keys(input).length === 1;

// ---------------------------------------------------------------------------
// Byte parity
// ---------------------------------------------------------------------------

/**
 * The exact body bytes `_problem` writes, as text:
 * `_canonical({"error": message})` — sorted keys (there is only one),
 * `(",", ":")` separators, `ensure_ascii=False`.
 *
 * Fails, rather than silently escaping, when `message` contains a lone
 * surrogate — matching Python, where `.encode("utf-8")` raises
 * `UnicodeEncodeError` and the `:2038` catch-all turns the response into a 500
 * (trap T1).  A message from {@link GATEWAY_PROBLEM_MESSAGES} is pure ASCII
 * and can never take that branch.
 */
export const gatewayProblemBody = (message: string): Either.Either<string, CanonError> =>
  canonicalText({ error: message }, CANON_UTF8);

/** {@link gatewayProblemBody} through `.encode("utf-8")` — what `Content-Length` counts. */
export const gatewayProblemBytes = (message: string): Either.Either<Uint8Array, CanonError> =>
  canonicalBytes({ error: message }, CANON_UTF8);

// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------

/** What a problem message turned out to be. */
export type GatewayProblemClass =
  /** A fixed message from the catalogue, with the status it is sent with. */
  | { readonly _tag: 'Known'; readonly message: GatewayProblemMessage; readonly status: number }
  /** `upstream returned HTTP n` — the status is both relayed and reported. */
  | { readonly _tag: 'UpstreamStatus'; readonly status: number }
  /** Neither: a newer gateway, a different service, or a proxy in the way. */
  | { readonly _tag: 'Unrecognized'; readonly message: string };

/**
 * Classify a message the gateway sent.  Total, and never throws: an
 * unrecognized message is a value, because a gateway newer than this build is
 * a normal thing to meet.
 */
export const classifyGatewayProblemMessage = (message: string): GatewayProblemClass =>
  isKnownGatewayProblemMessage(message)
    ? { _tag: 'Known', message, status: GATEWAY_PROBLEM_STATUS[message] }
    : Option.match(parseUpstreamHttpStatus(message), {
        onNone: (): GatewayProblemClass => ({ _tag: 'Unrecognized', message }),
        onSome: (status): GatewayProblemClass => ({ _tag: 'UpstreamStatus', status }),
      });
