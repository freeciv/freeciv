/**
 * FNV-1a, 64-bit — the protocol's cheap content digest.
 *
 * `v2_control` uses it for the digests that ride on observations
 * (`_research_choices_digest`, `_known_techs_digest`,
 * `_diplomacy_clauses_digest`), and the schema id itself describes the
 * algorithm by its constants:
 *
 * ```python
 * {"algorithm": "FNV-1a-64", "offset_basis": 14695981039346656037,
 *  "prime": 1099511628211, "text_format": "fnv1a64-%016x"}
 * ```
 *
 * Both constants exceed `Number.MAX_SAFE_INTEGER` and the multiply overflows
 * 64 bits on every byte, so the state is a `bigint` masked to 64 bits — the
 * `Number`/`>>>` route cannot represent the intermediate product at all.
 */
import { Either, Option } from 'effect';
import { encodeUtf8, type LoneSurrogateError } from './canon.ts';

/** `2**64 - 1`, the modulus every step folds back into. */
export const U64_MASK = 0xffff_ffff_ffff_ffffn;

/** FNV-1a-64 offset basis: `0xcbf29ce484222325`. */
export const FNV1A64_OFFSET_BASIS = 14695981039346656037n;

/** FNV-1a-64 prime: `0x100000001b3`. */
export const FNV1A64_PRIME = 1099511628211n;

/**
 * Fold more bytes into a running digest.
 *
 * Exported so the framed digests can be built the way Python builds them —
 * `update(id.to_bytes(4, "big")); update(len(name)...); update(name)` — without
 * first concatenating the whole record.
 */
export const fnv1a64Update = (digest: bigint, bytes: Uint8Array): bigint =>
  bytes.reduce<bigint>(
    (accumulator, byte) => ((accumulator ^ BigInt(byte)) * FNV1A64_PRIME) & U64_MASK,
    digest & U64_MASK,
  );

/** The digest of a byte string, starting from the offset basis. */
export const fnv1a64 = (bytes: Uint8Array): bigint =>
  fnv1a64Update(FNV1A64_OFFSET_BASIS, bytes);

/**
 * The digest of `text.encode("utf-8", "strict")`.
 *
 * An unpaired surrogate is an error rather than a digest over U+FFFD: Python
 * raises there, so a value that silently hashed would diverge invisibly.
 */
export const fnv1a64Utf8 = (text: string): Either.Either<bigint, LoneSurrogateError> =>
  Either.map(encodeUtf8(text), fnv1a64);

/** `f"fnv1a64-{digest:016x}"` — the on-the-wire spelling. */
export const formatFnv1a64 = (digest: bigint): string =>
  `fnv1a64-${(digest & U64_MASK).toString(16).padStart(16, '0')}`;

/** `_FNV1A64_DIGEST` (v2_control.py:191) — what a peer is allowed to send. */
export const FNV1A64_TEXT_PATTERN = /^fnv1a64-[0-9a-f]{16}$/;

/** The digest a well-formed `fnv1a64-…` string denotes, if it is well formed. */
export const parseFnv1a64 = (text: string): Option.Option<bigint> =>
  FNV1A64_TEXT_PATTERN.test(text)
    ? Option.some(BigInt(`0x${text.slice('fnv1a64-'.length)}`))
    : Option.none();
