/**
 * `GET /health` — the replay gateway's identity payload.
 *
 * Produced by `ReplayGatewayHTTPServer.identity_payload`
 * (`agent_eval/replay_gateway.py:1301-1325`) and served by the `/health`
 * route (`:1970-1978`, which 400s on *any* query string).  The same dict is
 * written to the on-disk ready file at mode 0600 (`_write_private_json`,
 * `:221-240`) and printed as one canonical line on stdout (`main`, `:2201`),
 * so this one schema decodes all three: the HTTP body, the ready file the
 * local stack polls for, and the launcher's stdout handshake.
 *
 * The payload is never modelled in the viewer's `types.ts`, which is why the
 * Python source and the captured fixture — not the TypeScript viewer — are the
 * oracle here.
 *
 * ## What this payload is *for*
 *
 * It answers "which gateway am I talking to, and does it serve the runs I
 * think it does?".  Every field except `pid` feeds that question, and
 * `identity` folds four of them into one comparable token
 * (see {@link gatewayIdentityMaterial}).  Two gateways with the same
 * `identity` are interchangeable; two with different ones are not, however
 * similar their URLs look.
 *
 * ## Strictness, deliberately asymmetric
 *
 * - `schema_version` is a literal `1`.  It versions *this document's shape*,
 *   so decoding a `2` with a `1` reader is unsound and must fail loudly.
 * - `protocol_version` is a plain non-negative integer.  It versions the
 *   *conversation*, and it exists to be compared — pinning it to a literal
 *   would turn "the peer speaks a newer protocol" from a value you can branch
 *   on into a decode failure that throws away the `url` and `pid` you need in
 *   order to report the mismatch.  Compare with
 *   {@link isSupportedGatewayProtocol}.
 * - `ok` and `kind` are literals: a payload from a different service, or a
 *   gateway that has stopped claiming to be healthy, is not this packet.
 *
 * @module
 */

import { Schema } from 'effect';
import { WireInt, WireNonNegativeInt } from '../numeric.ts';
import { decodeTolerant, encodeTolerant, isTolerant } from '../tolerant.ts';
import type { TolerantDecoder, TolerantEncoder } from '../tolerant.ts';

/**
 * `GATEWAY_KIND` — `agent_eval/replay_gateway.py:52`.  The self-identifying
 * discriminator, and the only field that distinguishes this payload from the
 * supervisor's own `/health` (which carries no `kind` at all).
 */
export const GATEWAY_KIND = 'freeciv-replay-gateway' as const;

/**
 * `GATEWAY_PROTOCOL_VERSION` — `agent_eval/replay_gateway.py:53`.  The
 * revision of the route contract this build was written against; see the
 * module doc for why the schema does not pin it.
 */
export const GATEWAY_PROTOCOL_VERSION = 1 as const;

/** `schema_version` of the identity document (`:1305`). */
export const GATEWAY_IDENTITY_SCHEMA_VERSION = 1 as const;

/**
 * `schema_version` as it decodes: the literal 1, as a `bigint`.
 *
 * A `Schema.Literal(1)` would decode to the JS `number` 1, and every copy of
 * this document is written by CPython with `sort_keys=True`: the HTTP body
 * through `_canonical` (`replay_gateway.py:123-128`), the 0600 ready file
 * through `_write_private_json` (`replay_gateway.py:221-240`), and the stdout
 * handshake line.  A consumer that decoded and re-canonicalized would spell it
 * `1.0` where Python wrote `1`, and every digest over those bytes would
 * disagree.  See `../numeric.ts`.
 */
const GatewayIdentitySchemaVersion = WireInt.pipe(
  Schema.filter((value) => value === BigInt(GATEWAY_IDENTITY_SCHEMA_VERSION), {
    identifier: 'GatewayIdentitySchemaVersion',
    description: 'Identity payload schema version, always the literal 1',
  }),
);

/**
 * `hashlib.sha256(material).hexdigest()[:20]` (`_identity`, `:175-183`) — 20
 * characters of *lowercase* hex.  `hexdigest()` never emits uppercase, so a
 * case-insensitive pattern here would accept something the gateway cannot
 * produce and quietly break equality comparisons against a value that did
 * come from it.
 */
export const GATEWAY_IDENTITY_RE: RegExp = /^[0-9a-f]{20}$/;

/**
 * The 20-hex-character gateway identity token.  Branded because its whole
 * purpose is equality: comparing it to an arbitrary `string` is a bug, and
 * comparing two of them is the supported way to ask "same gateway?".
 */
