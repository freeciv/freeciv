/**
 * Required-ness is part of the contract, so it gets a second copy.
 *
 * ## The hole this closes
 *
 * `Schema.optional(X)` and `X` decode every fixture in the corpus identically,
 * because a captured payload always carries the field.  So "every archived
 * fixture decodes", every round-trip test and every `onExcessProperty` audit
 * pass whether a field is demanded or not; 18 of 22 randomly sampled
 * non-optional fields could be wrapped in `Schema.optional` with `bun test`,
 * `bun run typecheck` and `bun run lint` all green.  Only the ten hand-written
 * negatives in `test/gateway/manifest.test.ts` ever exercised a missing key,
 * and they cover ten fields out of ~800.
 *
 * ## Two tests, two different claims
 *
 * 1. **The shape matches the snapshot** (`test/schema-shape-fixture.ts`).
 *    Catches a field that silently became optional — or required.  It is a
 *    snapshot, so it proves change, not correctness.
 * 2. **Every field the AST calls required is actually enforced.**  For each
 *    struct reachable from the barrels, decoding an object with that one key
 *    deleted must fail.  This is the half a snapshot cannot give you: it
 *    proves the *decoder* honours what the AST declares, so the snapshot is
 *    describing something real rather than an annotation nobody reads.
 */
import { describe, expect, test } from 'bun:test';
import { Either, Schema, SchemaAST } from 'effect';
import * as Agent from 'src/agent/index';
import * as Gateway from 'src/gateway/index';
import { TOLERANT_PARSE_OPTIONS } from 'src/tolerant';
import { SCHEMA_SHAPES } from 'test/schema-shape-fixture';

/** A schema-shaped export: anything carrying an Effect Schema AST on `.ast`. */
const isSchemaLike = (value: unknown): value is { readonly ast: SchemaAST.AST } =>
  (typeof value === 'object' || typeof value === 'function') &&
  value !== null &&
  'ast' in value &&
  typeof value.ast === 'object' &&
  value.ast !== null &&
  '_tag' in value.ast;

const astOf = (value: unknown): SchemaAST.AST | undefined =>
  isSchemaLike(value) ? value.ast : undefined;

const identifierOf = (ast: SchemaAST.AST): string | undefined => {
  const annotation = ast.annotations[SchemaAST.IdentifierAnnotationId];
  return typeof annotation === 'string' ? annotation : undefined;
};

interface Discovered {
  /** Identifier -> sorted `field` / `field?` list. */
  readonly shapes: Map<string, ReadonlyArray<string>>;
  /** Identifier -> the TypeLiteral it was read from, for the enforcement test. */
  readonly nodes: Map<string, SchemaAST.TypeLiteral>;
}

const walk = (
  ast: SchemaAST.AST,
  label: string,
  seen: Set<SchemaAST.AST>,
  found: Discovered,
): void => {
  if (seen.has(ast)) return;
  seen.add(ast);
  const name = identifierOf(ast) ?? label;

  if (SchemaAST.isTypeLiteral(ast)) {
    const fields = ast.propertySignatures
      .map((property) => `${String(property.name)}${property.isOptional ? '?' : ''}`)
      .toSorted();
    if (fields.length > 0 && !found.shapes.has(name)) {
      found.shapes.set(name, fields);
      found.nodes.set(name, ast);
    }
    for (const property of ast.propertySignatures) {
      walk(property.type, `${name}.${String(property.name)}`, seen, found);
    }
    for (const record of ast.indexSignatures) walk(record.type, `${name}[]`, seen, found);
    return;
  }
  if (SchemaAST.isUnion(ast)) {
    ast.types.forEach((member, index) => walk(member, `${name}|${String(index)}`, seen, found));
    return;
  }
  if (SchemaAST.isTupleType(ast)) {
    ast.elements.forEach((element, index) =>
      walk(element.type, `${name}[${String(index)}]`, seen, found),
    );
    for (const rest of ast.rest) walk(rest.type, `${name}[]`, seen, found);
    return;
  }
  if (SchemaAST.isRefinement(ast)) return walk(ast.from, name, seen, found);
  if (SchemaAST.isTransformation(ast)) return walk(ast.to, name, seen, found);
  if (SchemaAST.isSuspend(ast)) return walk(ast.f(), name, seen, found);
};

