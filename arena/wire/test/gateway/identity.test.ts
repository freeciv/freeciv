/**
 * Decode parity for `GET /health` against the captured gateway payload.
 *
 * The Python in `agent_eval/replay_gateway.py` is the oracle: every
 * expectation here is either a fact about the captured bytes in
 * `test/fixtures/live/gateway-health.json` or a rule re-read from the Python
 * source itself, so a schema that "looks right" but disagrees with the server
 * fails here rather than in the parity rig.
 */
import { createHash } from 'node:crypto';
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import { CANON_UTF8, type CanonRecord, canonicalText } from 'src/canon';
import { decodeJsonObject, type JsonObject, type JsonValue } from 'src/json';
import { formatIssuePath } from 'src/tolerant';
import {
  GATEWAY_IDENTITY_RE,
  GATEWAY_KIND,
  GATEWAY_PROTOCOL_VERSION,
  GatewayIdentity,
  decodeGatewayIdentity,
  decodeServiceUrl,
  encodeGatewayIdentity,
  gatewayIdentityMaterial,
  gatewaySelfUrl,
  gatewaySelfUrlMatches,
  isGatewayIdentity,
  isGatewaySelfUrl,
  isNormalizedServiceUrl,
  isSupportedGatewayProtocol,
} from 'src/gateway/identity';

const FIXTURES = `${import.meta.dir}/../fixtures`;

const readFixture = (relative: string): Promise<string> =>
  Bun.file(`${FIXTURES}/${relative}`).text();

const rightOrThrow = <A, E>(either: Either.Either<A, E>): A =>
  Either.getOrThrowWith(either, (error) => new Error(`expected Right, got ${String(error)}`));

const accepts = (either: Either.Either<unknown, unknown>): boolean => Either.isRight(either);

/** The exact bytes the gateway put on the wire, and their parse. */
const HEALTH_TEXT = await readFixture('live/gateway-health.json');
/** Same route, different producer — see the rejection test below. */
const SUPERVISOR_HEALTH_TEXT = await readFixture('live/supervisor-health.json');
const HEALTH: JsonObject = rightOrThrow(decodeJsonObject(JSON.parse(HEALTH_TEXT) as unknown));

/** The captured payload with one field replaced — one defect at a time. */
const withField = (key: string, value: JsonValue): JsonObject => ({ ...HEALTH, [key]: value });

/** The captured payload with one key dropped. */
const without = (key: string): JsonObject =>
  Object.fromEntries(Object.entries(HEALTH).filter(([name]) => name !== key));

const issuePaths = (input: unknown): ReadonlyArray<string> =>
  Either.match(decodeGatewayIdentity(input), {
    onLeft: (error) => error.issues.map((issue) => formatIssuePath(issue.path)),
    onRight: () => [],
  });

