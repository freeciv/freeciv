/**
 * Tolerant decoding — the single decode entry point for the whole port.
 *
 * ## The convention every wire schema file follows
 *
 * A wire schema module exports, for each packet, exactly three things under
 * one name plus one `decode` prefix:
 *
 * ```ts
 * // src/gateway/games.ts
 * export const GameRow = Schema.Struct({ game_id: GameId, state: RunState })
 * export type GameRow = typeof GameRow.Type
 * export const decodeGameRow = decodeTolerant(GameRow, 'GameRow')
 * ```
 *
 * - the `Schema.Struct` (same name as the packet, PascalCase),
 * - the derived type (`typeof X.Type`, never hand-written — the schema is the
 *   single source of truth),
 * - `decodeX`, built from {@link decodeTolerant} and nothing else.
 *
 * No wire module may call `Schema.decodeUnknownSync`, `Schema.decodeSync`, or
 * a bare `Schema.decodeUnknownEither` of its own: those default to
 * `onExcessProperty: "ignore"`, which silently deletes fields the server added
 * after this client was built, and `Sync` variants throw instead of returning
 * an error value.
 *
 * ## Why "tolerant"
 *
 * The gateway is an evolving Python service (`agent_eval/replay_gateway.py`).
 * It adds fields to its JSON payloads without bumping `schema_version`.  Two
 * failure modes have to be impossible:
 *
 * 1. **A new server field must never break a consumer.**  Excess properties
 *    are accepted, not rejected.
 * 2. **A new server field must never be *lost* by a middleman.**  A proxy that
 *    decodes a payload, touches one field, and re-encodes it must emit the
 *    unknown fields it never understood — byte-identically, key order and all.
 *    This is what {@link TOLERANT_PARSE_OPTIONS} buys with
 *    `onExcessProperty: "preserve"` plus `propertyOrder: "original"`, and it
 *    is proven by the round-trip tests in `test/tolerant.test.ts`.
 *
 * Two things a round trip does not preserve, both inherited from the JSON
 * layer rather than chosen here, both pinned by tests: number *spelling*
 * (`JSON.parse` canonicalizes `1.7e9` to `1700000000`, exactly as Python's
 * `json` does), and a literal `"__proto__"` key, which `Schema` drops while
 * rebuilding the object.  See the `./json.ts` module doc.
 *
 * Note that preservation is a *runtime* property: the decoded value carries
 * the extra keys, but the static type does not name them (it cannot — they
 * are unknown by definition).  Reach for {@link encodeTolerant} to re-encode;
 * a plain `Schema.encodeEither` drops everything the type does not name.
 *
 * ## Errors are values
 *
 * `decodeTolerant` returns `Either<A, WireDecodeError>`.  Nothing here throws.
 * `Either` is itself an `Effect`, so a decoder composes straight into an
 * Effect program without a lift:
 *
 * ```ts
 * const row = yield* decodeGameRow(payload) // fails with WireDecodeError
 * ```
 *
 * @module
 */

import { Data, Either, Option, ParseResult, Schema, SchemaAST } from 'effect';

/**
 * Parse options shared by every decoder and encoder in `@arena/wire`.
 *
 * - `onExcessProperty: "preserve"` — unknown server fields survive decode
 *   *and* re-encode instead of being dropped.
 * - `errors: "all"` — report every issue in a bad payload, not just the first,
 *   so one round trip against a drifted server tells the whole story.
 * - `propertyOrder: "original"` — output keys keep the order they arrived in,
 *   which makes `JSON.stringify(encode(decode(JSON.parse(text))))` identical
 *   to `JSON.stringify(JSON.parse(text))` for any payload this package
 *   accepts.
 */
export const TOLERANT_PARSE_OPTIONS: SchemaAST.ParseOptions = {
  onExcessProperty: 'preserve',
  errors: 'all',
  propertyOrder: 'original',
};

/** One structured complaint from a failed decode or encode. */
export interface WireIssue {
  /** `ParseResult` issue tag, e.g. `"Type"`, `"Missing"`, `"Refinement"`. */
  readonly kind: string;
  /** Path from the payload root to the offending value, e.g. `["games", "0", "state"]`. */
  readonly path: ReadonlyArray<string>;
  /** Human-readable explanation for this one path. */
  readonly message: string;
}

const issuesOf = (parseError: ParseResult.ParseError): ReadonlyArray<WireIssue> =>
  ParseResult.ArrayFormatter.formatErrorSync(parseError).map((issue) => ({
    kind: issue._tag,
    path: issue.path.map(String),
    message: issue.message,
  }));

/** Render an issue path as a JSON-pointer-ish string; `"<root>"` when empty. */
export const formatIssuePath = (path: ReadonlyArray<string>): string =>
  path.length === 0 ? '<root>' : path.join('.');

