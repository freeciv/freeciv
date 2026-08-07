/**
 * The JSON escape hatch, and the one place JSON is serialized.
 *
 * Ports `_print_json` (client.py:6127-6128), `_print_v2_json` (3073-3074),
 * `V2_JSON_ONLY_COMMANDS` / `_json_environment` / `_json_requested`
 * (3077-3098).
 *
 * `JSON.stringify` is not usable on its own: Python's `json.dumps` defaults to
 * `ensure_ascii=True`, so every non-ASCII character leaves as `\uXXXX`.  A
 * single accented city name would otherwise diverge on every byte-diff, so the
 * serializer below reproduces CPython's escaping, key ordering and separators
 * exactly.  See NOTES.md §2 for the one case it cannot reproduce.
 */
import { Console, Effect } from 'effect';
import { V2_JSON_ENVIRONMENT, V2_JSON_ONLY_COMMANDS, V2_TRUE_STRINGS } from 'src/constants';

// ---------------------------------------------------------------------------
// CPython-compatible json.dumps
// ---------------------------------------------------------------------------

export interface DumpsOptions {
  /** `indent=N`. Omitted (or `null`) means CPython's single-line default. */
  readonly indent?: number | null;
  /** `sort_keys`. Defaults to `false`, like CPython. */
  readonly sortKeys?: boolean;
  /** `separators=(item, key)`. Defaults follow CPython's indent-aware rule. */
  readonly separators?: readonly [string, string];
  /** `ensure_ascii`. Defaults to `true`, like CPython. */
  readonly ensureAscii?: boolean;
}

const ESCAPES: ReadonlyMap<string, string> = new Map([
  ['\\', '\\\\'],
  ['"', '\\"'],
  ['\b', '\\b'],
  ['\f', '\\f'],
  ['\n', '\\n'],
  ['\r', '\\r'],
  ['\t', '\\t'],
]);

const unicodeEscape = (code: number): string => `\\u${code.toString(16).padStart(4, '0')}`;

/** `json.encoder.py_encode_basestring_ascii` (and its non-ASCII sibling). */
export const encodeStringAscii = (value: string, ensureAscii = true): string => {
  let out = '"';
  for (const character of [...value]) {
    const point = character.codePointAt(0) ?? 0;
    const known = ESCAPES.get(character);
    if (known !== undefined) {
      out += known;
    } else if (point < 0x20) {
      out += unicodeEscape(point);
    } else if (point < 0x7f || !ensureAscii) {
      out += character;
    } else if (point <= 0xffff) {
      out += unicodeEscape(point);
    } else {
      // CPython emits an explicit surrogate pair for astral characters.
      const offset = point - 0x10000;
      out += unicodeEscape(0xd800 + (offset >> 10));
      out += unicodeEscape(0xdc00 + (offset & 0x3ff));
    }
  }
  return `${out}"`;
};

const numberText = (value: number): string => {
  if (!Number.isFinite(value)) {
    // CPython would emit `Infinity`/`NaN`; nothing validated ever reaches here.
    return value > 0 ? 'Infinity' : Number.isNaN(value) ? 'NaN' : '-Infinity';
  }
  return String(value);
};

const sortKeysAscii = (keys: ReadonlyArray<string>): ReadonlyArray<string> =>
  [...keys].sort((left, right) => (left < right ? -1 : left > right ? 1 : 0));

interface DumpContext {
  readonly itemSeparator: string;
  readonly keySeparator: string;
  readonly indent: string | null;
  readonly sortKeys: boolean;
  readonly ensureAscii: boolean;
}

const dump = (value: unknown, context: DumpContext, depth: number): string => {
  if (value === null || value === undefined) return 'null';
  if (typeof value === 'boolean') return value ? 'true' : 'false';
  if (typeof value === 'number') return numberText(value);
  if (typeof value === 'string') return encodeStringAscii(value, context.ensureAscii);
  const { indent, itemSeparator, keySeparator } = context;
  const newlineOuter = indent === null ? '' : `\n${indent.repeat(depth)}`;
  const newlineInner = indent === null ? '' : `\n${indent.repeat(depth + 1)}`;
  const separator = itemSeparator + newlineInner;
  if (Array.isArray(value)) {
    if (value.length === 0) return '[]';
    const parts = value.map((item) => dump(item, context, depth + 1));
    return `[${newlineInner}${parts.join(separator)}${newlineOuter}]`;
  }
  if (typeof value === 'object') {
    const record = value as Record<string, unknown>;
    const own = Object.keys(record);
    const keys = context.sortKeys ? sortKeysAscii(own) : own;
    if (keys.length === 0) return '{}';
    const parts = keys.map(
      (key) =>
        `${encodeStringAscii(key, context.ensureAscii)}${keySeparator}${dump(
          record[key],
          context,
          depth + 1
        )}`
    );
    return `{${newlineInner}${parts.join(separator)}${newlineOuter}}`;
  }
  return 'null';
};

/** CPython's `json.dumps`, restricted to the value domain the wire carries. */
export const pyJsonDumps = (value: unknown, options: DumpsOptions = {}): string => {
  const indentSize = options.indent ?? null;
  const indent = indentSize === null ? null : ' '.repeat(indentSize);
  const defaults: readonly [string, string] = indent === null ? [', ', ': '] : [',', ': '];
  const [itemSeparator, keySeparator] = options.separators ?? defaults;
  return dump(
    value,
    {
      itemSeparator,
      keySeparator,
      indent,
      sortKeys: options.sortKeys === true,
      ensureAscii: options.ensureAscii !== false,
    },
    0
  );
};

/** `json.dumps(value, sort_keys=True, separators=(",", ":"))`. */
export const compactJson = (value: unknown): string =>
  pyJsonDumps(value, { sortKeys: true, separators: [',', ':'] });

/** `json.dumps(value, indent=2, sort_keys=True)`. */
export const indentedJson = (value: unknown): string =>
  pyJsonDumps(value, { sortKeys: true, indent: 2 });

/**
 * `_canonical_body` (client.py:8328-8335) — the byte-stable idempotency
 * payload.  `ensure_ascii=False` is load-bearing: a city name outside ASCII
 * must hash to the same bytes on every retry, which it only does when the
 * escape policy never changes.
 */
export const canonicalJson = (value: unknown): string =>
  pyJsonDumps(value, { sortKeys: true, separators: [',', ':'], ensureAscii: false });

// ---------------------------------------------------------------------------
// The two printers
// ---------------------------------------------------------------------------

/** `_print_v2_json` — one compact, key-sorted line. */
export const printV2Json = (value: unknown): Effect.Effect<void> =>
  Console.log(compactJson(value));

/** `_print_json` — the strategic-v1 shape: two-space indent, keys sorted. */
export const printJson = (value: unknown): Effect.Effect<void> =>
  Console.log(indentedJson(value));

// ---------------------------------------------------------------------------
// _json_requested
// ---------------------------------------------------------------------------

/**
 * `PLAY_JSON=1` is exactly `--json` on every command that declares it; it can
 * never widen what a command does, only which of its two renderings is printed.
 */
export const jsonEnvironment = (
  environment: Readonly<Record<string, string | undefined>> = process.env
): boolean => V2_TRUE_STRINGS.has((environment[V2_JSON_ENVIRONMENT] ?? '').trim().toLowerCase());

/** Report whether this invocation's output must be full-fidelity JSON. */
export const jsonRequested = (
  command: string,
  flag: boolean,
  environment: Readonly<Record<string, string | undefined>> = process.env
): boolean => V2_JSON_ONLY_COMMANDS.has(command) || flag || jsonEnvironment(environment);
