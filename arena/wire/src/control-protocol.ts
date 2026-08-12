/**
 * `control_protocol` — which conversation a seat is having with the server.
 *
 * `strategic-v1` is the one-call-per-turn protocol; `full-control-v2` gives the
 * agent the phase ledger.  The set is closed in three places upstream and all
 * three close it identically: `CONTROL_PROTOCOLS` (`full_control_v2.py:19`),
 * `PUBLIC_CONTROL_PROTOCOLS` (`replay_gateway.py:46`) and the pair of module
 * constants at `full_control_v2.py:17-18`.
 *
 * `src/gateway/games.ts` and `src/gateway/manifest.ts` each declared this
 * vocabulary, the `Schema.Literal` over it, and the membership predicate,
 * independently and identically.  One home, so a future protocol cannot be
 * added to one copy and not the other.
 *
 * What is *not* consolidated here, deliberately: `src/agent/protocol.ts`
 * re-states `full-control-v2` as `ControlProtocolV2`, a one-element literal.
 * That is not a duplicate of {@link ControlProtocol} — it is the narrower claim
 * that a v2 envelope header may carry only that one value, and widening it to
 * this schema would let a `strategic-v1` envelope decode as a v2 one.
 */
import { Schema } from 'effect';

/** The `control_protocol` of the phase-ledger protocol (`full_control_v2.py:18`). */
export const FULL_CONTROL_V2 = 'full-control-v2';

/** The `control_protocol` of the one-call-per-turn protocol (`full_control_v2.py:17`). */
export const STRATEGIC_V1 = 'strategic-v1';

/**
 * Every `control_protocol` this build understands, in upstream order.
 *
 * The gateway *omits* the key entirely for anything outside this set
 * (`replay_gateway.py:475`) rather than passing the unknown value through, so
 * a decoder that sees one is looking at a payload the gateway did not write.
 * Where a producer other than the gateway may send an unrecognized value,
 * `src/gateway/manifest.ts` keeps it under the `UnrecognizedControlProtocol`
 * brand instead of dropping it.
 */
export const CONTROL_PROTOCOLS = [STRATEGIC_V1, FULL_CONTROL_V2] as const;

/** A control protocol this build understands. */
export const ControlProtocol = Schema.Literal(...CONTROL_PROTOCOLS).annotations({
  identifier: 'ControlProtocol',
  description: 'strategic-v1 or full-control-v2',
});
/** A control protocol this build understands. */
export type ControlProtocol = typeof ControlProtocol.Type;

const CONTROL_PROTOCOL_SET: ReadonlySet<string> = new Set<string>(CONTROL_PROTOCOLS);

/**
 * The `candidate in PUBLIC_CONTROL_PROTOCOLS` test of
 * `_public_control_protocol` (`replay_gateway.py:472-475`).
 */
export const isControlProtocol = (value: string): value is ControlProtocol =>
  CONTROL_PROTOCOL_SET.has(value);
