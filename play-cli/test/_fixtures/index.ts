/**
 * Shared test fixtures.
 *
 * Every unit's tests import from here rather than hand-rolling a session or a
 * wire payload, so a schema change breaks one file instead of eighteen.
 */
export {
  FIXTURE_AGENT_ID,
  FIXTURE_CONTROLLER,
  FIXTURE_CURSOR,
  FIXTURE_GAME_ID,
  FIXTURE_REVISION,
  errorPayload,
  healthPayload,
  identity,
  legalPagePayload,
  pagePayload,
  receiptPayload,
  sessionFile,
  waitPayload,
} from 'test/_fixtures/wire';

export {
  fakeFetch,
  jsonResponse,
  recordingFetch,
  type FakeRoute,
  type RecordedRequest,
} from 'test/_fixtures/fake-v2-server';

export { scratchWorkspace, withScratchWorkspace, type Scratch } from 'test/_fixtures/workspace';