const discover = (): Discovered => {
  const found: Discovered = { shapes: new Map(), nodes: new Map() };
  for (const [namespace, barrel] of [
    ['Agent', Agent],
    ['Gateway', Gateway],
  ] as const) {
    for (const [name, value] of Object.entries(barrel)) {
      const ast = astOf(value);
      if (ast !== undefined) walk(ast, `${namespace}.${name}`, new Set(), found);
    }
  }
  return found;
};

const { shapes, nodes } = discover();

const actual: ReadonlyArray<readonly [string, ReadonlyArray<string>]> = [...shapes.entries()]
  .toSorted(([left], [right]) => (left < right ? -1 : left > right ? 1 : 0));

// ---------------------------------------------------------------------------
// The one rule the package barrel states categorically
// ---------------------------------------------------------------------------

/**
 * `src/index.ts` says it flatly: "a decoded `Gateway` integer is a `bigint` (a
 * Python `int`, which is what `canonicalText` needs to spell it without a
 * `.0`), while a decoded `Agent` integer is a `number`".
 *
 * It was false for two of the six gateway modules.  `identity.ts` and
 * `archive.ts` used `Schema.Int`, which decodes to a JS `number`, for `pid`,
 * `port`, `protocol_version`, `schema_version`, `turn`, `year` and the PPM
 * player ids — and since `/health` and every archive body are written by
 * CPython through `_canonical`, a consumer that decoded and re-canonicalized
 * got `"pid":77917.0` where Python wrote `"pid":77917`.  The package's own test
 * pinned that as expected rather than fixing it.
 *
 * So: no `Schema.Int` anywhere under the `Gateway` barrel, with a short list of
 * named exceptions that are not canonical-JSON body fields at all.
 */
const isIntRefinement = (ast: SchemaAST.AST): boolean => {
  if (!SchemaAST.isRefinement(ast)) return false;
  const title = ast.annotations[SchemaAST.TitleAnnotationId];
  if (title === 'int' && ast.from._tag === 'NumberKeyword') return true;
  return isIntRefinement(ast.from);
};

/**
 * Values that are `Schema.Int` on purpose, because they never travel as a field
 * inside a canonicalized JSON body.
 */
const INT_EXCEPTIONS: ReadonlySet<string> = new Set([
  // Query parameters. Parsed from a URL query string, never emitted in a body
  // — `replay.ts` says so where they are declared.
  'ReplayQuery.after_turn',
  'ReplayQuery.limit',
  'BoardQuery.turn',
  // A frame index is a path segment and an array position, not a body field;
  // `../ids.ts` keeps it a `number` deliberately.
  'FrameIndex',
]);

const intFields = (): ReadonlyArray<string> => {
  const offenders: string[] = [];
  const seen = new Set<SchemaAST.AST>();

  const scan = (ast: SchemaAST.AST, label: string): void => {
    if (seen.has(ast)) return;
    seen.add(ast);
    const name = identifierOf(ast) ?? label;
    if (INT_EXCEPTIONS.has(name)) return;
    if (isIntRefinement(ast)) {
      offenders.push(name);
      return;
    }
    if (SchemaAST.isTypeLiteral(ast)) {
      for (const property of ast.propertySignatures) {
        scan(property.type, `${identifierOf(ast) ?? label}.${String(property.name)}`);
      }
      for (const record of ast.indexSignatures) scan(record.type, `${name}[]`);
      return;
    }
    if (SchemaAST.isUnion(ast)) {
      ast.types.forEach((member, index) => scan(member, `${name}|${String(index)}`));
      return;
    }
    if (SchemaAST.isTupleType(ast)) {
      ast.elements.forEach((element, index) => scan(element.type, `${name}[${String(index)}]`));
      for (const rest of ast.rest) scan(rest.type, `${name}[]`);
      return;
    }
    if (SchemaAST.isTransformation(ast)) return scan(ast.to, name);
    if (SchemaAST.isSuspend(ast)) return scan(ast.f(), name);
  };

  for (const [name, value] of Object.entries(Gateway)) {
    const ast = astOf(value);
    if (ast !== undefined) scan(ast, `Gateway.${name}`);
  }
  return [...new Set(offenders)].toSorted();
};