export const GatewayIdentityToken = Schema.String.pipe(
  Schema.pattern(GATEWAY_IDENTITY_RE, {
    identifier: 'GatewayIdentityToken',
    description: 'First 20 lowercase hex chars of the gateway identity sha256',
  }),
  Schema.brand('GatewayIdentityToken'),
);
/** A validated gateway identity token. */
export type GatewayIdentityToken = typeof GatewayIdentityToken.Type;

/** Decode an unknown value as a {@link GatewayIdentityToken}. */
export const decodeGatewayIdentityToken: TolerantDecoder<GatewayIdentityToken> = decodeTolerant(
  GatewayIdentityToken,
  'GatewayIdentityToken',
);

/**
 * A bound TCP port as the gateway reports it.  Always the *actual* bound
 * port, so `--port 0` shows up here as whatever the kernel handed out
 * (`:1302`, `:1312`) and never as `0`.
 */
export const GatewayPort = WireInt.pipe(Schema.betweenBigInt(1n, 65535n)).annotations({
  identifier: 'GatewayPort',
  description: 'Bound TCP port, 1-65535',
});
/** A bound TCP port. */
export type GatewayPort = typeof GatewayPort.Type;

/**
 * `os.getpid()` (`:1310`).  A POSIX pid is a positive integer; the gateway
 * itself relies on that when it refuses to delete a ready file whose `pid`
 * does not match its own (`_remove_owned_ready_file`, `:2100-2110`).
 */
export const GatewayPid = WireInt.pipe(Schema.positiveBigInt()).annotations({
  identifier: 'GatewayPid',
  description: 'Operating-system process id of the gateway',
});
/** A process id. */
export type GatewayPid = typeof GatewayPid.Type;

/**
 * An absolute filesystem path as the gateway reports it (`repo_root`,
 * `runs_root`, `cache_root`).  Every one is `Path(...).expanduser().resolve()`
 * (`gateway_config`, `:205-213`), so it is absolute and symlink-free — but the
 * *value* is whatever checkout the operator ran, and the fixtures embed one
 * developer's home directory.  Assert on shape, never on these strings.
 *
 * No separator or drive-letter rule is imposed: pinning `/` would make the
 * schema reject a payload the same Python emits on a platform this port has
 * no reason to exclude.
 */
export const GatewayPath = Schema.NonEmptyString.annotations({
  identifier: 'GatewayPath',
  description: 'Absolute, resolved filesystem path (operator-specific value)',
});
/** An absolute filesystem path. */
export type GatewayPath = typeof GatewayPath.Type;

const httpUrlOrNull = (value: string): URL | null =>
  URL.canParse(value) ? new URL(value) : null;

/**
 * Re-render a parsed URL the way `_normalize_service_url` does
 * (`:129-161`): `urlunsplit((scheme, rendered_host, path.rstrip("/"), "", ""))`.
 *
 * `URL` already lowercases the scheme and hostname, brackets an IPv6 literal
 * and drops a scheme-default port, which is exactly the `rendered_host`
 * construction on the Python side.
 */
const renderServiceUrl = (parsed: URL): string =>
  `${parsed.protocol}//${parsed.host}${parsed.pathname.replace(/\/+$/, '')}`;

/**
 * True when `value` is already in the normal form `_normalize_service_url`
 * (`:129-161`) produces: an `http`/`https` origin, no userinfo, no query, no
 * fragment, no dot segments, no trailing slash, optional path prefix.
 *
 * The check is *idempotence against the URL parser* rather than a transcribed
 * regex: `new URL` resolves `..`/`.` segments and normalizes the host, so any
 * input the Python would have rejected — or would have rendered differently —
 * fails to survive the round trip.
 *
 * One known divergence, harmless for the values the gateway emits: Python
 * leaves a literal space or non-ASCII character in a path prefix alone while
 * `URL` percent-encodes it, so such a path would be rejected here.  No
 * captured payload has one, and the operator would have to pass an
 * `--upstream-url` with a space in its path to make one.
 */
export const isNormalizedServiceUrl = (value: string): boolean => {
  const parsed = httpUrlOrNull(value);
  return (
    parsed !== null &&
    (parsed.protocol === 'http:' || parsed.protocol === 'https:') &&
    parsed.username === '' &&
    parsed.password === '' &&
    parsed.search === '' &&
    parsed.hash === '' &&
    value === renderServiceUrl(parsed)
  );
};

