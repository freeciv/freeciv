/**
 * Golden `full-control-v2` health and wait payloads.
 *
 * Values transcribed from `play-cli/test/_fixtures/wire.ts` (`healthPayload`,
 * `waitPayload`, `identity`), which in turn mirror the fixtures in
 * `play/tests/test_client.py`.  Copied as literals rather than imported:
 * `@arena/wire` depends on `effect` and nothing else, and a fixture reaching
 * back into `play-cli` would make the parity claim circular.
 *
 * Every builder returns the *minimal valid* shape and takes an override object,
 * so a test that proves one field is refused says exactly that and nothing
 * else.  Overrides replace a key outright — nested blocks have their own
 * builders for the same reason.
 *
 * Not a `*.test.ts` file, so `bun test` never collects it as a suite.
 */

import type { SessionIdentity } from 'src/agent/primitives';
import { OpaqueId, PlayGameId } from 'src/agent/ids';
/**
 * A payload under construction.
 *
 * Values are `unknown`, not `JsonValue`: a fixture's whole job is to be handed
 * things a real payload would never carry — a `place` of `true`, a `turn` of
 * `2.5`, a `state_token` of `"/etc/passwd"` — so that the schema, not the type
 * checker, is what refuses them.  A `JsonValue`-typed builder would reject
 * those call sites at compile time and quietly delete the test.
 */
export type WirePayload = { readonly [key: string]: unknown };

/** `play-cli/test/_fixtures/wire.ts:12` — `FIXTURE_GAME_ID`. */
export const GAME_ID = 'game_Hsit9YEuBjKdJPPouFoGVYlk';

/** `play-cli/test/_fixtures/wire.ts:13` — `FIXTURE_AGENT_ID`. */
export const AGENT_ID = 'agent_0123456789abcdef';

/** `play-cli/test/_fixtures/wire.ts:14` — `FIXTURE_CONTROLLER`. */
export const CONTROLLER = 'codex-gpt-5.6-sol';

/** The seat every bound decoder in these suites is checked against. */
export const SEAT_ID = 'seat_one';

/** The player name that goes with {@link SEAT_ID}. */
export const PLAYER_NAME = 'Alice';

/** The `full-control-v2` literal, spelled out so the fixture is self-contained. */
export const CONTROL_PROTOCOL = 'full-control-v2';

/** A state token that satisfies `OPAQUE_ID_RE`. */
export const STATE_TOKEN = OpaqueId.make(`state_${'0'.repeat(26)}`);

/** A second, different token — the one a `revision_changed` wake must move past. */
export const OTHER_STATE_TOKEN = OpaqueId.make(`state_${'1'.repeat(26)}`);

/** The `state_revision` block a wait envelope may carry. */
export const revisionWire = (overrides: WirePayload = {}): WirePayload => ({
  turn: 5,
  revision: 12,
  state_token: STATE_TOKEN,
  ...overrides,
});

/** The local session a bound decoder checks a health envelope against. */
export const session = (overrides: Partial<SessionIdentity> = {}): SessionIdentity => ({
  gameId: PlayGameId.make(GAME_ID),
  agentId: OpaqueId.make(AGENT_ID),
  controllerLabel: CONTROLLER,
  place: 1,
  seatId: SEAT_ID,
  playerName: PLAYER_NAME,
  evaluation: null,
  ...overrides,
});

/** `phase.timing` — a `default`-mode phase 4 seconds into a 60-second budget. */
export const timingWire = (overrides: WirePayload = {}): WirePayload => ({
  mode: 'default',
  timeout_s: 60,
  deadline_started_at: 1000,
  deadline_at: 1060,
  elapsed_s: 4,
  remaining_s: 56,
  ...overrides,
});

/** The phase block: turn 5, live, and this seat's to play. */
export const phaseWire = (overrides: WirePayload = {}): WirePayload => ({
  state: 'awaiting_agent',
  turn: 5,
  phase: 0,
  active: true,
  timing: timingWire(),
  ...overrides,
});

