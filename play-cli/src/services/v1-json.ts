/**
 * The strategic-v1 transport, with CPython's `int`/`float` distinction intact.
 *
 * `next`, `act` and `result` are the three commands in `V2_JSON_ONLY_COMMANDS`
 * whose **entire** stdout is `_print_json(value)` — the body the supervisor
 * sent, re-serialized.  That makes the int/float question load-bearing in a way
 * it is nowhere else: it is not one field of a rendered card, it is every line
 * of the output.
 *
 * Core's `src/services/http.ts` runs `JSON.parse` on the response body, and
 * JavaScript has one number type, so `180.0` and `180` arrive as the same
 * value and `String(180)` prints `180`.  CPython prints `180.0`.  That is not
 * hypothetical on this surface:
 *
 * | payload | field | supervisor |
 * | --- | --- | --- |
 * | `GET /v1/games/{id}/me/next` | `action_timeout_s` | `TIMING_MODE_TIMEOUTS["default"] = 180.0`, published verbatim (supervisor.py:117, 8422) |
 * | `GET /v1/games/{id}/me/next` | `deadline_at` | `time.time()` plus the timeout |
 * | `GET /v1/games/{id}/me/next` | `pending_duration_s` | `round(max(0.0, …), 3)` (supervisor.py:8290) |
 * | `POST /v1/games/{id}/me/actions` | the same barrier-progress block, echoed |
 * | `GET /v1/games/{id}/result` | `config.action_timeout_s`, `lobby_timeout_s`, every `latency_ms` | the run report, verbatim |
 *
 * So line 2 of *every* `next` response diverged, on 100% of the strategic-v1
 * poll loop.  U06 repaired its own surface with a field map
 * (`src/services/health-json.ts`), but a field map cannot work here: `result`
 * returns the whole run report, an arbitrarily deep document written by four
 * different modules, and `next` embeds an arbitrary `observation`.  The only
 * answer that scales is the one NOTES §2 names as the general fix — decode the
 * body with U13's `parsePython`, which reads the *lexeme*, so a fraction or an
 * exponent makes a `float` and anything else an exact `int`, exactly as
 * `json.scanner` does.
 *
 * This module therefore does the request itself rather than through core's
 * `Http`: the `Http` service hands back an already-parsed `JsonObject` and the
 * lexeme is gone by then, and `src/services/http.ts` is core's file, not this
 * unit's.  Everything else about the request is core's semantics — the same
 * refusal sentences, the same redirect policy, the same timeout shape, and
 * core's own `v1ErrorMessage` for a non-2xx — so the two transports cannot
 * drift on anything but the number model.  See NOTES §11.9.
 */