describe('GET /health decodes the captured gateway payload', () => {
  test('the fixture decodes', () => {
    expect(accepts(decodeGatewayIdentity(HEALTH))).toBe(true);
  });

  test('isGatewayIdentity guards the decoded value, not the raw JSON', () => {
    // `Schema.is` tests the Type side. A gateway body's integers are `bigint`
    // once decoded and `number` on the wire, so the raw capture is correctly
    // *not* a decoded identity — asking the question the other way round is
    // what `decodeGatewayIdentity` is for.
    expect(isGatewayIdentity(rightOrThrow(decodeGatewayIdentity(HEALTH)))).toBe(true);
    expect(isGatewayIdentity(HEALTH)).toBe(false);
  });

  test('every field lands with the meaning the dossier gives it', () => {
    const identity = rightOrThrow(decodeGatewayIdentity(HEALTH));
    expect(identity.schema_version).toBe(1n);
    expect(identity.ok).toBe(true);
    expect(identity.kind).toBe(GATEWAY_KIND);
    expect(identity.protocol_version).toBe(BigInt(GATEWAY_PROTOCOL_VERSION));
    expect(GATEWAY_IDENTITY_RE.test(identity.identity)).toBe(true);
    expect(typeof identity.pid).toBe('bigint');
    expect(identity.host).toBe('127.0.0.1');
    expect(identity.port).toBe(62190n);
    expect(identity.url).toBe('http://127.0.0.1:62190');
    expect(identity.upstream_service_url).toBe('http://127.0.0.1:62188');
    // Operator-specific values: assert the shape, never the string.
    expect(identity.repo_root.length).toBeGreaterThan(0);
    expect(identity.runs_root.startsWith(identity.repo_root)).toBe(true);
    expect(identity.cache_root.startsWith(identity.repo_root)).toBe(true);
    expect(identity.viewer_public_url).toBe('https://freeciv.localhost');
    expect(isSupportedGatewayProtocol(identity)).toBe(true);
  });

  test('a round trip is byte-identical, key order and all', () => {
    // The body goes out through `_canonical` (py:123-126, sort_keys=True), so
    // the captured order is sorted; `propertyOrder: "original"` keeps it.
    expect(JSON.stringify(HEALTH)).toBe(HEALTH_TEXT);
    const identity = rightOrThrow(decodeGatewayIdentity(HEALTH));
    const encoded = rightOrThrow(encodeGatewayIdentity(identity));
    expect(JSON.stringify(encoded)).toBe(HEALTH_TEXT);
  });

  test('a decoded identity canonicalizes back to the captured bytes with no bridging', () => {
    // Cross-check on `_canonical` (py:123-126) and the point of dossier T1:
    // `pid`, `port`, `schema_version` and `protocol_version` are Python
    // `int`s, and CPython writes them without a fraction.
    //
    // This used to require the caller to hand-bridge all four with
    // `BigInt(...)`, because the schema decoded them to JS `number` — and the
    // divergence was pinned here as *expected*, contradicting the rule the
    // package barrel states ("a decoded `Gateway` integer is a `bigint`").
    // They are `WireInt` now, so the decoded value goes straight to the
    // canonical writer and the bytes match.
    const identity = rightOrThrow(decodeGatewayIdentity(HEALTH));
    const asCanon: CanonRecord = {
      ...HEALTH,
      schema_version: identity.schema_version,
      protocol_version: identity.protocol_version,
      pid: identity.pid,
      port: identity.port,
    };
    expect(rightOrThrow(canonicalText(asCanon, CANON_UTF8))).toBe(HEALTH_TEXT);
  });

  test('the JS number spelling is what the bigint convention exists to avoid', () => {
    // The raw `JSON.parse` output carries `number`s, and the canonical writer
    // is obliged to spell a `number` the way Python spells a `float`. This is
    // the digest mismatch `src/numeric.ts` opens by warning about, and the
    // reason `GatewayPid` is a `WireInt` rather than a `Schema.Int`.
    expect(rightOrThrow(canonicalText(HEALTH, CANON_UTF8))).toContain('"pid":77917.0');
  });

  test('a field this build has never heard of survives decode and re-encode', () => {
    const future = {
      ...HEALTH,
      tls: { enabled: false },
      shard_ids: [1, 2, 3],
    };
    const identity = rightOrThrow(decodeGatewayIdentity(future));
    const encoded = rightOrThrow(encodeGatewayIdentity(identity));
    expect(encoded).toMatchObject({ tls: { enabled: false }, shard_ids: [1, 2, 3] });
    expect(JSON.stringify(encoded)).toBe(JSON.stringify(future));
  });
});

/** `hashlib.sha256(material).hexdigest()[:20]` (py:183). */
const digest20 = (material: string): string =>
  createHash('sha256').update(material, 'utf8').digest('hex').slice(0, 20);

describe('identity is the sha256 of four of its own fields', () => {
  test('the captured token reproduces from the captured payload', () => {
    // `_identity` (py:175-183) is the strongest proof the four path/URL fields
    // were read with the right meanings and in the right order.
    const identity = rightOrThrow(decodeGatewayIdentity(HEALTH));
    expect(digest20(gatewayIdentityMaterial(identity))).toBe(identity.identity);
  });

  test('an absent viewer_public_url still contributes its empty slot', () => {
    // `viewer_public_url or ""` — five parts, four separators, always.
    const identity = rightOrThrow(decodeGatewayIdentity(without('viewer_public_url')));
    const material = gatewayIdentityMaterial(identity);
    expect(material.split('\0')).toHaveLength(5);
    expect(material.endsWith('\0')).toBe(true);
    // Dropping the viewer URL changes the fingerprint: a viewer-less gateway
    // is not the same gateway.
    expect(digest20(material)).not.toBe(identity.identity);
  });

  test('the token itself is 20 lowercase hex characters', () => {
    expect(accepts(decodeGatewayIdentity(withField('identity', '6438800F78AEA01C10D1')))).toBe(
      false,
    );
    expect(accepts(decodeGatewayIdentity(withField('identity', '6438800f78aea01c10d')))).toBe(
      false,
    );
    expect(accepts(decodeGatewayIdentity(withField('identity', '6438800f78aea01c10d1a')))).toBe(
      false,
    );
    expect(accepts(decodeGatewayIdentity(withField('identity', 'zzzzzzzzzzzzzzzzzzzz')))).toBe(
      false,
    );
  });
});