/**
 * A payload did not match its schema.  Carries the formatted tree (`message`)
 * and the flat issue list (`issues`) so callers can log one and branch on the
 * other without re-running the parser.
 */
export class WireDecodeError extends Data.TaggedError('WireDecodeError')<{
  /** Identifier of the schema that rejected the payload. */
  readonly schemaName: string;
  /** `ParseResult.TreeFormatter` rendering of every issue. */
  readonly message: string;
  /** Flat, machine-readable form of the same issues. */
  readonly issues: ReadonlyArray<WireIssue>;
}> {}

/**
 * A value could not be encoded back to its wire form.  Distinct tag from
 * {@link WireDecodeError} so a `switch` over `_tag` stays exhaustive and a
 * proxy can tell "the server sent junk" from "we built junk".
 */
export class WireEncodeError extends Data.TaggedError('WireEncodeError')<{
  readonly schemaName: string;
  readonly message: string;
  readonly issues: ReadonlyArray<WireIssue>;
}> {}

/**
 * Best available name for a schema: the caller's label, else the schema's
 * `identifier` annotation, else its structural rendering.
 */
export const schemaLabel = <A, I, R>(
  schema: Schema.Schema<A, I, R>,
  schemaName?: string,
): string =>
  schemaName ??
  Option.getOrElse(SchemaAST.getIdentifierAnnotation(schema.ast), () => schema.ast.toString());

/** A decoder produced by {@link decodeTolerant}. */
export type TolerantDecoder<A> = (input: unknown) => Either.Either<A, WireDecodeError>;

/** An encoder produced by {@link encodeTolerant}. */
export type TolerantEncoder<A, I> = (value: A) => Either.Either<I, WireEncodeError>;

/**
 * Build the decoder for a wire schema.  **Every `decodeX` in this package is
 * built here**; see the module doc for the convention.
 *
 * Unknown properties in `input` are preserved on the result (runtime only —
 * the static type names only what the schema names), and failures come back
 * as a {@link WireDecodeError} value rather than a thrown exception.
 *
 * @param schema - the packet schema; must have no unresolved requirements
 *   (`R = never`) because decoding is synchronous.
 * @param schemaName - overrides the schema's `identifier` annotation in errors.
 */
export const decodeTolerant = <A, I>(
  schema: Schema.Schema<A, I>,
  schemaName?: string,
): TolerantDecoder<A> => {
  const decode = Schema.decodeUnknownEither(schema, TOLERANT_PARSE_OPTIONS);
  const name = schemaLabel(schema, schemaName);
  return (input) =>
    Either.mapLeft(
      decode(input),
      (parseError) =>
        new WireDecodeError({
          schemaName: name,
          message: ParseResult.TreeFormatter.formatErrorSync(parseError),
          issues: issuesOf(parseError),
        }),
    );
};

/**
 * Build the encoder that mirrors {@link decodeTolerant}: unknown properties
 * carried on a decoded value are written back out, in their original order.
 *
 * Use this — not `Schema.encodeEither` — anywhere a payload that came off the
 * wire is being sent back out, or the server's additive fields die in transit.
 */
export const encodeTolerant = <A, I>(
  schema: Schema.Schema<A, I>,
  schemaName?: string,
): TolerantEncoder<A, I> => {
  const encode = Schema.encodeEither(schema, TOLERANT_PARSE_OPTIONS);
  const name = schemaLabel(schema, schemaName);
  return (value) =>
    Either.mapLeft(
      encode(value),
      (parseError) =>
        new WireEncodeError({
          schemaName: name,
          message: ParseResult.TreeFormatter.formatErrorSync(parseError),
          issues: issuesOf(parseError),
        }),
    );
};

/**
 * Refinement built from a schema, for the cases where a boolean is genuinely
 * what is wanted (guards, `filter` predicates).
 *
 * **It asks about the decoded value, not the wire value.**  `Schema.is` tests
 * the `Type` side, so for a schema with no transformation — where `Type` and
 * `Encoded` are the same — this agrees with {@link decodeTolerant} on the same
 * input.  For a schema that *transforms*, the two answer different questions
 * and they are supposed to: a `Gateway` body carries `WireInt` fields, whose
 * `Encoded` side is `number` and whose `Type` side is `bigint`, so
 * `isTolerant(GatewayIdentity)(rawJson)` is `false` while
 * `decodeTolerant(GatewayIdentity)(rawJson)` is a `Right`.
 *
 * Use this to guard a value you already decoded.  To ask "would this JSON
 * decode?", use {@link decodeTolerant} and test the `Either`.
 * `test/tolerant.test.ts` pins both halves of that distinction.
 */
export const isTolerant = <A, I>(
  schema: Schema.Schema<A, I>,
): ((input: unknown) => input is A) => Schema.is(schema, TOLERANT_PARSE_OPTIONS);