/** A `waiting_on.seats` row — every value free-form on the wire. */
export const waitingOnSeatWire = (overrides: WirePayload = {}): WirePayload => ({
  place: 2,
  seat_id: 'seat_two',
  player_name: 'Bob',
  controller_label: null,
  standing: 'active',
  is_self: false,
  ...overrides,
});

/** The `waiting_on` block: this phase belongs to another seat. */
export const waitingOnWire = (overrides: WirePayload = {}): WirePayload => ({
  kind: 'other_seat',
  summary: 'waiting on Bob',
  waiting_s: 12.5,
  seats: [waitingOnSeatWire()],
  ...overrides,
});

/** The `auto_end` block: enabled and armed, 30 seconds of grace left. */
export const autoEndWire = (overrides: WirePayload = {}): WirePayload => ({
  enabled: true,
  armed: true,
  grace_s: 45,
  remaining_s: 30,
  ...overrides,
});

/** The `prior_end` block — deliberately place 2, i.e. *not* this seat. */
export const priorEndWire = (overrides: WirePayload = {}): WirePayload => ({
  place: 2,
  seat_id: 'seat_two',
  player_name: 'Bob',
  controller_label: null,
  turn: 4,
  phase: 0,
  source: 'timeout',
  receipt_state: 'applied',
  resolution: 'advanced',
  elapsed_s: 60,
  orders_submitted: null,
  ...overrides,
});

/** The `last_phase_end` event, naming this seat and this controller. */
export const phaseEndWire = (overrides: WirePayload = {}): WirePayload => ({
  sequence: 1,
  turn: 4,
  phase: 0,
  place: 1,
  seat_id: SEAT_ID,
  player_name: PLAYER_NAME,
  player_color: '#3F7FBF',
  controller_label: CONTROLLER,
  controller_type: 'external',
  source: 'agent',
  receipt_state: 'applied',
  resolution: 'advanced',
  deadline_started_at: 1000,
  ended_at: 1030,
  elapsed_s: 30,
  ...overrides,
});

/** The `last_recovery` event — all seventeen keys `V2_RECOVERY_FIELDS` names. */
export const recoveryWire = (overrides: WirePayload = {}): WirePayload => ({
  attempt: 1,
  client_state: 'reattached',
  exit_code: null,
  exit_signal: null,
  format: 'v2',
  game_id: GAME_ID,
  kind: 'sidecar_reattach',
  outcome: 'recovered',
  place: 1,
  recovered_to_turn: 4,
  rewound_applied_actions: false,
  schema_version: 2,
  seat_id: SEAT_ID,
  sidecar_generation: 4,
  timestamp: '2026-08-11T00:00:00Z',
  trigger: 'sidecar_exit',
  turn: 5,
  ...overrides,
});

/** The health envelope — `play-cli/test/_fixtures/wire.ts:129` (`healthPayload`). */
export const healthWire = (overrides: WirePayload = {}): WirePayload => ({
  schema_version: 2,
  control_protocol: CONTROL_PROTOCOL,
  game_id: GAME_ID,
  agent: { agent_id: AGENT_ID, controller_label: CONTROLLER },
  game_state: 'running',
  seat: { place: 1, seat_id: SEAT_ID, player_name: PLAYER_NAME },
  sidecar: { state: 'ready', generation: 3 },
  observation_available: true,
  legal_actions_available: true,
  phase: phaseWire(),
  last_phase_end: null,
  ...overrides,
});

/** The wait envelope — `play-cli/test/_fixtures/wire.ts:157` (`waitPayload`). */
export const waitWire = (overrides: WirePayload = {}): WirePayload => ({
  schema_version: 2,
  control_protocol: CONTROL_PROTOCOL,
  game_id: GAME_ID,
  agent_id: AGENT_ID,
  wake_reason: 'phase_active',
  health: healthWire(),
  state_revision: null,
  ...overrides,
});
