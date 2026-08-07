/**
 * The mirror store: layout, cells, revisions, the header and the phase marker.
 *
 * Ports `MirrorDirectoryTests`, the header half of `RevisionStampTests`,
 * `ApiTests` and `DriftTests` from `play/tests/test_state_mirror.py`, plus the
 * client-side bridge `_mirror_text` / `_cached_phase_note`
 * (client.py:10755-10799).  Every golden string was produced by running the
 * CPython original against the same input.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import type { PrivateFs } from 'src/services/private-fs';
import {
  HEADER_FILE,
  MISSING,
  OVERVIEW_FILE,
  PHASE_FILE,
  aliasMap,
  appendMonitorLog,
  cachedPhaseNote,
  cell,
  dig,
  fileName,
  handle,
  isBehind,
  isNewer,
  isStale,
  localTimestamp,
  mirrorDir,
  mirrorGuard,
  mirrorText,
  pair,
  phaseMarker,
  position,
  readPhaseMarker,
  revLine,
  revisionPair,
  staleLine,
  tableText,
  updateFromHealth,
  updatePhaseMarker,
  writeMirror,
} from 'src/services/mirror';
import { playerError } from 'src/errors';
import { scratchWorkspace, type Scratch } from 'test/_fixtures';

const GAME_ID = 'game_12345678901234567890';
const UNIT_A = `unit_${'a'.repeat(32)}`;
const UNIT_B = `unit_${'b'.repeat(32)}`;

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

interface Mirror {
  readonly dir: string;
  readonly sessionPath: string;
  readonly run: <A, E>(effect: Effect.Effect<A, E, PrivateFs>) => Either.Either<A, E>;
}

const freshMirror = (): Mirror => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const sessionPath = path.join(scratch.workspace.stateRoot, GAME_ID, 'codex-test.json');
  return {
    dir: Effect.runSync(mirrorDir(sessionPath)),
    sessionPath,
    run: (effect) => Effect.runSync(Effect.either(Effect.provide(effect, scratch.layer))),
  };
};

const right = <A, E>(either: Either.Either<A, E>): A => {
  expect(Either.isLeft(either) ? either.left : 'ok').toBe('ok');
  if (Either.isLeft(either)) throw new Error('unreachable');
  return either.right;
};

const HEALTH = {
  schema_version: 2,
  control_protocol: 'full-control-v2',
  game_id: GAME_ID,
  agent: { agent_id: 'agent_test-controller', controller_label: 'codex-test-model' },
  game_state: 'running',
  seat: { place: 1, seat_id: 'place-1', player_name: 'AgentPlace1' },
  sidecar: { state: 'running', generation: 1, server_connected: true },
  observation_available: true,
  legal_actions_available: true,
  phase: {
    state: 'awaiting_agent',
    turn: 3,
    phase: 0,
    active: true,
    timing: {
      mode: 'default',
      timeout_s: 180,
      deadline_started_at: 10,
      deadline_at: 190,
      elapsed_s: 139,
      remaining_s: 41,
    },
  },
  last_phase_end: null,
  objective: 'Maximize final Freeciv civilization score.',
  max_turns: 5000,
  turns_remaining: 4997,
};

/** The CPython golden, byte for byte, from `state_mirror.update_from_health`. */
const HEADER_BODY = [
  'game      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test-model',
  'objective Maximize final Freeciv civilization score.',
  'budget    turn 3 of 5000 · 4997 remaining',
  'phase     awaiting_agent · turn 3 phase 0 · active yes',
  'timing    default · 41s left of 180s',
  'game_state running · observation yes · legal_actions yes',
  'sidecar   running · server_connected yes',
  '',
  'protocol  full-control-v2 · every action is an opaque server-issued capability;',
  '          aliases are revision-scoped and stop resolving once a newer revision arrives.',
  'files     state/overview.tsv state/units.tsv state/cities.tsv state/map.txt',
  '          state/options/<alias>.txt state/delta.md cache/*.tsv  (projections; read them, they cost no network)',
  '',
];

const DEFAULT_CARD = [
  'just turn                              turn briefing',
  'just state --section SECTION           one state section',
  "just legal --actor_id ACTOR_ID         one actor's option catalog",
  'just batch --action_id ID [--arguments JSON]',
  'just wait                              block until the next phase',
];

