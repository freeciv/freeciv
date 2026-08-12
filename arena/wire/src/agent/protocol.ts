/**
 * The full-control-v2 envelope header, and the identity a response is checked
 * against.
 *
 * Clean-room Effect Schema re-expression of the protocol constants at
 * `play/client.py:43-49` / `:98-126` and of `_validate_v2_header`
 * (`play/client.py:1319-1329`), read a second time through
 * `play-cli/src/constants.ts` and `play-cli/src/schema/error.ts`.  Nothing here
 * imports from `play-cli`.
 *
 * Every v2 envelope — receipt, batch, disposition, error, page, wait, health —
 * opens with the same two literals and (all but the error envelope) names the
 * game and agent it belongs to.  Four checks, in the order the Python runs
 * them:
 *
 * 1. `schema_version == 2`,
 * 2. `control_protocol == "full-control-v2"`,
 * 3. `game_id == session["game_id"]`,
 * 4. `agent_id == session["agent_id"]`, *only when the payload carries one*.
 *
 * The first two are literals and live in the schemas that use them.  The last
 * two cannot: whether a payload belongs to *this* seat is knowledge the
 * payload does not contain.  They are supplied by {@link sessionMismatch} and
 * applied inside `…For(session)` schema factories, so the check still runs in
 * the parser and still reports as a `WireDecodeError` rather than as a second,
 * differently-shaped failure a caller has to remember to handle.
 *
 * @module
 */

import { Either, Schema } from 'effect';
import { FULL_CONTROL_V2 } from '../control-protocol.ts';
import { JsonObject } from '../json.ts';
import { boundedJsonObject } from './primitives.ts';
import { decodeTolerant, type TolerantDecoder } from '../tolerant.ts';

// ---------------------------------------------------------------------------
// The two header literals — play/client.py:43, full_control_v2.py:17
// ---------------------------------------------------------------------------

/**
 * `FULL_CONTROL_V2` (`play/client.py:43`) — the `control_protocol` value of
 * every agent-side envelope, and of a v2 run's `config.control_protocol` in
 * the supervisor manifest — and `STRATEGIC_V1` (`full_control_v2.py:17`), the
 * older protocol,
 * named only so a caller can *recognise* it: no schema in this directory
 * accepts it.
 *
 * Both strings live in `../control-protocol.ts`, which is also where the
 * gateway's `PUBLIC_CONTROL_PROTOCOLS` vocabulary is closed over them.  They
 * are re-exported here because every reader of this module needs them and
 * `play/client.py` really does declare them alongside the header rules below.
 */
export { FULL_CONTROL_V2, STRATEGIC_V1 } from '../control-protocol.ts';

/**
 * The `control_protocol` literal.
 *
 * Closed on purpose, and one of the few closed literals in `@arena/wire`: this
 * is a protocol discriminator, not a server-chosen label, and a payload
 * claiming a different protocol must not be decoded by v2 rules at all.
 */
export const ControlProtocolV2 = Schema.Literal(FULL_CONTROL_V2).annotations({
  identifier: 'ControlProtocolV2',
  description: 'control_protocol discriminator of the full-control-v2 agent protocol',
});
/** The one value {@link ControlProtocolV2} admits. */
export type ControlProtocolV2 = typeof ControlProtocolV2.Type;

/**
 * The `schema_version` every v2 agent envelope carries.
 *
 * Note this is **2**, while every replay-gateway and run-directory payload
 * carries **1** (`../gateway/`).  The agent protocol and the archive format
 * version independently, so the two literals never share a name here.
 */
export const SCHEMA_VERSION_V2 = 2 as const;

/** The `schema_version` literal. */
export const SchemaVersionV2 = Schema.Literal(SCHEMA_VERSION_V2).annotations({
  identifier: 'SchemaVersionV2',
  description: 'schema_version of every full-control-v2 agent envelope',
});
/** The one value {@link SchemaVersionV2} admits. */
export type SchemaVersionV2 = typeof SchemaVersionV2.Type;

// ---------------------------------------------------------------------------
// The open payload sub-tree — OpenAPI `JsonObject`, client.py `_json_value`
// ---------------------------------------------------------------------------

/**
 * An open JSON object, bounded but never shaped: a command's `arguments`, a
 * descriptor's `subject` and `arguments_schema`, a structured error's
 * `details`.
 *
 * The OpenAPI calls it `JsonObject` — "a bounded JSON object whose
 * section-specific shape is documented by the matching state-section
 * contract".  The bounds are `_json_value`'s (`play/client.py:1278`): depth
 * 12, 8192 items, 2048 keys, keys of 1-128 code points, finite numbers only.
 * The *shape* is deliberately not checked, because it is per-kind and the
 * Python never checked it either — inventing a shape here would refuse
 * arguments a live server accepts.
 *
 * Numbers inside stay `number`, like every integer in this package; bridge to
 * `../canon.ts` with `BigInt(value)` at the point where the int/float
 * distinction is known.  See `../gateway/archive.ts`'s module note.
 */
export const V2JsonObject: Schema.Schema<JsonObject> = JsonObject.pipe(
  Schema.filter((value) => {
    const checked = boundedJsonObject(value, 'json object');
    return Either.isLeft(checked) ? checked.left.message : undefined;
  }),
).annotations({
  identifier: 'V2JsonObject',
  description: 'An open JSON object within _json_value bounds (client.py:1275)',
});

/** Decode an unknown value as a {@link V2JsonObject}. */
export const decodeV2JsonObject: TolerantDecoder<JsonObject> = decodeTolerant(
  V2JsonObject,
  'V2JsonObject',
);

// ---------------------------------------------------------------------------
// _validate_v2_header — play/client.py:1319-1329
// ---------------------------------------------------------------------------

/**
 * The two session fields a v2 response is checked against.
 *
 * Structural on purpose: `SessionIdentity` (`./primitives.ts`) satisfies it,
 * and so does a two-field literal in a test, so neither caller has to know
 * which fields this check wanted.  The fields are `string`, not the branded
 * ids, because the comparison is equality against whatever the payload carries
 * — a brand would add a decode step without adding a guarantee.
 */
export interface V2Session {
  /** The game the local seat is joined to. */
  readonly gameId: string;
  /** The agent id the supervisor minted for this seat. */
  readonly agentId: string;
}

/** The identity fields a v2 envelope may carry.  `agent_id` is not universal. */
export interface V2Addressed {
  readonly game_id: string;
  readonly agent_id?: string;
}

/**
 * `_validate_v2_header`'s two identity checks (`play/client.py:1325-1328`), as
 * a refusal message or `undefined` for "no mismatch".
 *
 * Shaped for `Schema.filter`, which reads exactly that convention.
 *
 * `agent_id` is compared only when the payload has the key — the Python guards
 * it with `"agent_id" in raw` because not every v2 envelope names an agent,
 * and comparing an absent field would refuse a legal payload.  The messages
 * are the Python's own detail strings.
 */
export const sessionMismatch = (
  session: V2Session,
  payload: V2Addressed,
): string | undefined => {
  if (payload.game_id !== session.gameId) return 'response belongs to another game';
  if (payload.agent_id !== undefined && payload.agent_id !== session.agentId) {
    return 'response belongs to another agent';
  }
  return undefined;
};