describe('url is built by f-string, not by the URL normalizer', () => {
  test('the captured url is exactly what host and port render to', () => {
    const identity = rightOrThrow(decodeGatewayIdentity(HEALTH));
    expect(gatewaySelfUrl(identity.host, identity.port)).toBe(identity.url);
    expect(gatewaySelfUrlMatches(identity)).toBe(true);
  });

  test('an IPv6 host is bracketed in url and bare in host', () => {
    const ipv6 = { ...HEALTH, host: '::1', url: 'http://[::1]:62190' };
    const identity = rightOrThrow(decodeGatewayIdentity(ipv6));
    expect(identity.host).toBe('::1');
    expect(gatewaySelfUrlMatches(identity)).toBe(true);
    // The unbracketed spelling is not a URL the gateway can emit.
    expect(isGatewaySelfUrl('http://::1:62190')).toBe(false);
  });

  test('the port is always explicit, even when it is the scheme default', () => {
    // py:1313 appends `:{port}` unconditionally, so the self URL is exactly
    // the string `_normalize_service_url` would have collapsed.
    expect(isGatewaySelfUrl('http://127.0.0.1:80')).toBe(true);
    expect(isNormalizedServiceUrl('http://127.0.0.1:80')).toBe(false);
    const defaulted = { ...HEALTH, port: 80, url: 'http://127.0.0.1:80' };
    expect(accepts(decodeGatewayIdentity(defaulted))).toBe(true);
  });

  test('shapes the f-string cannot produce are rejected', () => {
    expect(isGatewaySelfUrl('http://127.0.0.1')).toBe(false);
    expect(isGatewaySelfUrl('https://127.0.0.1:62190')).toBe(false);
    expect(isGatewaySelfUrl('http://127.0.0.1:62190/')).toBe(false);
    expect(isGatewaySelfUrl('http://127.0.0.1:62190/health')).toBe(false);
    expect(isGatewaySelfUrl('http://user@127.0.0.1:62190')).toBe(false);
    expect(isGatewaySelfUrl('')).toBe(false);
    expect(accepts(decodeGatewayIdentity(withField('url', 'http://127.0.0.1')))).toBe(false);
  });

  test('a cross-field mismatch is a value to branch on, not a decode failure', () => {
    // A gateway that grew a path prefix must still be readable: you need its
    // identity and pid precisely so you can report the mismatch.
    const drifted = rightOrThrow(decodeGatewayIdentity(withField('port', 1234)));
    expect(gatewaySelfUrlMatches(drifted)).toBe(false);
  });
});

describe('ServiceUrl is the normal form _normalize_service_url produces', () => {
  test.each([
    'http://127.0.0.1:62188',
    'https://freeciv.localhost',
    'http://[::1]:8080',
    'https://example.test/prefix',
    'http://example.test/deep/prefix',
  ])('accepts %s', (value) => {
    expect(isNormalizedServiceUrl(value)).toBe(true);
    expect(accepts(decodeServiceUrl(value))).toBe(true);
  });

  test.each([
    ['a trailing slash is rstripped by the producer', 'http://example.test/'],
    ['a scheme-default port is dropped by the producer', 'http://example.test:80'],
    ['an https default port is dropped too', 'https://example.test:443'],
    ['credentials are rejected outright', 'http://user:pass@example.test'],
    ['a query string is rejected outright', 'http://example.test?a=1'],
    ['a fragment is rejected outright', 'http://example.test#frag'],
    ['dot segments are rejected outright', 'http://example.test/a/../b'],
    ['the host is lowercased by the producer', 'http://EXAMPLE.test'],
    ['only http(s) survives', 'ftp://example.test'],
    ['a bare host is not a URL', 'example.test'],
    ['the empty string is not a URL', ''],
  ])('rejects %s', (_reason, value) => {
    expect(isNormalizedServiceUrl(value)).toBe(false);
    expect(accepts(decodeServiceUrl(value))).toBe(false);
  });

  test('the two URL fields on the payload use it', () => {
    expect(accepts(decodeGatewayIdentity(withField('upstream_service_url', 'http://x.test/')))).toBe(
      false,
    );
    expect(accepts(decodeGatewayIdentity(withField('viewer_public_url', 'http://x.test/')))).toBe(
      false,
    );
    expect(
      accepts(decodeGatewayIdentity(withField('upstream_service_url', 'http://x.test/prefix'))),
    ).toBe(true);
  });
});