const readFile = (dir: string, relative: ReadonlyArray<string>): string =>
  fs.readFileSync(path.join(dir, ...relative), 'utf8');

// ---------------------------------------------------------------------------

describe('mirror_dir', () => {
  test('the mirror is a sibling directory of the session file', () => {
    expect(Effect.runSync(mirrorDir('/w/play/.sessions/game_x/codex-test.json'))).toBe(
      '/w/play/.sessions/game_x/codex-test'
    );
  });

  test('a suffixless session never collides with its own mirror', () => {
    expect(Effect.runSync(mirrorDir('/w/play/.sessions/game_x/codex-test'))).toBe(
      '/w/play/.sessions/game_x/codex-test-mirror'
    );
  });

  test('only the last suffix is dropped', () => {
    expect(Effect.runSync(mirrorDir('/w/x/a.b.c'))).toBe('/w/x/a.b');
  });

  test('a trailing slash still names the session', () => {
    expect(Effect.runSync(mirrorDir('/w/x/'))).toBe('/w/x-mirror');
  });

  test('a session path with no name is refused as a value, not a raise', () => {
    // CPython lets `Path.with_name` raise `ValueError` here; the port keeps the
    // refusal but makes it a `PlayerError` in the error channel (NOTES.md, U04).
    const either = Effect.runSync(Effect.either(mirrorDir('/')));
    expect(Either.isLeft(either) ? either.left.message : '').toBe(
      'state mirror: the session file name has no mirror directory'
    );
  });

  /**
   * `mirror_dir` is `Path.stem if Path.suffix else Path.name + "-mirror"`, and
   * CPython 3.14 (this repo runs 3.14.6) rewrote both properties: leading dots
   * are dropped before the suffix search, and a bare trailing dot is now a
   * suffix.  A session basename is user-controlled — `--session` and
   * `PLAY_SESSION` reach `_session_path` unfiltered — so a name-level
   * disagreement writes the whole mirror into a directory no later `show`,
   * `state` or `_cached_phase_note` would ever read.  Every row below was
   * produced by running `state_mirror.mirror_dir` under the installed CPython.
   */
  test('every dotted basename resolves exactly as CPython 3.14 resolves it', () => {
    const golden: ReadonlyArray<readonly [string, string]> = [
      ['/w/x/session.', '/w/x/session'],
      ['/w/x/a.', '/w/x/a'],
      ['/w/x/x..', '/w/x/x.'],
      ['/w/x/..hidden', '/w/x/..hidden-mirror'],
      ['/w/x/.hidden', '/w/x/.hidden-mirror'],
      ['/w/x/.hidden.json', '/w/x/.hidden'],
      ['/w/x/...', '/w/x/...-mirror'],
      ['/w/x/..', '/w/x/..-mirror'],
      ['/w/x/..a.b', '/w/x/..a'],
      ['/w/x/a.b.', '/w/x/a.b'],
      ['a..b.', 'a..b'],
      ['session.json', 'session'],
      ['plain', 'plain-mirror'],
      // pathlib drops `.` components and normalises repeated separators, which
      // `path.basename` / `path.dirname` do not.
      ['/w/x/.', '/w/x-mirror'],
      ['/w//x///a.json', '/w/x/a'],
      ['/w/x/./y.json', '/w/x/y'],
      ['/w/x/y.json/', '/w/x/y'],
      // POSIX keeps *exactly* two leading slashes and collapses one or three.
      ['//w/x/a.json', '//w/x/a'],
      ['///w/x/a.json', '/w/x/a'],
    ];
    expect(golden.map(([input]) => [input, Effect.runSync(mirrorDir(input))])).toEqual(
      golden.map(([input, expected]) => [input, expected])
    );
  });
});

