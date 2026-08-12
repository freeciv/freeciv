/**
 * The v2 wire shapes the agent suites decode.
 *
 * Transcribed from `play-cli/test/receipt-harness.ts`, which in turn mirrors
 * the `revision()`, `error()`, `receipt()` and `descriptor()` classmethods in
 * `play/tests/test_client.py`.  Copied as literals rather than imported:
 * `@arena/wire` depends on `effect` and nothing else, and a fixture that
 * reached back into `play-cli` would make the parity claim circular.
 *
 * Not a `*.test.ts` file, so `bun test` never collects it as a suite.
 */

import type { V2Session } from 'src/agent/protocol';
import type { JsonObject, JsonValue } from 'src/json';

/** `test_client.py`'s `FIXTURE_GAME_ID` — a real-shaped v2 game id. */
export const GAME_ID = 'game_Hsit9YEuBjKdJPPouFoGVYlk';

/** `test_client.py`'s `FIXTURE_AGENT_ID`. */
export const AGENT_ID = 'agent_0123456789abcdef';

/** The seat every bound decoder in these suites is checked against. */
export const SESSION: V2Session = { gameId: GAME_ID, agentId: AGENT_ID };

/** `test_client.py`'s `revision(8)` — printed by the CLI as `rev8/t3`. */
export const REVISION: JsonObject = {
  turn: 3,
  revision: 8,
  state_token: `state_${'0'.repeat(26)}`,
};

/** The same `(turn, revision)` with a fresh token: a republished state. */
export const REPUBLISHED_REVISION: JsonObject = {
  turn: 3,
  revision: 8,
  state_token: `state_${'1'.repeat(26)}`,
};

/** A strictly later revision. */
export const NEXT_REVISION: JsonObject = {
  turn: 3,
  revision: 9,
  state_token: `state_${'2'.repeat(26)}`,
};

/** `test_client.py`'s `BATCH_ID`. */
export const BATCH_ID = `batch_${'R'.repeat(24)}`;

const FULL_CONTROL_V2 = 'full-control-v2';

/** `test_client.py`'s `error()` classmethod. */
export const errorEnvelope = (
  code = 'invalid_request',
  revision: JsonValue = REVISION,
  retryable = false,
): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  error: { code, message: 'validated test error', retryable, details: {} },
  state_revision: revision,
});

/** `test_client.py`'s `receipt()` classmethod. */
export const receiptWire = (
  batchId: string = BATCH_ID,
  state = 'applied',
  overrides: JsonObject = {},
): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: GAME_ID,
  agent_id: AGENT_ID,
  batch_id: batchId,
  receipt_state: state,
  idempotent: false,
  state_revision: REVISION,
  error:
    state === 'rejected'
      ? errorEnvelope('illegal_action')
      : state === 'ambiguous'
        ? errorEnvelope('action_outcome_ambiguous')
        : null,
  observation: null,
  ...overrides,
});

/** The six feeling rows, in stage order, all `happy`. */
export const feelingsWire = (happy: number): ReadonlyArray<JsonObject> =>
  ['base', 'luxury', 'effects', 'nationality', 'martial_law', 'final'].map((stage) => ({
    stage,
    happy,
    content: 0,
    unhappy: 0,
    angry: 0,
  }));

/**
 * The city block of `receipt.test.ts`'s `investigation()` helper in
 * `play-cli`, with the population identity satisfied: four happy citizens in
 * every stage, no specialists, `size: 4`.
 */
export const cityWire = (overrides: JsonObject = {}): JsonObject => ({
  id: 'city_1',
  name: 'München',
  size: 4,
  production: { id: 'unit_warriors', kind: 'unit', name: 'Warriors' },
  shields: { stock: 12, surplus: 2 },
  improvements: [{ id: 'b_1', name: 'Barracks' }],
  citizens: { feelings: feelingsWire(4), specialists: [] },
  ...overrides,
});

/** The observation envelope around {@link cityWire}. */
export const investigationWire = (overrides: JsonObject = {}): JsonObject => ({
  id: 'obs_1',
  type: 'city_investigation',
  source: 'human_client_city_info',
  freshness: 'captured_at_receipt_revision',
  state_revision: REVISION,
  city: cityWire(),
  ...overrides,
});

/** `test_client.py`'s `descriptor()` classmethod. */
export const descriptorWire = (overrides: JsonObject = {}): JsonObject => ({
  action_id: 'action_opaque',
  kind: 'phase.end',
  label: 'End phase',
  subject: { operation: 'end' },
  arguments_schema: { type: 'object' },
  state_revision: REVISION,
  ...overrides,
});

/** The body `_persist_batch_for_action` writes before the first POST. */
export const batchWire = (overrides: JsonObject = {}): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: GAME_ID,
  agent_id: AGENT_ID,
  batch_id: BATCH_ID,
  state_revision: REVISION,
  commands: [{ action_id: 'action_opaque', arguments: { ready: true } }],
  ...overrides,
});

/** A `CliBatchDisposition` body, `disposition` and the rest overridable. */
export const dispositionWire = (overrides: JsonObject = {}): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: GAME_ID,
  agent_id: AGENT_ID,
  batch_id: BATCH_ID,
  disposition: 'receipt_terminal',
  receipt: receiptWire(),
  error: null,
  ...overrides,
});
