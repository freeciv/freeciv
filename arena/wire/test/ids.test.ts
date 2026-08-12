/**
 * Identifier and run-state rules, checked against the Python gateway that
 * still owns them.
 */
import { describe, expect, test } from 'bun:test';
import { Either, Schema } from 'effect';
import {
  DERIVED_RUN_STATES,
  decodeFrameIndex,
  decodeFrameIndexFromPngName,
  decodeFrameIndexFromString,
  decodeGameId,
  decodeRunState,
  decodeRunStateTolerant,
  FRAME_INDEX_DIGITS_RE,
  FRAME_INDEX_RE,
  FrameIndex,
  FrameIndexFromPngName,
  FrameIndexFromString,
  GAME_ID_RE,
  GameId,
  isGameId,
  isKnownRunState,
  isTerminalRunState,
  LIVE_RUN_STATES,
  RunState,
  TERMINAL_RUN_STATES,
} from 'src/ids';

const accepts = (either: Either.Either<unknown, unknown>): boolean => Either.isRight(either);

const rightOrThrow = <A, E>(either: Either.Either<A, E>): A =>
  Either.getOrThrowWith(either, (error) => new Error(`expected Right, got ${String(error)}`));

const VALID_ID = 'AbC-012345678901234_'; // 20 chars, one of each class

describe('GameId', () => {
  test('the 20-character floor and 80-character ceiling are inclusive', () => {
    expect(VALID_ID).toHaveLength(20);
    expect(accepts(decodeGameId('a'.repeat(20)))).toBe(true);
    expect(accepts(decodeGameId('a'.repeat(80)))).toBe(true);
    expect(accepts(decodeGameId('a'.repeat(19)))).toBe(false);
    expect(accepts(decodeGameId('a'.repeat(81)))).toBe(false);
  });

  test('the character class is URL-safe base64 without padding', () => {
    expect(accepts(decodeGameId(VALID_ID))).toBe(true);
    expect(accepts(decodeGameId(`${'a'.repeat(19)}.`))).toBe(false);
    expect(accepts(decodeGameId(`${'a'.repeat(19)}/`))).toBe(false);
    expect(accepts(decodeGameId(`${'a'.repeat(19)}+`))).toBe(false);
    expect(accepts(decodeGameId(`${'a'.repeat(19)}=`))).toBe(false);
    expect(accepts(decodeGameId(`${'a'.repeat(19)} `))).toBe(false);
    expect(accepts(decodeGameId(`${'a'.repeat(19)}é`))).toBe(false);
  });

  test('path-traversal and separator attempts are rejected outright', () => {
    expect(accepts(decodeGameId('../../etc/passwd0000'))).toBe(false);
    expect(accepts(decodeGameId(`${'a'.repeat(20)}/manifest.json`))).toBe(false);
    expect(accepts(decodeGameId(`${'a'.repeat(20)}%2F..`))).toBe(false);
  });

  test('an embedded or trailing newline cannot sneak past the anchors', () => {
    // Python's `$` also matches before a trailing newline; `re.fullmatch`
    // forbids that, and JS anchors agree. Both must reject these.
    expect(accepts(decodeGameId(`${'a'.repeat(20)}\n`))).toBe(false);
    expect(accepts(decodeGameId(`${'a'.repeat(10)}\n${'a'.repeat(10)}`))).toBe(false);
    expect(GAME_ID_RE.test(`${'a'.repeat(20)}\n`)).toBe(false);
  });

  test('non-strings are rejected, not coerced', () => {
    expect(accepts(decodeGameId(12345678901234567890n))).toBe(false);
    expect(accepts(decodeGameId(1234567890))).toBe(false);
    expect(accepts(decodeGameId(null))).toBe(false);
    expect(accepts(decodeGameId(['a'.repeat(20)]))).toBe(false);
  });

  test('the guard agrees with the decoder', () => {
    const cases = [VALID_ID, 'short', 'a'.repeat(80), 'a'.repeat(81), 'bad char here!!!!!!!'];
    expect(cases.map(isGameId)).toEqual(cases.map((input) => accepts(decodeGameId(input))));
  });

  test('the brand constructor refuses an invalid id', () => {
    // String(...) unwraps the brand: a branded value refuses comparison with a
    // bare string, which is exactly what the brand is for.
    expect(String(GameId.make(VALID_ID))).toBe(VALID_ID);
    expect(() => GameId.make('too-short')).toThrow();
  });

  test('a shared RegExp is not stateful across calls', () => {
    // A `g`-flagged pattern would alternate pass/fail via lastIndex.
    expect(GAME_ID_RE.global).toBe(false);
    expect([1, 2, 3].map(() => isGameId(VALID_ID))).toEqual([true, true, true]);
  });
});