describe('cells', () => {
  test('every scalar renders the way CPython renders it', () => {
    expect({
      none: cell(null),
      missing: cell(MISSING),
      true: cell(true),
      false: cell(false),
      float_int: cell(3.0),
      float: cell(2.5),
      control: cell('Lon\ndon\t# rev 999 turn 999'),
      spaces: cell('a     b'),
      long: cell('x'.repeat(100)),
      empty: cell('   '),
    }).toEqual({
      none: '-',
      missing: '-',
      true: 'yes',
      false: 'no',
      float_int: '3',
      float: '2.5',
      control: 'Lon don # rev 999 turn 999',
      spaces: 'a b',
      long: `${'x'.repeat(63)}~`,
      empty: '-',
    });
  });

  test('a capped cell is exactly 64 characters', () => {
    expect(cell('y'.repeat(100)).length).toBe(64);
  });

  // Regression: `_MAX_CELL` counts code points.  A UTF-16 cap truncated a
  // 42-code-point name to 33 and left a lone high surrogate at the end, which
  // the utf8 write then turned into U+FFFD on disk.
  test('an astral cell is capped in code points, never in UTF-16 units', () => {
    const emoji = '\u{1F600}';
    const short = `AB${emoji.repeat(40)}`;
    expect(cell(short)).toBe(short);
    expect([...cell(short)].length).toBe(42);

    const long = `${'x'.repeat(10)}${emoji.repeat(60)}`;
    const capped = cell(long);
    expect(capped).toBe(`${'x'.repeat(10)}${emoji.repeat(53)}~`);
    expect([...capped].length).toBe(64);
    expect(/[\uD800-\uDBFF](?![\uDC00-\uDFFF])/.test(capped)).toBe(false);
    expect(Buffer.from(capped, 'utf8').includes(Buffer.from('�', 'utf8'))).toBe(false);
  });

  test('the 64-code-point boundary is the same one CPython draws', () => {
    // Each pair is (input, CPython's `_cell` output).
    const emoji = '\u{1F600}';
    const cjkExt = '\u{20000}';
    const cases: ReadonlyArray<readonly [string, string]> = [
      [emoji.repeat(64), emoji.repeat(64)],
      [emoji.repeat(65), `${emoji.repeat(63)}~`],
      [`${'x'.repeat(62)}${emoji}`, `${'x'.repeat(62)}${emoji}`],
      [`${'x'.repeat(63)}${emoji}`, `${'x'.repeat(63)}${emoji}`],
      [`${'x'.repeat(64)}${emoji}`, `${'x'.repeat(63)}~`],
      [cjkExt.repeat(70), `${cjkExt.repeat(63)}~`],
      ['中'.repeat(70), `${'中'.repeat(63)}~`],
    ];
    for (const [input, want] of cases) expect(cell(input)).toBe(want);
  });

  test('whitespace is CPython whitespace, not JavaScript whitespace', () => {
    // CPython strips U+0085 and collapses a run of it; it does not strip
    // U+FEFF, which JavaScript's `\s` does.
    expect(cell('\x85 a \x85')).toBe('a');
    expect(cell('a\x85\x85b')).toBe('a b');
    expect(cell('﻿ a ﻿')).toBe('﻿ a ﻿');
  });

  test('pair is a dash when either half is absent, but not when either is null', () => {
    expect(pair(1, 2)).toBe('1/2');
    expect(pair(1, 2, ',')).toBe('1,2');
    expect(pair(MISSING, 2)).toBe('-');
    expect(pair(1, MISSING)).toBe('-');
    expect(pair(null, 2)).toBe('-/2');
  });

  test('position digs x and y out of an item', () => {
    expect(position({ x: 31, y: 72 })).toBe('31,72');
    expect(position({ x: 31 })).toBe('-');
  });
});

describe('dig', () => {
  test('an absent key is MISSING, a carried null is null', () => {
    expect(dig({ a: { b: null } }, 'a', 'b')).toBe(null);
    expect(dig({ a: {} }, 'a', 'b')).toBe(MISSING);
    expect(dig({ a: 1 }, 'a', 'b')).toBe(MISSING);
    expect(dig(null, 'a')).toBe(MISSING);
  });
});