/**
 * A normalized service origin, as `upstream_service_url` (`:1315`) and
 * `viewer_public_url` (`:1321-1324`) carry it.  Both went through
 * `_normalize_service_url` at config time (`:207-208`), so a gateway that is
 * up has already proven its URLs match this shape — the gateway would have
 * refused to start otherwise.
 */
export const ServiceUrl = Schema.String.pipe(
  Schema.filter(isNormalizedServiceUrl, {
    identifier: 'ServiceUrl',
    description: 'Credential-free http(s) origin with optional path prefix, no trailing slash',
  }),
);
/** A normalized service origin. */
export type ServiceUrl = typeof ServiceUrl.Type;

/** Decode an unknown value as a {@link ServiceUrl}. */
export const decodeServiceUrl: TolerantDecoder<ServiceUrl> = decodeTolerant(
  ServiceUrl,
  'ServiceUrl',
);

/**
 * The gateway's own address, built by f-string at `:1313` —
 * `f"http://{rendered_host}:{port}"` — and **not** by
 * `_normalize_service_url`.
 *
 * Two consequences a port must preserve:
 *
 * 1. The scheme is hardcoded `http`.  The gateway binds a loopback literal
 *    (`_loopback_host`, `:164-173`) and never terminates TLS.
 * 2. The port is *always* explicit, even when it is the scheme default, so
 *    `url` is `"http://127.0.0.1:80"` where a normalized service URL would be
 *    `"http://127.0.0.1"`.  {@link ServiceUrl} would reject that string; this
 *    schema must not.
 */