describe('schema shape / a decoded Gateway integer is a bigint', () => {
  test('the scan can actually see a Schema.Int', () => {
    // Without this the assertion below would pass if `isIntRefinement` broke.
    expect(isIntRefinement(Schema.Int.ast)).toBe(true);
    expect(isIntRefinement(Schema.Int.pipe(Schema.nonNegative()).ast)).toBe(true);
    expect(isIntRefinement(Schema.Number.ast)).toBe(false);
  });

  test('no Gateway body field decodes a Python int to a JS number', () => {
    expect(intFields()).toEqual([]);
  });
});

describe('schema shape / the snapshot', () => {
  test('the walk found the structs it is supposed to find', () => {
    // Without this, breaking the walker would make the comparison trivially true.
    expect(actual.length).toBeGreaterThan(80);
    expect(actual.reduce((sum, [, fields]) => sum + fields.length, 0)).toBeGreaterThan(600);
  });

  test('every struct and its optionality matches test/schema-shape-fixture.ts', () => {
    expect(actual).toEqual(SCHEMA_SHAPES.map(([name, fields]) => [name, fields]));
  });
});

// ---------------------------------------------------------------------------
// The AST says required — does the decoder agree?
// ---------------------------------------------------------------------------

/** A value that satisfies `ast` well enough to reach the missing-key check. */
const sampleFor = (ast: SchemaAST.AST): unknown => {
  if (SchemaAST.isRefinement(ast)) return sampleFor(ast.from);
  if (SchemaAST.isTransformation(ast)) return sampleFor(ast.from);
  if (SchemaAST.isSuspend(ast)) return sampleFor(ast.f());
  if (SchemaAST.isLiteral(ast)) return ast.literal;
  if (SchemaAST.isUnion(ast)) return sampleFor(ast.types[0] ?? ast);
  if (SchemaAST.isTupleType(ast)) return [];
  if (SchemaAST.isTypeLiteral(ast)) {
    return sampleObject(ast);
  }
  if (ast._tag === 'StringKeyword') return '';
  if (ast._tag === 'NumberKeyword') return 0;
  if (ast._tag === 'BooleanKeyword') return false;
  if (ast._tag === 'BigIntKeyword') return 0n;
  return null;
};

/** {@link sampleFor} for a struct, typed so callers need no cast to index it. */
const sampleObject = (ast: SchemaAST.TypeLiteral): Record<string, unknown> =>
  Object.fromEntries(
    ast.propertySignatures
      .filter((property) => !property.isOptional)
      .map((property) => [String(property.name), sampleFor(property.type)]),
  );

/**
 * Structs whose required fields cannot be probed this way, with the reason.
 *
 * A generic sample cannot satisfy a cross-field `Schema.filter` (a receipt
 * whose error must name its own revision, a page whose cursor must agree with
 * `catalog_complete`), so deleting a key from such a sample fails for the
 * wrong reason — which is still a failure, so it does not weaken the test;
 * these are skipped only where the *baseline* sample already fails, which
 * would make the check vacuous rather than wrong.
 */
const probeable = [...nodes.entries()].filter(([, ast]) => {
  const schema = Schema.make(ast);
  const baseline = Schema.decodeUnknownEither(schema, TOLERANT_PARSE_OPTIONS)(sampleFor(ast));
  return Either.isRight(baseline);
});

describe('schema shape / required means required', () => {
  test('enough structs are probeable for this to mean something', () => {
    expect(probeable.length).toBeGreaterThan(40);
  });

  test('deleting any required key makes the decode fail', () => {
    const accepted = probeable.flatMap(([name, ast]) => {
      const decode = Schema.decodeUnknownEither(Schema.make(ast), TOLERANT_PARSE_OPTIONS);
      const sample = sampleObject(ast);
      return ast.propertySignatures
        .filter((property) => !property.isOptional)
        .flatMap((property) => {
          const key = String(property.name);
          const { [key]: _removed, ...without } = sample;
          return Either.isRight(decode(without)) ? [`${name}.${key}`] : [];
        });
    });
    expect(accepted).toEqual([]);
  });
});