describe('handles and aliases', () => {
  test('a handle strips the opaque prefix and caps the digits', () => {
    expect(handle('u', UNIT_A, 6)).toBe('u.aaaaaa');
    expect(handle('c', `city_${'c'.repeat(32)}`, 6)).toBe('c.cccccc');
    expect(handle('u', UNIT_A, 10)).toBe('u.aaaaaaaaaa');
    expect(handle('u', '12345', 6)).toBe('u.12345');
  });

  test('the CLI alias layer wins and the rest fall back to handles', () => {
    expect([...aliasMap('u', [UNIT_A, UNIT_B], { [UNIT_A]: 'u1' })]).toEqual([
      [UNIT_A, 'u1'],
      [UNIT_B, 'u.bbbbbb'],
    ]);
    expect([...aliasMap('u', [UNIT_A, UNIT_B], null)]).toEqual([
      [UNIT_A, 'u.aaaaaa'],
      [UNIT_B, 'u.bbbbbb'],
    ]);
  });

  test('a handle width counts code points', () => {
    expect(handle('u', '\u{1F600}'.repeat(20), 6)).toBe(`u.${'\u{1F600}'.repeat(6)}`);
  });

  // Regression: the alias table has to be read with own-key semantics.  A bare
  // index resolved a server-supplied id spelled `constructor` to
  // `Object.prototype.constructor` and rendered the host runtime's function
  // source into the alias column of `state/units.tsv`.
  test('an id that names an Object.prototype member falls back to a handle', () => {
    expect([
      ...aliasMap('u', ['toString', 'constructor', '__proto__', 'valueOf'], null),
    ]).toEqual([
      ['toString', 'u.toStri'],
      ['constructor', 'u.constr'],
      ['__proto__', '__proto__'],
      ['valueOf', 'u.valueO'],
    ]);
  });

  test('an inherited member is not an alias, but an own key of that name is', () => {
    expect([...aliasMap('u', ['toString'], {})]).toEqual([['toString', 'u.toStri']]);
    expect([...aliasMap('u', ['toString'], { toString: 'u1' })]).toEqual([
      ['toString', 'u1'],
    ]);
  });

  test('a colliding prefix widens the handle until it is unique', () => {
    const colliding = `unit_${'a'.repeat(31)}b`;
    expect([...aliasMap('u', [UNIT_A, colliding], null)]).toEqual([
      [UNIT_A, `u.${'a'.repeat(32)}`],
      [colliding, `u.${'a'.repeat(31)}b`],
    ]);
  });
});

describe('file names', () => {
  test('an alias can never escape the options directory', () => {
    expect(Effect.runSync(fileName('u1'))).toBe('u1');
    expect(Effect.runSync(fileName('../../etc/passwd'))).toBe('etc_passwd');
    expect(Effect.runSync(fileName('..--__a.b'))).toBe('a.b');
  });

  test('one astral character becomes one underscore, not two', () => {
    expect(Effect.runSync(fileName('a\u{1F600}b'))).toBe('a_b');
  });

  test('an alias with no usable name is refused', () => {
    const either = Effect.runSync(Effect.either(fileName('///')));
    expect(Either.isLeft(either) ? either.left.message : '').toBe(
      'state mirror: an actor alias has no usable file name'
    );
  });
});