import { Console, Context, Effect, Layer, Option } from 'effect';
import { PlayerError, playerError } from 'src/errors';
import {
  PyFloat,
  PyInt,
  isPyMapping,
  parsePython,
  pyDumps,
  pyFloatRepr,
  type PyObject,
  type PyValue,
} from 'src/services/canonical-body';
import { encodeStringAscii } from 'src/services/json-output';
import { caTrustedFetch, v1ErrorMessage } from 'src/services/http';
import { isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';

// ---------------------------------------------------------------------------
// json.dumps(indent=2, sort_keys=True) over the Python number model
// ---------------------------------------------------------------------------

/**
 * The number token, when the value knows which Python type it came from.
 *
 * Identical to `canonical-body`'s private rule (that module's `numberText` is
 * not exported): a wrapped `int` prints exact and undecorated, a wrapped
 * `float` prints through `repr`, and a bare JavaScript number — anything this
 * port built rather than read off the wire — prints as an `int` when it is one.
 */
const numberText = (value: PyInt | PyFloat | number): string => {
  if (value instanceof PyInt) return value.value.toString();
  if (value instanceof PyFloat) return pyFloatRepr(value.value);
  return Number.isInteger(value) && Math.abs(value) <= Number.MAX_SAFE_INTEGER
    ? String(value)
    : pyFloatRepr(value);
};

const isPyNumber = (value: PyValue): value is PyInt | PyFloat =>
  value instanceof PyInt || value instanceof PyFloat;

const sortKeysAscii = (keys: ReadonlyArray<string>): ReadonlyArray<string> =>
  [...keys].sort((left, right) => (left < right ? -1 : left > right ? 1 : 0));

const INDENT = '  ';

const dump = (value: PyValue, depth: number): string => {
  if (value === null || value === undefined) return 'null';
  if (typeof value === 'boolean') return value ? 'true' : 'false';
  if (typeof value === 'number' || isPyNumber(value)) return numberText(value);
  if (typeof value === 'string') return encodeStringAscii(value, true);
  const outer = `\n${INDENT.repeat(depth)}`;
  const inner = `\n${INDENT.repeat(depth + 1)}`;
  if (Array.isArray(value)) {
    if (value.length === 0) return '[]';
    return `[${inner}${value.map((item) => dump(item, depth + 1)).join(`,${inner}`)}${outer}]`;
  }
  if (isPyMapping(value)) {
    const keys = sortKeysAscii(Object.keys(value));
    if (keys.length === 0) return '{}';
    const parts = keys.map((key) => {
      const item = value[key];
      return `${encodeStringAscii(key, true)}: ${dump(item === undefined ? null : item, depth + 1)}`;
    });
    return `{${inner}${parts.join(`,${inner}`)}${outer}}`;
  }
  return 'null';
};

/**
 * `json.dumps(value, indent=2, sort_keys=True)` — `_print_json`'s shape
 * (client.py:6127-6128), over a value that remembers its Python numeric type.
 *
 * Core's `indentedJson` is the same layout over plain JavaScript numbers; the
 * only difference is `180.0`.
 */
export const pyIndentedJson = (value: PyValue): string => dump(value, 0);

/** `_print_json` for a body decoded by {@link parsePython}. */
export const printPyJson = (value: PyValue): Effect.Effect<void> =>
  Console.log(pyIndentedJson(value));

// ---------------------------------------------------------------------------
// Reading a decoded body
// ---------------------------------------------------------------------------

/** `dict.get(key)`: absent and JSON `null` collapse to `None`, as in CPython. */
export const pyField = (value: PyObject, key: string): PyValue => {
  if (!Object.prototype.hasOwnProperty.call(value, key)) return null;
  const item = value[key];
  return item === undefined ? null : item;
};

/**
 * Drop back to a plain JSON value, for the identity comparisons only.
 *
 * The guards in `next` and `act` are CPython `!=` over the two values, and
 * CPython's `!=` does **not** distinguish `1` from `1.0` — `180 == 180.0` is
 * `True`.  So the comparison deliberately erases exactly the distinction the
 * printer preserves; comparing the `repr`s instead would invent a mismatch
 * CPython never sees.
 */
export const pyToJson = (value: PyValue): JsonValue => {
  if (value === null || value === undefined) return null;
  if (value instanceof PyInt) return Number(value.value);
  if (value instanceof PyFloat) return value.value;
  if (typeof value === 'boolean' || typeof value === 'number' || typeof value === 'string') {
    return value;
  }
  if (Array.isArray(value)) return value.map((item) => pyToJson(item));
  if (isPyMapping(value)) {
    const out: Record<string, JsonValue> = {};
    for (const key of Object.keys(value)) {
      const item = value[key];
      out[key] = pyToJson(item === undefined ? null : item);
    }
    return out;
  }
  return null;
};

export type PyArgument =
  | { readonly ok: true; readonly value: PyValue }
  | { readonly ok: false; readonly message: string };

/** CPython's `str(JSONDecodeError)`, when U13's scanner produced one. */
const decodeMessage = (failure: { readonly _tag: string; readonly message?: string }): string | null =>
  (failure._tag === 'decode' || failure._tag === 'constant') && failure.message !== undefined
    ? failure.message
    : null;

/**
 * `json.loads` for a CLI argument — `--action`'s object.
 *
 * Two things it gets from U13's scanner that `JSON.parse` cannot give:
 *
 * - the **value** keeps its Python numeric types, so `--action '{"w": 1.0}'`
 *   puts `"w":1.0` on the wire rather than `"w":1`, exactly as CPython's
 *   `json.dumps(json.loads(...))` does;
 * - the **message** is `str(json.JSONDecodeError)` — `Expecting value: line 1
 *   column 2 (char 1)` — which is the tail `--action must be valid JSON: …`
 *   carries in CPython and used to be V8's own sentence (NOTES §11.3).
 *
 * `JSON.parse` stays the *verdict*: the CPython message is reported only when
 * the host parser refuses the text too, so a disagreement between the two
 * scanners can never turn a valid action into a refusal.
 */
export const parsePyArgument = (text: string): PyArgument => {
  const parsed = parsePython(text);
  if (parsed.failure === null) return { ok: true, value: parsed.value };
  try {
    return { ok: true, value: jsonToPy(JSON.parse(text) as unknown) };
  } catch (cause) {
    return {
      ok: false,
      message:
        decodeMessage(parsed.failure) ??
        (cause instanceof Error ? cause.message : String(cause)),
    };
  }
};

/** The response-body form: the caller has already proved the text is JSON. */
export const parsePyText = (text: string): PyValue => {
  const parsed = parsePython(text);
  // `parsePython` gives up past its own depth cap where CPython's scanner
  // would still answer.  Falling back keeps the *value* right; only the
  // int/float spelling is lost, on a document no supervisor sends.
  return parsed.failure === null ? parsed.value : jsonToPy(JSON.parse(text) as unknown);
};

/** Narrow a `JSON.parse` result into the model, without a cast. */
const jsonToPy = (value: unknown): PyValue => {
  if (value === null || value === undefined) return null;
  if (typeof value === 'boolean' || typeof value === 'number' || typeof value === 'string') {
    return value;
  }
  if (Array.isArray(value)) return value.map((item: unknown) => jsonToPy(item));
  if (typeof value === 'object') {
    const out: Record<string, PyValue> = {};
    for (const [key, item] of Object.entries(value)) out[key] = jsonToPy(item);
    return out;
  }
  return null;
};

// ---------------------------------------------------------------------------
// The transport
// ---------------------------------------------------------------------------

export interface V1RequestOptions {
  readonly token?: string | undefined;
  readonly body?: PyValue | undefined;
  /** Seconds. CPython's default is 60. */
  readonly timeout?: number;
}

export interface V1JsonApi {
  /**
   * `request_json` (client.py:225-243), returning the body with its Python
   * numeric types intact.  Non-2xx raises core's `v1ErrorMessage` sentence.
   */
  readonly requestPyJson: (
    method: string,
    url: string,
    options?: V1RequestOptions
  ) => Effect.Effect<PyObject, PlayerError>;
}

export class V1Json extends Context.Tag('V1Json')<V1Json, V1JsonApi>() {}

const DEFAULT_TIMEOUT_S = 60;

/** `urlunsplit((scheme, netloc, "", "", ""))`, for the unreachable sentence. */
const originOf = (url: string): string => {
  try {
    const parsed = new URL(url);
    return `${parsed.protocol}//${parsed.host}`;
  } catch {
    return url;
  }
};

/** The same sentence `src/services/http.ts` raises, deliberately verbatim. */
const unreachable = (url: string, reason: string): PlayerError =>
  playerError(
    `cannot reach the Freeciv supervisor at ${originOf(url)}: ${reason}. ` +
      'Stop and tell the user; do not retry in a loop.'
  );

const invalidResponse = (cause: unknown): PlayerError =>
  playerError(
    `invalid supervisor response: ${cause instanceof Error ? cause.message : String(cause)}`
  );

export const v1JsonFor = (fetchImpl: typeof fetch): V1JsonApi => ({
  requestPyJson: (method, url, options = {}) =>
    Effect.gen(function* () {
      const data = options.body === undefined ? null : pyDumps(options.body, true);
      const headers: Record<string, string> = { Accept: 'application/json' };
      if (data !== null) headers['Content-Type'] = 'application/json';
      if (options.token !== undefined && options.token !== '') {
        headers['Authorization'] = `Bearer ${options.token}`;
      }
      const timeoutMillis = (options.timeout ?? DEFAULT_TIMEOUT_S) * 1000;

      const response = yield* Effect.tryPromise({
        try: (signal) =>
          fetchImpl(url, {
            method,
            headers,
            ...(data === null ? {} : { body: data }),
            // A redirect is a refusal, not a hop: the bearer credential stays
            // on the exact configured supervisor origin.
            redirect: 'error',
            signal,
          }),
        catch: (cause) => unreachable(url, cause instanceof Error ? cause.message : String(cause)),
      }).pipe(
        Effect.timeoutFail({
          duration: `${timeoutMillis} millis`,
          onTimeout: () => unreachable(url, `timed out after ${timeoutMillis / 1000}s`),
        })
      );

      const text = yield* Effect.tryPromise({
        try: () => response.text(),
        catch: invalidResponse,
      });
      // `JSON.parse` is still the syntax verdict — its message is the one the
      // refusal carries — and `parsePython` supplies the value.
      yield* Effect.try({ try: () => JSON.parse(text) as unknown, catch: invalidResponse });
      const value = parsePyText(text);
      if (!isPyMapping(value)) {
        return yield* Effect.fail(
          playerError('the supervisor returned a non-object JSON response')
        );
      }
      if (response.status >= 200 && response.status < 300) return value;
      const plain = pyToJson(value);
      const body: JsonObject = isJsonObject(plain) ? plain : {};
      return yield* Effect.fail(playerError(v1ErrorMessage(response.status, body)));
    }),
});

/**
 * The live transport, over the global `fetch` behind the private-CA wrapper —
 * v1 requests (`join`, `next`, `act`, `result`) reach the same supervisor
 * behind the same certificate as v2's `HttpLive` (NOTES §I.3.1).
 *
 * `Layer.sync`, not `Layer.succeed`: reading `fetch` at module-evaluation time
 * would freeze whatever was global then.
 */
export const V1JsonLive: Layer.Layer<V1Json> = Layer.sync(V1Json, () =>
  v1JsonFor(caTrustedFetch(fetch))
);

/** Inject a fake transport, the same seam `httpLayer(fake)` gives core. */
export const v1JsonLayer = (fetchImpl: typeof fetch): Layer.Layer<V1Json> =>
  Layer.succeed(V1Json, v1JsonFor(fetchImpl));

/**
 * The ambient transport if one was provided, otherwise the live one.
 *
 * `Effect.serviceOption` does **not** add the tag to the requirement channel,
 * so a command built on this needs no new entry in `cli-main`'s Layer stack
 * while a test can still hand it a fake — the same "correct default, better
 * when the real thing lands" shape as PORT_MAP §4.6's two inversion seams.
 */
export const v1Json: Effect.Effect<V1JsonApi> = Effect.map(
  Effect.serviceOption(V1Json),
  Option.getOrElse((): V1JsonApi => v1JsonFor(caTrustedFetch(fetch)))
);
