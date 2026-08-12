/**
 * The core modules reach consumers through the barrel, and consumers import
 * `@arena/wire` — never `src/`.  If a merge drops one of these re-exports the
 * package still typechecks; this is what notices.
 */
import { describe, expect, test } from 'bun:test';
import * as Wire from 'src/index';

describe('@arena/wire barrel', () => {
  test.each([
    'decodeTolerant',
    'encodeTolerant',
    'isTolerant',
    'schemaLabel',
    'formatIssuePath',
    'TOLERANT_PARSE_OPTIONS',
    'WireDecodeError',
    'WireEncodeError',
  ])('re-exports %s from tolerant.ts', (name) => {
    expect(Object.hasOwn(Wire, name)).toBe(true);
  });

  test.each([
    'JsonValue',
    'JsonObject',
    'JsonArray',
    'JsonValueFromString',
    'decodeJsonValue',
    'decodeJsonObject',
    'decodeJsonArray',
    'decodeJsonValueFromString',
    'isJsonValue',
    'isJsonObject',
    'isJsonArray',
    'jsonField',
  ])('re-exports %s from json.ts', (name) => {
    expect(Object.hasOwn(Wire, name)).toBe(true);
  });

  test.each([
    'GameId',
    'GAME_ID_RE',
    'decodeGameId',
    'isGameId',
    'FrameIndex',
    'FRAME_INDEX_RE',
    'FRAME_INDEX_DIGITS_RE',
    'FrameIndexFromString',
    'FrameIndexFromPngName',
    'decodeFrameIndex',
    'decodeFrameIndexFromString',
    'decodeFrameIndexFromPngName',
    'RunState',
    'RunStateTolerant',
    'UnrecognizedRunState',
    'decodeRunState',
    'decodeRunStateTolerant',
    'isTerminalRunState',
    'isKnownRunState',
    'LIVE_RUN_STATES',
    'TERMINAL_RUN_STATES',
    'DERIVED_RUN_STATES',
  ])('re-exports %s from ids.ts', (name) => {
    expect(Object.hasOwn(Wire, name)).toBe(true);
  });

  test('the convention works end to end through the barrel', () => {
    const decoded = Wire.decodeGameId('AbC-012345678901234_');
    expect(Wire.isTerminalRunState('completed')).toBe(true);
    expect(decoded._tag).toBe('Right');
  });
});