describe('viewer_public_url is omit-or-present, never null', () => {
  test('omitting it decodes and re-encodes without inventing the key', () => {
    const bare = without('viewer_public_url');
    const identity = rightOrThrow(decodeGatewayIdentity(bare));
    expect(identity.viewer_public_url).toBeUndefined();
    const encoded = rightOrThrow(encodeGatewayIdentity(identity));
    expect(Object.hasOwn(encoded, 'viewer_public_url')).toBe(false);
    expect(JSON.stringify(encoded)).toBe(JSON.stringify(bare));
  });

  test('an explicit null is rejected — the producer omits, it does not null', () => {
    // Deliberately unlike the gateway's `optionalWith({nullable: true})`
    // fields (dossier T4): here "absent" carries meaning (watch URLs stay
    // relative, py:1883-1884), so a null would be a different payload.
    expect(accepts(decodeGatewayIdentity(withField('viewer_public_url', null)))).toBe(false);
    expect(new Set(issuePaths(withField('viewer_public_url', null)))).toEqual(
      new Set(['viewer_public_url']),
    );
  });
});

describe('what this payload is not', () => {
  test('the supervisor /health fixture is a different document', () => {
    // Same route, different producer: no kind, no schema_version, no identity.
    const supervisor = JSON.parse(SUPERVISOR_HEALTH_TEXT) as unknown;
    expect(accepts(decodeGatewayIdentity(supervisor))).toBe(false);
    expect(issuePaths(supervisor)).toContain('kind');
  });

  test('a payload that has stopped claiming health is not this packet', () => {
    expect(accepts(decodeGatewayIdentity(withField('ok', false)))).toBe(false);
    expect(accepts(decodeGatewayIdentity(withField('kind', 'freeciv-supervisor')))).toBe(false);
  });

  test('schema_version is pinned; protocol_version is not', () => {
    expect(accepts(decodeGatewayIdentity(withField('schema_version', 2)))).toBe(false);
    expect(accepts(decodeGatewayIdentity(withField('schema_version', '1')))).toBe(false);
    const newer = rightOrThrow(decodeGatewayIdentity(withField('protocol_version', 2)));
    expect(isSupportedGatewayProtocol(newer)).toBe(false);
    expect(newer.url).toBe('http://127.0.0.1:62190');
    expect(accepts(decodeGatewayIdentity(withField('protocol_version', -1)))).toBe(false);
    expect(accepts(decodeGatewayIdentity(withField('protocol_version', 1.5)))).toBe(false);
  });

  test('pid and port carry their operating-system bounds', () => {
    expect(accepts(decodeGatewayIdentity(withField('pid', 0)))).toBe(false);
    expect(accepts(decodeGatewayIdentity(withField('pid', -1)))).toBe(false);
    expect(accepts(decodeGatewayIdentity(withField('pid', 1.5)))).toBe(false);
    expect(accepts(decodeGatewayIdentity(withField('pid', 1)))).toBe(true);
    // `--port 0` binds an ephemeral port; the payload reports the bound one.
    expect(accepts(decodeGatewayIdentity(withField('port', 0)))).toBe(false);
    expect(accepts(decodeGatewayIdentity(withField('port', 65536)))).toBe(false);
    expect(
      accepts(decodeGatewayIdentity({ ...HEALTH, port: 65535, url: 'http://127.0.0.1:65535' })),
    ).toBe(true);
  });

  test('the paths are opaque strings, but they must be there', () => {
    expect(accepts(decodeGatewayIdentity(withField('repo_root', 'C:\\checkout\\freeciv')))).toBe(
      true,
    );
    expect(accepts(decodeGatewayIdentity(withField('runs_root', '/tmp/rüns/ワールド')))).toBe(true);
    expect(accepts(decodeGatewayIdentity(withField('cache_root', '')))).toBe(false);
    expect(issuePaths(without('repo_root'))).toEqual(['repo_root']);
  });

  test('failures are values, and every issue is reported at once', () => {
    const broken = { ...HEALTH, ok: false, pid: 0, identity: 'nope' };
    const result = decodeGatewayIdentity(broken);
    expect(Either.isLeft(result)).toBe(true);
    expect(new Set(issuePaths(broken))).toEqual(new Set(['ok', 'pid', 'identity']));
    expect(isGatewayIdentity(broken)).toBe(false);
  });

  test('non-objects are rejected without coercion', () => {
    expect(issuePaths(null)).toEqual(['<root>']);
    expect(accepts(decodeGatewayIdentity(HEALTH_TEXT))).toBe(false);
    expect(accepts(decodeGatewayIdentity([HEALTH]))).toBe(false);
  });
});

