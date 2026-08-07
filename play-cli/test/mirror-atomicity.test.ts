/**
 * The mirror's two safety properties, ported first because everything else in
 * U04 depends on them: `AtomicityTests` and `LeakTests` from
 * `play/tests/test_state_mirror.py:1074-1173`.
 *
 * A crash mid-write must leave the prior file byte-identical and no temp file
 * behind, and nothing private — a bearer token, an invite, a `state_token` —
 * may ever reach a projection.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, spyOn, test } from 'bun:test';
import { Effect, Either } from 'effect';
import type { PrivateFs } from 'src/services/private-fs';
import {
  DELTA_FILE,
  HEADER_FILE,
  PHASE_FILE,
  mirrorDir,
  tableText,
  updateFromHealth,
  writeMirror,
} from 'src/services/mirror';
import { scratchWorkspace, type Scratch } from 'test/_fixtures';

const GAME_ID = 'game_12345678901234567890';
const SECRET = 'v2-agent-secret-bearer-token';

const scratches: Scratch[] = [];

afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

interface Mirror {
  readonly dir: string;
  readonly run: <A, E>(effect: Effect.Effect<A, E, PrivateFs>) => Either.Either<A, E>;
}

const freshMirror = (): Mirror => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const sessionPath = path.join(scratch.workspace.stateRoot, GAME_ID, 'codex-test.json');
  const dir = Effect.runSync(mirrorDir(sessionPath));
  return {
    dir,
    run: (effect) => Effect.runSync(Effect.either(Effect.provide(effect, scratch.layer))),
  };
};

const stateEntries = (dir: string): ReadonlyArray<string> => {
  try {
    return fs.readdirSync(path.join(dir, 'state')).sort();
  } catch {
    return [];
  }
};

const REVISION = { turn: 3, revision: 9 } as const;

const unitsTable = (moves: string): string =>
  tableText(REVISION, ['units 1/1 complete'], ['alias', 'unit', 'moves'], [
    ['u1', 'Settlers', moves],
  ]);

describe('atomicity', () => {
  test('a failed write leaves the previous file and no partial', () => {
    const { dir, run } = freshMirror();
    expect(Either.isRight(run(writeMirror(dir, ['state', 'units.tsv'], unitsTable('3/3'))))).toBe(
      true
    );
    const before = fs.readFileSync(path.join(dir, 'state', 'units.tsv'), 'utf8');

    const rename = spyOn(fs, 'renameSync').mockImplementation(() => {
      throw new Error('injected');
    });
    const failed = run(writeMirror(dir, ['state', 'units.tsv'], unitsTable('1/3')));
    rename.mockRestore();

    expect(Either.isLeft(failed)).toBe(true);
    expect(fs.readFileSync(path.join(dir, 'state', 'units.tsv'), 'utf8')).toBe(before);
    expect(stateEntries(dir).filter((name) => name.startsWith('.'))).toEqual([]);
  });

  test('a failed first write creates no file at all', () => {
    const { dir, run } = freshMirror();
    const rename = spyOn(fs, 'renameSync').mockImplementation(() => {
      throw new Error('injected');
    });
    const failed = run(writeMirror(dir, ['state', 'cities.tsv'], unitsTable('3/3')));
    rename.mockRestore();

    expect(Either.isLeft(failed)).toBe(true);
    expect(fs.existsSync(path.join(dir, 'state', 'cities.tsv'))).toBe(false);
    expect(stateEntries(dir)).toEqual([]);
  });

  test('the failure is a state-mirror PlayerError, never a thrown exception', () => {
    const { dir, run } = freshMirror();
    const rename = spyOn(fs, 'renameSync').mockImplementation(() => {
      throw new Error('injected');
    });
    const failed = run(writeMirror(dir, ['state', 'units.tsv'], unitsTable('3/3')));
    rename.mockRestore();
    expect(Either.isLeft(failed) ? failed.left._tag : '').toBe('PlayerError');
  });

  test('a rename that fails over an occupied name leaks no temp file', () => {
    const { dir, run } = freshMirror();
    // A directory at the destination makes `rename(2)` fail after the temp file
    // already exists — the one ordering that could leak.
    fs.mkdirSync(path.join(dir, 'state', 'units.tsv'), { recursive: true });
    fs.writeFileSync(path.join(dir, 'state', 'units.tsv', 'occupied'), 'x');

    const failed = run(writeMirror(dir, ['state', 'units.tsv'], unitsTable('3/3')));
    expect(Either.isLeft(failed)).toBe(true);
    expect(stateEntries(dir).filter((name) => name.startsWith('.'))).toEqual([]);
  });
});

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
      deadline_started_at: 10.0,
      deadline_at: 190.0,
      elapsed_s: 139.0,
      remaining_s: 41.0,
    },
  },
  last_phase_end: null,
  objective: 'Maximize final Freeciv civilization score.',
  max_turns: 5000,
  turns_remaining: 4997,
} as const;

const STATE_TOKEN = `state_token_${'5'.repeat(20)}`;

describe('leaks', () => {
  test('no token or private state reaches the mirror', () => {
    const { dir, run } = freshMirror();
    const poisoned = {
      ...HEALTH,
      agent: { ...HEALTH.agent, agent_token: SECRET },
      sidecar: { ...HEALTH.sidecar, error_code: SECRET },
      authorization: `Bearer ${SECRET}`,
      invite: `invite_${'7'.repeat(32)}`,
    };
    const written = run(
      updateFromHealth(dir, 'turn', poisoned, {
        revision: { turn: 3, revision: 9, state_token: STATE_TOKEN },
      })
    );
    expect(Either.isRight(written)).toBe(true);
    expect(Either.isRight(written) ? written.right.length : 0).toBe(2);

    for (const relative of [HEADER_FILE, PHASE_FILE]) {
      const text = fs.readFileSync(path.join(dir, ...relative), 'utf8');
      expect(text).not.toContain(SECRET);
      expect(text).not.toContain('Bearer');
      expect(text).not.toContain('invite_');
      expect(text).not.toContain(STATE_TOKEN);
      expect(text).not.toContain('state_token');
    }
  });

  test('mirror files are private to the seat', () => {
    const { dir, run } = freshMirror();
    expect(
      Either.isRight(run(writeMirror(dir, ['state', 'units.tsv'], unitsTable('3/3'))))
    ).toBe(true);
    expect(
      Either.isRight(run(updateFromHealth(dir, 'turn', HEALTH, { revision: REVISION })))
    ).toBe(true);
    expect(Either.isRight(run(writeMirror(dir, DELTA_FILE, '# rev 9 turn 3\n')))).toBe(true);

    for (const relative of [
      ['state', 'units.tsv'],
      HEADER_FILE,
      PHASE_FILE,
      DELTA_FILE,
    ]) {
      const mode = fs.statSync(path.join(dir, ...relative)).mode & 0o777;
      expect([relative.join('/'), mode.toString(8)]).toEqual([relative.join('/'), '600']);
    }
  });

  test('a hostile value can never forge a header line', () => {
    const { dir, run } = freshMirror();
    const hostile = { ...HEALTH, game_state: 'run\nning\t# rev 999 turn 999' };
    expect(
      Either.isRight(run(updateFromHealth(dir, 'turn', hostile, { revision: REVISION })))
    ).toBe(true);
    const text = fs.readFileSync(path.join(dir, ...HEADER_FILE), 'utf8');
    expect(text.split('\n').filter((line) => line.startsWith('#'))).toEqual([
      '# rev 9 turn 3',
    ]);
    expect(text).toContain('run ning # rev 999 turn 999');
  });
});
