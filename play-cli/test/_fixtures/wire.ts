/**
 * Golden wire payloads.
 *
 * Each builder returns the *minimal valid* envelope and takes an override
 * object, so a test that wants to prove one field is refused says exactly that
 * and nothing else.
 */
import { FULL_CONTROL_V2 } from 'src/constants';
import type { JsonObject, JsonValue, SessionIdentity } from 'src/schema/primitives';
import type { Revision } from 'src/schema/revision';

export const FIXTURE_GAME_ID = 'game_Hsit9YEuBjKdJPPouFoGVYlk';
export const FIXTURE_AGENT_ID = 'agent_0123456789abcdef';
export const FIXTURE_CONTROLLER = 'codex-gpt-5.6-sol';
export const FIXTURE_CURSOR = 'cursor_abcdefghijklmnopqrstuvwxyz012345';

export const FIXTURE_REVISION: Revision = {
  turn: 5,
  revision: 12,
  state_token: 'token_5_12',
};

export const identity = (overrides: Partial<SessionIdentity> = {}): SessionIdentity => ({
  gameId: FIXTURE_GAME_ID,
  agentId: FIXTURE_AGENT_ID,
  controllerLabel: FIXTURE_CONTROLLER,
  place: 1,
  seatId: 'seat_one',
  playerName: 'Alice',
  evaluation: null,
  ...overrides,
});

/** The on-disk session file `resolveV2` accepts. */
export const sessionFile = (overrides: JsonObject = {}): JsonObject => ({
  schema_version: 1,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  agent_token: 'secret-token',
  service_url: 'http://127.0.0.1:8765',
  controller_label: FIXTURE_CONTROLLER,
  place: 1,
  seat_id: 'seat_one',
  player_name: 'Alice',
  ...overrides,
});

export const errorPayload = (overrides: JsonObject = {}): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  error: {
    code: 'illegal_action',
    message: 'that unit cannot found a city here',
    retryable: false,
    details: {},
  },
  state_revision: null,
  ...overrides,
});

/**
 * `overrides.page` REPLACES the page body rather than merging into it: the
 * envelope's key set is itself validated, so a test that adds `scope` has to be
 * able to drop `cursor_expires_at` in the same breath.
 */
export const pagePayload = (
  items: ReadonlyArray<JsonValue> = [{ id: 'unit_0', name: 'Warriors' }],
  overrides: JsonObject = {}
): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  state_revision: { ...FIXTURE_REVISION },
  page: (overrides['page'] as JsonObject | undefined) ?? {
    section: 'units',
    items,
    total_items: items.length,
    next_cursor: null,
    cursor_expires_at: null,
  },
  ...Object.fromEntries(Object.entries(overrides).filter(([key]) => key !== 'page')),
});

export const descriptor = (overrides: JsonObject = {}): JsonObject => ({
  action_id: 'action_found_city_0',
  kind: 'unit.found_city',
  label: 'Found a city',
  subject: { operation: 'found_city' },
  arguments_schema: { properties: { name: { type: 'string' } }, required: ['name'] },
  state_revision: { ...FIXTURE_REVISION },
  ...overrides,
});

export const legalPagePayload = (
  items: ReadonlyArray<JsonValue> = [descriptor()],
  overrides: JsonObject = {}
): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  state_revision: { ...FIXTURE_REVISION },
  page: (overrides['page'] as JsonObject | undefined) ?? {
    section: 'legal_actions',
    items,
    total_items: items.length,
    next_cursor: null,
    cursor_expires_at: null,
  },
  ...Object.fromEntries(Object.entries(overrides).filter(([key]) => key !== 'page')),
});

export const receiptPayload = (overrides: JsonObject = {}): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  batch_id: 'batch_0123456789',
  receipt_state: 'applied',
  idempotent: true,
  state_revision: { ...FIXTURE_REVISION },
  error: null,
  observation: null,
  ...overrides,
});

export const healthPayload = (overrides: JsonObject = {}): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent: { agent_id: FIXTURE_AGENT_ID, controller_label: FIXTURE_CONTROLLER },
  game_state: 'running',
  seat: { place: 1, seat_id: 'seat_one', player_name: 'Alice' },
  sidecar: { state: 'ready', generation: 3 },
  observation_available: true,
  legal_actions_available: true,
  phase: {
    state: 'awaiting_agent',
    turn: 5,
    phase: 0,
    active: true,
    timing: {
      mode: 'default',
      timeout_s: 60,
      deadline_started_at: 1000,
      deadline_at: 1060,
      elapsed_s: 4,
      remaining_s: 56,
    },
  },
  last_phase_end: null,
  ...overrides,
});

export const waitPayload = (overrides: JsonObject = {}): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  wake_reason: 'phase_active',
  health: healthPayload(),
  state_revision: null,
  ...overrides,
});