describe('revisions', () => {
  test('the stamp line', () => {
    expect(revLine(null)).toBe('# rev ? turn ?');
    expect(revLine({ turn: 3, revision: 9 })).toBe('# rev 9 turn 3');
  });

  test('newer compares revision first, then turn', () => {
    expect(isNewer({ turn: 3, revision: 9 }, null)).toBe(true);
    expect(isNewer({ turn: 3, revision: 9 }, { turn: 3, revision: 7 })).toBe(true);
    expect(isNewer({ turn: 3, revision: 7 }, { turn: 3, revision: 9 })).toBe(false);
    expect(isNewer({ turn: 4, revision: 9 }, { turn: 3, revision: 9 })).toBe(true);
    expect(isNewer({ turn: 3, revision: 9 }, { turn: 3, revision: 9 })).toBe(false);
  });

  test('stale means the mirror already holds a strictly newer revision', () => {
    expect(isStale({ turn: 3, revision: 7 }, { turn: 3, revision: 9 })).toBe(true);
    expect(isStale({ turn: 3, revision: 9 }, { turn: 3, revision: 9 })).toBe(false);
    expect(isStale({ turn: 3, revision: 9 }, null)).toBe(false);
  });

  test('the re-verification line show prefixes a stale section with', () => {
    expect(staleLine({ turn: 3, revision: 7 }, { turn: 3, revision: 12 })).toBe(
      'stale: rendered at rev7, now rev12 — aliases will be re-verified by meaning on use'
    );
    expect(isBehind({ turn: 3, revision: 7 }, { turn: 3, revision: 12 })).toBe(true);
    expect(isBehind({ turn: 3, revision: 12 }, { turn: 3, revision: 12 })).toBe(false);
  });

  test('a payload without integer counters is refused', () => {
    const either = Effect.runSync(Effect.either(revisionPair({ state_token: 'x' })));
    expect(Either.isLeft(either) ? either.left.message : '').toBe(
      'state mirror: payload carries no state revision counters'
    );
  });

  test('a boolean is not an integer counter', () => {
    expect(
      Either.isLeft(Effect.runSync(Effect.either(revisionPair({ turn: true, revision: 9 }))))
    ).toBe(true);
  });

  test('a valid pair keeps turn and revision apart', () => {
    expect(
      Effect.runSync(revisionPair({ turn: 3, revision: 9, state_token: 'ignored' }))
    ).toEqual({ turn: 3, revision: 9 });
  });
});

// ---------------------------------------------------------------------------

describe('update_from_health', () => {
  test('the header is byte-identical to CPython and stamps the page revision', () => {
    const { dir, run } = freshMirror();
    const written = right(
      run(updateFromHealth(dir, 'turn', HEALTH, { revision: { turn: 3, revision: 9 } }))
    );
    expect(written.map((file) => path.relative(dir, file))).toEqual([
      path.join('state', 'header.txt'),
      path.join('state', 'phase.json'),
    ]);
    expect(readFile(dir, HEADER_FILE)).toBe(
      `${['# rev 9 turn 3', ...HEADER_BODY, ...DEFAULT_CARD].join('\n')}\n`
    );
  });

  test('a header without a known revision is marked unknown', () => {
    const { dir, run } = freshMirror();
    right(run(updateFromHealth(dir, 'health', HEALTH)));
    expect(readFile(dir, HEADER_FILE).startsWith('# rev ? turn ?')).toBe(true);
  });

  test('the header reuses the newest revision the mirror holds', () => {
    const { dir, run } = freshMirror();
    right(
      run(
        writeMirror(
          dir,
          OVERVIEW_FILE,
          tableText({ turn: 3, revision: 11 }, ['overview 1/1 complete'], ['fact', 'value'], [
            ['turn', '3'],
          ])
        )
      )
    );
    right(run(updateFromHealth(dir, 'health', HEALTH)));
    expect(readFile(dir, HEADER_FILE).startsWith('# rev 11 turn 3')).toBe(true);
  });

  test('the command card is caller supplied', () => {
    const { dir, run } = freshMirror();
    right(
      run(
        updateFromHealth(dir, 'turn', HEALTH, {
          revision: { turn: 3, revision: 9 },
          commands: ['just turn --end --await', 'just options u1'],
        })
      )
    );
    const text = readFile(dir, HEADER_FILE);
    expect(text).toContain('just turn --end --await');
    expect(text).toContain('just options u1');
    expect(text).not.toContain('just batch');
  });

  test('a drifted revision refuses the whole projection', () => {
    const { dir, run } = freshMirror();
    const either = run(
      updateFromHealth(dir, 'turn', HEALTH, { revision: { state_token: 'x' } })
    );
    expect(Either.isLeft(either) ? either.left.message : '').toBe(
      'state mirror: payload carries no state revision counters'
    );
    expect(fs.existsSync(path.join(dir, ...HEADER_FILE))).toBe(false);
  });
});