export const GATEWAY_SELF_URL_RE: RegExp =
  /^http:\/\/(?:\[[0-9A-Fa-f:.]+\]|[^\s/[\]:@?#]+):(?:0|[1-9][0-9]*)$/;

/** True when `value` has the `http://{host}:{port}` shape `:1313` emits. */
export const isGatewaySelfUrl = (value: string): boolean => GATEWAY_SELF_URL_RE.test(value);

/** The gateway's own `http://{host}:{port}` address. */
export const GatewaySelfUrl = Schema.String.pipe(
  Schema.filter(isGatewaySelfUrl, {
    identifier: 'GatewaySelfUrl',
    description: 'http://{host}:{port} with an always-explicit port, IPv6 host bracketed',
  }),
);
/** The gateway's own address. */
export type GatewaySelfUrl = typeof GatewaySelfUrl.Type;

/**
 * Rebuild `url` from `host` and `port` exactly as `identity_payload` does
 * (`:1302-1303`, `:1313`): an IPv6 host is bracketed, an IPv4 host is not,
 * and the port is always appended.
 */
export const gatewaySelfUrl = (host: string, port: bigint | number): string =>
  `http://${host.includes(':') ? `[${host}]` : host}:${String(port)}`;

/**
 * `GET /health` on the replay gateway.
 *
 * Fields are declared in the order `identity_payload` inserts them
 * (`:1304-1320`), which is *not* the order they appear on the wire: the body
 * goes out through `_canonical` (`:123-126`, `sort_keys=True`), so the HTTP
 * response, the ready file and the stdout line are all key-sorted.  Decoding
 * preserves whatever order arrived, so this declaration order costs nothing
 * and documents the producer.
 */
export const GatewayIdentity = Schema.Struct({
  /** Literal `1` — the shape of *this* document. */
  schema_version: GatewayIdentitySchemaVersion,
  /** Literal `true`.  The route has no unhealthy branch: it either answers or it does not. */
  ok: Schema.Literal(true),
  /** Literal `"freeciv-replay-gateway"` (`GATEWAY_KIND`, `:52`). */
  kind: Schema.Literal(GATEWAY_KIND),
  /** Route-contract revision; compare, do not assert.  See the module doc. */
  protocol_version: WireNonNegativeInt,
  /** sha256 fingerprint of the four paths/URLs below (`:175-183`). */
  identity: GatewayIdentityToken,
  /** `os.getpid()` of the serving process (`:1310`). */
  pid: GatewayPid,
  /**
   * The bound loopback host, **unbracketed** even for IPv6 (`:1302`, `:1311`)
   * — `url` is where the brackets appear.  The loopback rule is enforced when
   * the server is constructed (`_loopback_host`, `:164-173`), not on the wire,
   * so this stays an opaque string.
   */
  host: Schema.NonEmptyString,
  /** The actually-bound port (`:1312`). */
  port: GatewayPort,
  /** `http://{host}:{port}` (`:1313`); see {@link GatewaySelfUrl}. */
  url: GatewaySelfUrl,
  /** Resolved checkout root (`:1314`). */
  repo_root: GatewayPath,
  /** Normalized origin of the supervisor this gateway proxies (`:1315-1317`). */
  upstream_service_url: ServiceUrl,
  /** Resolved directory the run archives are read from (`:1318`). */
  runs_root: GatewayPath,
  /** Resolved directory derived turn snapshots are cached in (`:1319`). */
  cache_root: GatewayPath,
  /**
   * Present **only** when the operator passed `--viewer-public-url`
   * (`:1321-1324`).  This key is omit-or-present, never `null` — the
   * deliberate opposite of the `optionalWith({ nullable: true })` fields
   * elsewhere in the gateway, where a blank value is nulled instead of
   * dropped.  Its presence also flips watch URLs from relative to absolute
   * (`:1883-1884`, `:1916`), so "absent" and "null" would mean different
   * things to a consumer even if the producer were sloppy.  It is not sloppy.
   */
  viewer_public_url: Schema.optional(ServiceUrl),
}).annotations({
  identifier: 'GatewayIdentity',
  description: 'GET /health on the freeciv replay gateway (identity_payload)',
});
/** A decoded `GET /health` payload. */
export type GatewayIdentity = typeof GatewayIdentity.Type;

/** Decode a `GET /health` payload, keeping fields this build does not know. */
export const decodeGatewayIdentity: TolerantDecoder<GatewayIdentity> = decodeTolerant(
  GatewayIdentity,
  'GatewayIdentity',
);

/** Re-encode a `GET /health` payload, unknown fields and key order intact. */
export const encodeGatewayIdentity: TolerantEncoder<
  GatewayIdentity,
  typeof GatewayIdentity.Encoded
> = encodeTolerant(GatewayIdentity, 'GatewayIdentity');

/**
 * True when `input` is a well-formed **decoded** `GET /health` payload — one
 * whose integers are already `bigint`.  To ask whether raw JSON off the socket
 * would decode, use {@link decodeGatewayIdentity} and test the `Either`; see
 * `isTolerant` in `../tolerant.ts` for why the two differ here.
 */
export const isGatewayIdentity: (input: unknown) => input is GatewayIdentity =
  isTolerant(GatewayIdentity);

/**
 * The exact preimage `_identity` hashes (`:175-183`):
 *
 * ```python
 * material = "\0".join((
 *     str(repo_root), service_url, str(runs_root), str(cache_root),
 *     viewer_public_url or "",
 * )).encode("utf-8")
 * ```
 *
 * Note the fifth element: an absent `viewer_public_url` contributes an empty
 * string, so the trailing `\0` is always there.  Feeding this through sha256
 * and taking the first 20 hex characters must reproduce `identity` — that is
 * the strongest available proof that a port read the four fields with the
 * right meanings, and `test/gateway/identity.test.ts` runs it against the
 * captured payload.
 *
 * Kept hash-free on purpose: `@arena/wire` depends on `effect` and nothing
 * else, so the digest belongs to the caller that already has one.
 */
export const gatewayIdentityMaterial = (identity: GatewayIdentity): string =>
  [
    identity.repo_root,
    identity.upstream_service_url,
    identity.runs_root,
    identity.cache_root,
    identity.viewer_public_url ?? '',
  ].join('\0');

/**
 * True when `url` is the `host`/`port` pair rendered the way `:1313` renders
 * it.  The producer guarantees this, but it is a *cross-field* invariant and
 * deliberately not part of the schema: a gateway that ever grows a proxy
 * prefix should still decode here, and be caught by this predicate, rather
 * than become an undecodable payload whose `identity` you cannot even read.
 */
export const gatewaySelfUrlMatches = (identity: GatewayIdentity): boolean =>
  identity.url === gatewaySelfUrl(identity.host, identity.port);

/**
 * True when the peer speaks the protocol revision this build was written
 * against ({@link GATEWAY_PROTOCOL_VERSION}).  A `false` here is a real
 * answer — the payload decoded, so the caller still has the peer's `url` and
 * `pid` to put in the error it raises.
 */
export const isSupportedGatewayProtocol = (identity: GatewayIdentity): boolean =>
  identity.protocol_version === BigInt(GATEWAY_PROTOCOL_VERSION);