/**
 * The Python gateway owns these rules for as long as both implementations
 * run.  Re-reading its source keeps the citations in `src/gateway/identity.ts`
 * from rotting and turns a server-side field addition into a red test here.
 */
/**
 * Read a Python source file the parity assertions below are written against.
 *
 * **Ungated on purpose.**  A missing `agent_eval/` used to make these tests
 * *skip*, so a checkout without the Python side reported the whole parity story
 * green while checking nothing.  This throws at module load instead, which is
 * the standard `test/canon.test.ts` and `test/fnv1a64.test.ts` already set for
 * the python3 oracle: a missing authority fails, it does not disappear.
 */
const readPythonSource = (path: string): Promise<string> => Bun.file(path).text();

const gatewaySource = await readPythonSource(
  `${import.meta.dir}/../../../../agent_eval/replay_gateway.py`,
);

describe('parity with agent_eval/replay_gateway.py', () => {
  test('the Python source is present — ungated, so a missing authority fails instead of skipping', () => {
    expect(gatewaySource.length).toBeGreaterThan(0);
  });

  const identityPayloadSource = gatewaySource.slice(
    gatewaySource.indexOf('def identity_payload'),
    gatewaySource.indexOf('class ReplayGatewayHandler'),
  );

  test('GATEWAY_KIND and GATEWAY_PROTOCOL_VERSION are transcribed verbatim', () => {
    expect(gatewaySource).toContain(`GATEWAY_KIND = "${GATEWAY_KIND}"`);
    expect(gatewaySource).toContain(
      `GATEWAY_PROTOCOL_VERSION = ${String(GATEWAY_PROTOCOL_VERSION)}`,
    );
  });

  test('the schema names exactly the keys identity_payload emits', () => {
    const emitted = new Set([
      ...[...identityPayloadSource.matchAll(/"([a-z_]+)":/g)].map((match) => match[1]),
      ...[...identityPayloadSource.matchAll(/payload\["([a-z_]+)"\]/g)].map((match) => match[1]),
    ]);
    expect(emitted).toEqual(new Set<string | undefined>(Object.keys(GatewayIdentity.fields)));
  });

  test('url is still the f-string this port reimplements', () => {
    expect(identityPayloadSource).toContain('"url": f"http://{rendered_host}:{port}"');
    expect(identityPayloadSource).toContain('f"[{host}]" if ":" in host else host');
  });

  test('the identity preimage is still five NUL-joined parts', () => {
    expect(gatewaySource).toContain('material = "\\0".join((');
    expect(gatewaySource).toContain(
      'str(repo_root), service_url, str(runs_root), str(cache_root),',
    );
    expect(gatewaySource).toContain('viewer_public_url or "",');
    expect(gatewaySource).toContain('hashlib.sha256(material).hexdigest()[:20]');
  });

  test('viewer_public_url is still added conditionally, never nulled', () => {
    expect(identityPayloadSource).toContain(
      'if self.gateway_config.viewer_public_url is not None:',
    );
    expect(identityPayloadSource).not.toContain('"viewer_public_url": None');
  });

  test('the body is still canonical JSON, sorted and space-free', () => {
    expect(gatewaySource).toContain('value, sort_keys=True, separators=(",", ":")');
    expect(gatewaySource).toContain('self._json(HTTPStatus.OK, self.server.identity_payload())');
  });

  test('/health still refuses a query string rather than ignoring one', () => {
    expect(gatewaySource).toContain('"health does not accept query parameters"');
  });
});