describe('phase.json', () => {
  const clock = (): number => 1786091354.086;

  test('the marker is the closed, key-sorted object a watcher parses', () => {
    expect(phaseMarker(HEALTH, null, clock)).toBe(
      [
        '{',
        '  "active": true,',
        '  "announced": null,',
        '  "deadline_s_left": 41,',
        '  "game_state": "running",',
        '  "held_s": 139,',
        '  "holder": null,',
        '  "phase": 0,',
        '  "schema_version": 1,',
        '  "state": "awaiting_agent",',
        '  "turn": 3,',
        '  "updated_at": 1786091354.086',
        '}',
      ].join('\n')
    );
  });

  test('one other waiting seat becomes the holder, and only when inactive', () => {
    const waiting = {
      ...HEALTH,
      phase: {
        ...HEALTH.phase,
        active: false,
        waiting_on: {
          seats: [
            {
              is_self: false,
              place: 2,
              seat_id: 'place-2',
              player_name: 'AgentPlace2',
              controller_label: 'pi-gpt',
            },
            { is_self: true, place: 1, seat_id: 'place-1', player_name: 'AgentPlace1' },
          ],
        },
      },
    };
    const marker = JSON.parse(phaseMarker(waiting, [1, 5, 0], clock)) as Record<
      string,
      unknown
    >;
    expect(marker['holder']).toEqual({
      controller_label: 'pi-gpt',
      place: 2,
      player_name: 'AgentPlace2',
      seat_id: 'place-2',
    });
    expect(marker['announced']).toEqual([1, 5, 0]);
    expect(marker['active']).toBe(false);
  });

  test('an active phase never names a holder', () => {
    const waiting = {
      ...HEALTH,
      phase: {
        ...HEALTH.phase,
        waiting_on: { seats: [{ is_self: false, place: 2, seat_id: 'place-2' }] },
      },
    };
    expect(
      (JSON.parse(phaseMarker(waiting, null, clock)) as Record<string, unknown>)['holder']
    ).toBe(null);
  });

  test('a malformed announcement is dropped rather than trusted', () => {
    for (const bad of [[1, 5], [1, 5, 0, 2], [1, 5, -1], [true, 5, 0], ['1', 5, 0], null]) {
      expect(
        (JSON.parse(phaseMarker(HEALTH, bad, clock)) as Record<string, unknown>)['announced']
      ).toBe(null);
    }
  });

  test('read_phase_marker refuses a marker it cannot parse', () => {
    const { dir, run } = freshMirror();
    expect(right(run(readPhaseMarker(dir)))).toBe(null);
    right(run(writeMirror(dir, PHASE_FILE, 'not json')));
    expect(right(run(readPhaseMarker(dir)))).toBe(null);
    right(run(writeMirror(dir, PHASE_FILE, '{"schema_version": 99}')));
    expect(right(run(readPhaseMarker(dir)))).toBe(null);
  });

  test('the monitor announcement survives an unrelated health write', () => {
    const { dir, run } = freshMirror();
    right(run(updatePhaseMarker(dir, HEALTH, { announced: [1, 5, 0] })));
    right(run(updateFromHealth(dir, 'health', HEALTH)));
    expect(right(run(readPhaseMarker(dir)))?.['announced']).toEqual([1, 5, 0]);
  });

  test('update_phase_marker inherits the announcement it is not given', () => {
    const { dir, run } = freshMirror();
    right(run(updatePhaseMarker(dir, HEALTH, { announced: [1, 5, 0] })));
    right(run(updatePhaseMarker(dir, HEALTH)));
    expect(right(run(readPhaseMarker(dir)))?.['announced']).toEqual([1, 5, 0]);
  });

  test('the monitor write touches no other projection', () => {
    const { dir, run } = freshMirror();
    right(run(updatePhaseMarker(dir, HEALTH)));
    expect(fs.readdirSync(path.join(dir, 'state'))).toEqual(['phase.json']);
  });
});