describe('FrameIndex', () => {
  test('zero-based, integral, non-negative', () => {
    expect(accepts(decodeFrameIndex(0))).toBe(true);
    expect(accepts(decodeFrameIndex(999999))).toBe(true);
    expect(accepts(decodeFrameIndex(-1))).toBe(false);
    expect(accepts(decodeFrameIndex(1.5))).toBe(false);
    expect(accepts(decodeFrameIndex(Number.NaN))).toBe(false);
    expect(accepts(decodeFrameIndex('3'))).toBe(false);
  });
});

describe('FrameIndexFromString: leading zeros are forbidden', () => {
  test('canonical decimal strings decode', () => {
    expect(Number(rightOrThrow(decodeFrameIndexFromString('0')))).toBe(0);
    expect(Number(rightOrThrow(decodeFrameIndexFromString('7')))).toBe(7);
    expect(Number(rightOrThrow(decodeFrameIndexFromString('1024')))).toBe(1024);
  });

  test.each(['00', '007', '0000000', '01'])('%p is not a frame index', (text) => {
    expect(accepts(decodeFrameIndexFromString(text))).toBe(false);
  });

  test.each(['', ' 7', '7 ', '+7', '-7', '7.0', '0x10', '1e3', '٧', '7\n'])(
    '%p is not a frame index either',
    (text) => {
      expect(accepts(decodeFrameIndexFromString(text))).toBe(false);
    },
  );

  test('encoding produces the canonical form, so the round trip is a fixed point', () => {
    const index = rightOrThrow(decodeFrameIndexFromString('42'));
    expect(rightOrThrow(Schema.encodeEither(FrameIndexFromString)(index))).toBe('42');
    expect(rightOrThrow(Schema.encodeEither(FrameIndexFromString)(FrameIndex.make(0)))).toBe('0');
  });
});

describe('FrameIndexFromPngName: the URL path segment', () => {
  test('"{n}.png" decodes to n', () => {
    expect(Number(rightOrThrow(decodeFrameIndexFromPngName('0.png')))).toBe(0);
    expect(Number(rightOrThrow(decodeFrameIndexFromPngName('12.png')))).toBe(12);
  });

  test.each(['007.png', '000012.png', 'latest.png', '12.PNG', '12.png/', '12', '.png', '12.png.png'])(
    '%p is not a frame segment',
    (segment) => {
      expect(accepts(decodeFrameIndexFromPngName(segment))).toBe(false);
    },
  );

  test('the padded on-disk name is deliberately not accepted on the wire', () => {
    // ARCHIVE_PNG_RE stores frames as 000012.png; the gateway maps to 12.png.
    expect(accepts(decodeFrameIndexFromPngName('000012.png'))).toBe(false);
    expect(rightOrThrow(Schema.encodeEither(FrameIndexFromPngName)(FrameIndex.make(12)))).toBe(
      '12.png',
    );
  });

  test('one frame has exactly one URL', () => {
    const canonical = rightOrThrow(
      Schema.encodeEither(FrameIndexFromPngName)(FrameIndex.make(7)),
    );
    expect(canonical).toBe('7.png');
    expect(['7.png', '07.png', '007.png'].filter((s) => accepts(decodeFrameIndexFromPngName(s)))).toEqual([
      '7.png',
    ]);
  });
});

describe('run states', () => {
  test('the vocabulary is partitioned, with no duplicates', () => {
    const all = [...LIVE_RUN_STATES, ...TERMINAL_RUN_STATES, ...DERIVED_RUN_STATES];
    expect(new Set(all).size).toBe(all.length);
    expect(all).toEqual([...RunState.literals]);
  });

  test('every listed state decodes', () => {
    expect(RunState.literals.every((state) => accepts(decodeRunState(state)))).toBe(true);
  });

  test('a state this build does not know is rejected by the strict schema', () => {
    expect(accepts(decodeRunState('paused'))).toBe(false);
    expect(accepts(decodeRunState('COMPLETED'))).toBe(false);
    expect(accepts(decodeRunState(''))).toBe(false);
  });

  test('the tolerant schema keeps an unknown state instead of failing', () => {
    expect(rightOrThrow(decodeRunStateTolerant('completed'))).toBe('completed');
    // `String(...)` because an unrecognized state is branded: the type refuses
    // to be compared against a bare string, which is the whole point of it.
    expect(String(rightOrThrow(decodeRunStateTolerant('paused')))).toBe('paused');
    expect(accepts(decodeRunStateTolerant(7))).toBe(false);
    expect(accepts(decodeRunStateTolerant(null))).toBe(false);
  });

  test('isKnownRunState is the branch that forces unknown vocabulary into the open', () => {
    expect(isKnownRunState(rightOrThrow(decodeRunStateTolerant('running')))).toBe(true);
    expect(isKnownRunState(rightOrThrow(decodeRunStateTolerant('paused')))).toBe(false);
  });

  test('isTerminalRunState matches the gateway set exactly', () => {
    expect(TERMINAL_RUN_STATES.every(isTerminalRunState)).toBe(true);
    expect(LIVE_RUN_STATES.some(isTerminalRunState)).toBe(false);
    expect(DERIVED_RUN_STATES.some(isTerminalRunState)).toBe(false);
    expect(isTerminalRunState('paused')).toBe(false);
  });

  test('interrupted is not terminal: it is the relabel for a run that never finished', () => {
    expect(isTerminalRunState('interrupted')).toBe(false);
    expect(accepts(decodeRunState('interrupted'))).toBe(true);
  });
});

