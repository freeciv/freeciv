/**
 * How a JSON number is spelled on the wire.
 *
 * This is the type-level half of `./canon.ts`'s central rule: a Python `int` is
 * a `bigint` here and a Python `float` is a `number`, because that is the only
 * way `1` and `1.0` survive a decode/encode round trip distinctly.  Get it
 * wrong in a schema and nothing fails locally — the document decodes, the
 * fields read correctly, and then a digest taken over the re-encoded value
 * disagrees with Python's somewhere far away.
 *
 * Three gateway modules reached these four definitions independently and
 * arrived at the same place; `src/gateway/replay.ts` said so in a comment
 * ("the two must be merged into one shared module rather than allowed to
 * drift") and this module is that merge.  They are here rather than in
 * `./json.ts` because `./json.ts` models *untyped* JSON — a document whose
 * shape is not known — while these are the building blocks of documents whose
 * shape is.
 *
 * ## A deliberate namesake
 *
 * `src/agent/primitives.ts` also exports a `WireInt`, and it is **not** this
 * one: the agent protocol's integers are counters the port does arithmetic on,
 * so they stay `number`-spelled and cross into `bigint` at the hashing
 * boundary (see `revisionCanonRecord` in `src/agent/revision.ts`).  The two
 * live in different barrels — `Wire.WireInt` here, `Wire.Agent.WireInt` there
 * — and the package barrel never flattens them together.  If you are reading a
 * decoded value and it is a `number` where you expected a `bigint`, this is
 * the paragraph you were looking for.
 */
import { Schema } from 'effect';

/**
 * A JSON number CPython wrote with `int`.
 *
 * Decodes to `bigint` so `canonicalText` spells it without a fractional part,
 * and encodes back to a JavaScript number so a re-encoded document is still
 * ordinary JSON.  A fractional literal is refused, which is the verdict
 * `int(value) != value` reaches on the Python side (`supervisor.py:426`); the
 * gateway's own `_public_int` (`replay_gateway.py:337`) defaults rather than
 * rounds such a value, so decoding one means the payload did not come from the
 * gateway.
 *
 * The safe-integer bound comes from `Schema.BigIntFromNumber` and is tighter
 * than Python's unbounded `int`.  No field modelled in this package can reach
 * it: turn numbers cap at 5000, scores at four digits, epoch seconds at ten.
 * The one place the protocol *does* carry an integer past 2^53 —
 * `14695981039346656037`, the FNV offset basis inside the native schema id —
 * never travels as a JSON number; it is a `bigint` literal in `./fnv1a64.ts`.
 */
export const WireInt: Schema.Schema<bigint, number> = Schema.BigIntFromNumber.annotations({
  identifier: 'WireInt',
  description: 'A Python int: decodes to bigint so the canonical writer omits the ".0"',
});
/** A JSON number CPython wrote with `int`. */
export type WireInt = typeof WireInt.Type;

/**
 * A {@link WireInt} the producer guarantees is `>= 0`.
 *
 * `_public_int(value, default=0, minimum=0)` (`replay_gateway.py:337-343`)
 * clamps with `max(minimum, int(value))`, so every count, turn and total the
 * gateway publishes is non-negative by construction.
 */
export const WireNonNegativeInt: Schema.Schema<bigint, number> = WireInt.pipe(
  Schema.nonNegativeBigInt(),
).annotations({
  identifier: 'WireNonNegativeInt',
  description: 'A Python int the producer clamps to >= 0',
});
/** A {@link WireInt} the producer guarantees is `>= 0`. */
export type WireNonNegativeInt = typeof WireNonNegativeInt.Type;

/**
 * A JSON number CPython wrote with `float` — `created_at`, `action_timeout_s`,
 * `lobby_timeout_s`, `mean_latency_ms` when there were decisions.
 *
 * Stays a JavaScript `number`, which is what the canonical writer reads as
 * "spell this the way `repr(float)` would": `600` decoded here is re-emitted as
 * `600.0`, matching `_public_number` (`replay_gateway.py:348`), which coerces
 * with `float(value)` precisely so the wire never shows a bare `600`.
 *
 * `Schema.JsonNumber` rather than `Schema.Number`: `NaN` and `Infinity` are
 * unreachable through `JSON.parse` but reachable through a hand-built value,
 * and a non-finite float has no JSON spelling at all (`allow_nan=False`).
 */
export const WireFloat: Schema.Schema<number> = Schema.JsonNumber.annotations({
  identifier: 'WireFloat',
  description: 'A Python float: stays a JS number so the canonical writer keeps the ".0"',
});
/** A JSON number CPython wrote with `float`. */
export type WireFloat = typeof WireFloat.Type;

/**
 * A field Python spells as an `int` *or* a `float` depending on which branch
 * produced it.  Tried int-first, so `0` decodes to `0n` and `0.5` to `0.5`.
 *
 * The clearest instance is {@link import('./gateway/replay.ts').Technology}'s
 * `cost_base`, which the current catalog builder hardcodes to the int `0`
 * (`save_replay.py:322-333`) while older cached catalogs carry a float such as
 * `4303.405628104328`.  Both spellings are in the fixture corpus.
 *
 * This is the one place the round trip can lose information: an integral float
 * (`1500.0`) is indistinguishable from an int once `JSON.parse` has run, so it
 * decodes to `1500n` and re-encodes as `1500`.  That ambiguity is the JSON
 * reader's, not this schema's — nothing downstream can recover it from the
 * number alone, so the use sites document how a byte-exact port recovers the
 * intended spelling from a *sibling* field instead.
 */
export const WireNumber: Schema.Schema<bigint | number, number> = Schema.Union(
  WireInt,
  WireFloat,
).annotations({
  identifier: 'WireNumber',
  description: 'A field that is a Python int in one branch and a float in another',
});
/** A field Python spells as an `int` in one branch and a `float` in another. */
export type WireNumber = typeof WireNumber.Type;