describe('monitor.log', () => {
  test('a line is stamped, sanitized, capped and appended', () => {
    const { dir, run } = freshMirror();
    const at = new Date(2026, 7, 7, 9, 5, 3);
    right(run(appendMonitorLog(dir, 'turn 5 opened', at)));
    right(run(appendMonitorLog(dir, `exec\nrm -rf /\t${'x'.repeat(600)}`, at)));
    const lines = readFile(dir, ['state', 'monitor.log']).split('\n').slice(0, -1);
    expect(lines[0]).toBe('2026-08-07T09:05:03 turn 5 opened');
    expect(lines[1]?.startsWith('2026-08-07T09:05:03 exec rm -rf / xxx')).toBe(true);
    expect(lines[1]?.length).toBe(19 + 1 + 512);
    expect(lines.length).toBe(2);
  });

  // Regression: `[:_MAX_LOG_LINE]` counts code points.  A UTF-16 slice split a
  // surrogate pair in half in every logged `--exec` string that carried one.
  test('the 512 cap counts code points and never splits a surrogate pair', () => {
    const { dir, run } = freshMirror();
    const at = new Date(2026, 7, 7, 9, 5, 3);
    right(run(appendMonitorLog(dir, `${'z'.repeat(500)}${'\u{1F600}'.repeat(20)}`, at)));
    const line = readFile(dir, ['state', 'monitor.log']).split('\n')[0] ?? '';
    const body = line.slice('2026-08-07T09:05:03 '.length);
    expect(body).toBe(`${'z'.repeat(500)}${'\u{1F600}'.repeat(12)}`);
    expect([...body].length).toBe(512);
    expect(Buffer.from(line, 'utf8').includes(Buffer.from('�', 'utf8'))).toBe(false);
  });

  test('the timestamp is local time in the CPython format', () => {
    expect(localTimestamp(new Date(2026, 0, 2, 3, 4, 5))).toBe('2026-01-02T03:04:05');
  });
});

describe('the client bridge', () => {
  test('mirror_text reads a projection and nothing else', () => {
    const { dir, sessionPath, run } = freshMirror();
    expect(right(run(mirrorText(sessionPath, HEADER_FILE)))).toBe(null);
    right(run(updateFromHealth(dir, 'turn', HEALTH, { revision: { turn: 3, revision: 9 } })));
    expect(right(run(mirrorText(sessionPath, HEADER_FILE)))?.startsWith('# rev 9 turn 3')).toBe(
      true
    );
  });

  test('cached_phase_note is silent while the phase is this seat\'s', () => {
    const { dir, sessionPath, run } = freshMirror();
    right(run(updateFromHealth(dir, 'turn', HEALTH, { revision: { turn: 3, revision: 9 } })));
    expect(right(run(cachedPhaseNote(sessionPath)))).toBe('');
  });

  test('cached_phase_note names an inactive awaiting_agent phase', () => {
    const { dir, sessionPath, run } = freshMirror();
    const idle = { ...HEALTH, phase: { ...HEALTH.phase, active: false } };
    right(run(updateFromHealth(dir, 'turn', idle, { revision: { turn: 3, revision: 9 } })));
    expect(right(run(cachedPhaseNote(sessionPath)))).toBe(
      'your phase is not active (state awaiting_agent) — just wait'
    );
  });

  test('cached_phase_note names every stalled phase state, active or not', () => {
    const { dir, sessionPath, run } = freshMirror();
    const ending = { ...HEALTH, phase: { ...HEALTH.phase, state: 'ending' } };
    right(run(updateFromHealth(dir, 'turn', ending, { revision: { turn: 3, revision: 9 } })));
    expect(right(run(cachedPhaseNote(sessionPath)))).toBe(
      'your phase is not active (state ending) — just wait'
    );
  });

  test('cached_phase_note stays silent on the lobby, where the remedy is start', () => {
    const { dir, sessionPath, run } = freshMirror();
    const lobby = { ...HEALTH, phase: { ...HEALTH.phase, state: 'synchronizing', active: false } };
    right(run(updateFromHealth(dir, 'turn', lobby, { revision: { turn: 3, revision: 9 } })));
    expect(right(run(cachedPhaseNote(sessionPath)))).toBe('');
  });

  test('cached_phase_note is silent when there is no mirror at all', () => {
    const { sessionPath, run } = freshMirror();
    expect(right(run(cachedPhaseNote(sessionPath)))).toBe('');
  });

  test('a projection failure never fails the command', () => {
    const { run } = freshMirror();
    expect(
      Either.isRight(run(mirrorGuard(Effect.fail(playerError('state mirror: injected')))))
    ).toBe(true);
    expect(Either.isRight(run(mirrorGuard(Effect.die(new Error('boom')))))).toBe(true);
  });
});