/**
 * The Python gateway is the authority while both implementations run.  These
 * assertions re-read its source, so a drifted pattern fails here rather than
 * in production — and the line-number citations in `src/ids.ts` cannot rot
 * unnoticed.
 */
const AGENT_EVAL = `${import.meta.dir}/../../../agent_eval`;

/**
 * Read a Python source file the parity assertions below are written against.
 *
 * **Ungated on purpose.**  A missing `agent_eval/` used to make these tests
 * *skip*, so a checkout without the Python side reported the whole parity story
 * green while checking nothing.  This throws at module load instead, which is
 * the standard `test/canon.test.ts` and `test/fnv1a64.test.ts` already set for
 * the python3 oracle: a missing authority fails, it does not disappear.
 */
const readPythonSource = (path: string): Promise<string> => Bun.file(path).text();

const source = await readPythonSource(`${AGENT_EVAL}/replay_gateway.py`);
const supervisorSource = await readPythonSource(`${AGENT_EVAL}/supervisor.py`);

describe('parity with agent_eval/replay_gateway.py', () => {
  test('the Python sources are present — ungated, so a missing authority fails instead of skipping', () => {
    expect(source.length).toBeGreaterThan(0);
    expect(supervisorSource.length).toBeGreaterThan(0);
  });

  const pythonPattern = (name: string): string => {
    const match = new RegExp(`${name} = re\\.compile\\(r"([^"]+)"\\)`).exec(source);
    return match?.[1] ?? `<${name} not found>`;
  };

  test('GAME_ID_RE is transcribed verbatim', () => {
    expect(pythonPattern('GAME_ID_RE')).toBe(GAME_ID_RE.source);
  });

  test('FRAME_INDEX_RE is transcribed verbatim, under the upstream name', () => {
    // The port's FRAME_INDEX_RE must BE the Python's FRAME_INDEX_RE: it is
    // matched against a route path segment, so a port that reached for this
    // name and got the bare-integer pattern would route a request the gateway
    // 404s.  The digits-only variant is deliberately named something else.
    expect(pythonPattern('FRAME_INDEX_RE')).toBe(FRAME_INDEX_RE.source);
  });

  test('the digits-only pattern is FRAME_INDEX_RE minus its .png suffix', () => {
    expect(`${FRAME_INDEX_DIGITS_RE.source.slice(0, -1)}\\.png$`).toBe(FRAME_INDEX_RE.source);
  });

  test('the two are not interchangeable: only FRAME_INDEX_RE matches a path segment', () => {
    expect(FRAME_INDEX_RE.test('7.png')).toBe(true);
    expect(FRAME_INDEX_RE.test('7')).toBe(false);
    expect(FRAME_INDEX_DIGITS_RE.test('7')).toBe(true);
    expect(FRAME_INDEX_DIGITS_RE.test('7.png')).toBe(false);
  });

  test('TERMINAL_STATES is the same set', () => {
    const match = /TERMINAL_STATES = \{([^}]*)\}/.exec(source);
    const pythonStates = [...(match?.[1] ?? '').matchAll(/"([a-z_]+)"/g)].map((m) => m[1]);
    expect(new Set(pythonStates)).toEqual(new Set<string | undefined>(TERMINAL_RUN_STATES));
  });

  test('"interrupted" is still the relabel the gateway writes', () => {
    expect(source).toContain('row["state"] = "interrupted"');
  });

  test('"unknown" is still the missing-state fallback', () => {
    expect(source).toContain('manifest.get("state", manifest.get("status")), "unknown", 32,');
  });

  test('the live states the supervisor reports are still spelled the same', () => {
    expect(supervisorSource).toContain('"state": "lobby"');
    expect(supervisorSource).toContain('{"lobby", "starting", "running"}');
  });
});
