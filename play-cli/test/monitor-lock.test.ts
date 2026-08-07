/**
 * The monitor's singleton lock.
 *
 * Ports the idempotency block of `test_client.py:11725-11821`:
 * `test_a_second_persistent_monitor_is_a_no_op_that_reports_the_first` (the
 * lock half), `test_the_singleton_holds_against_a_second_process_not_just_a_thread`,
 * `test_a_released_lock_leaves_nothing_for_the_next_monitor_to_reap` and
 * `test_once_takes_no_lock_so_the_two_bindings_compose` (the lock half).
 *
 * The property under test is not "a second monitor is refused" but *how*: by the
 * kernel, so that idempotency and crash recovery both come free and there is no
 * PID file to reap.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect } from 'effect';
import { hasNativeFlock, monitorLockPath } from 'src/services/locks';
import { monitorHolder, withMonitorLock } from 'src/services/monitor-lock';
import { scratchWorkspace, type Scratch } from 'test/_fixtures';

const scratches: Scratch[] = [];

afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

interface Bench {
  readonly scratch: Scratch;
  readonly sessionPath: string;
  readonly lockPath: string;
}

const bench = (): Bench => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const sessionPath = path.join(scratch.workspace.stateRoot, 'session-codex-gpt-5.6-sol.json');
  return { scratch, sessionPath, lockPath: monitorLockPath(sessionPath) };
};

describe('the monitor lock', () => {
  test('is a real flock, not a bookkeeping approximation', () => {
    // Every property below is the kernel's; if the binding ever silently fell
    // back to a sentinel file, crash recovery would quietly become a liveness
    // heuristic and this suite would still pass.
    expect(hasNativeFlock()).toBe(true);
  });

  test('the first acquires, and says so by yielding no holder', async () => {
    const seat = bench();
    const seen = await Effect.runPromise(
      Effect.provide(
        withMonitorLock(seat.sessionPath, { pid: 41207, since: '16:21:04', game_id: 'g' }, (running) =>
          Effect.gen(function* () {
            // From this process, a second probe sees the lock held: `flock` is
            // per open file description, so opening it again really does
            // contend.
            const holder = yield* monitorHolder(seat.sessionPath);
            return { running, holder };
          })
        ),
        seat.scratch.layer
      )
    );
    expect(seen.running).toBeNull();
    expect(seen.holder?.['pid']).toBe(41207);
    expect(seen.holder?.['since']).toBe('16:21:04');
  });

  test('a second persistent monitor is refused and told who holds it', async () => {
    const seat = bench();
    const running = await Effect.runPromise(
      Effect.provide(
        withMonitorLock(seat.sessionPath, { pid: 41207, since: '16:21:04' }, () =>
          withMonitorLock(seat.sessionPath, { pid: 999, since: '16:22:00' }, (second) =>
            Effect.succeed(second)
          )
        ),
        seat.scratch.layer
      )
    );
    expect(running).not.toBeNull();
    expect(running?.['pid']).toBe(41207);
    // The second one recorded nothing: the file still names the first.
    const after = await Effect.runPromise(
      Effect.provide(monitorHolder(seat.sessionPath), seat.scratch.layer)
    );
    expect(after).toBeNull();
  });

  test('a released lock leaves nothing for the next monitor to reap', async () => {
    const seat = bench();
    const held = await Effect.runPromise(
      Effect.provide(
        withMonitorLock(seat.sessionPath, { pid: 1234 }, () => monitorHolder(seat.sessionPath)),
        seat.scratch.layer
      )
    );
    expect(held).not.toBeNull();
    // Crash recovery is the kernel's job, so there is no stale state.
    expect(
      await Effect.runPromise(Effect.provide(monitorHolder(seat.sessionPath), seat.scratch.layer))
    ).toBeNull();
    expect(fs.statSync(seat.lockPath).mode & 0o777).toBe(0o600);
  });

  test('the lock file is the holder record, and is emptied on release', async () => {
    const seat = bench();
    await Effect.runPromise(
      Effect.provide(
        withMonitorLock(seat.sessionPath, { pid: 4242, since: '09:00:00' }, () =>
          Effect.sync(() => {
            const raw = fs.readFileSync(seat.lockPath, 'utf8');
            expect(JSON.parse(raw)).toEqual({ pid: 4242, since: '09:00:00' });
            // `json.dumps(holder, sort_keys=True)`: keys sorted, CPython's
            // default separators.
            expect(raw).toBe('{"pid": 4242, "since": "09:00:00"}');
          })
        ),
        seat.scratch.layer
      )
    );
    expect(fs.readFileSync(seat.lockPath, 'utf8')).toBe('');
  });

  test('a monitor lock that is not mode 0600 is refused', async () => {
    const seat = bench();
    fs.mkdirSync(path.dirname(seat.lockPath), { recursive: true, mode: 0o700 });
    fs.writeFileSync(seat.lockPath, '', { mode: 0o644 });
    fs.chmodSync(seat.lockPath, 0o644);
    const either = await Effect.runPromise(
      Effect.either(
        Effect.provide(
          withMonitorLock(seat.sessionPath, { pid: 1 }, () => Effect.void),
          seat.scratch.layer
        )
      )
    );
    expect(either._tag).toBe('Left');
    if (either._tag === 'Left') {
      expect(either.left.message).toBe('the monitor lock must be a mode-0600 file');
    }
  });

  test('no lock file at all means no monitor', async () => {
    const seat = bench();
    expect(
      await Effect.runPromise(Effect.provide(monitorHolder(seat.sessionPath), seat.scratch.layer))
    ).toBeNull();
  });
});

// ---------------------------------------------------------------------------
// The case that actually matters: two processes, not two closures
// ---------------------------------------------------------------------------

const holderScript = (seat: Bench, seconds: number): string => {
  const moduleRoot = path.resolve(import.meta.dir, '..');
  return [
    `const { Effect, Layer } = await import('effect');`,
    `const lock = await import(${JSON.stringify(path.join(moduleRoot, 'src/services/monitor-lock.ts'))});`,
    `const files = await import(${JSON.stringify(path.join(moduleRoot, 'src/services/private-fs.ts'))});`,
    `const api = files.privateFsFor({ root: ${JSON.stringify(seat.scratch.workspace.root)}, stateRoot: ${JSON.stringify(seat.scratch.workspace.stateRoot)} });`,
    `const layer = Layer.succeed(files.PrivateFs, api);`,
    `await Effect.runPromise(Effect.provide(lock.withMonitorLock(${JSON.stringify(seat.sessionPath)}, { pid: process.pid }, (running) =>`,
    `  Effect.promise(async () => {`,
    `    process.stdout.write((running ? 'blocked' : 'acquired') + '\\n');`,
    `    await new Promise((resolve) => setTimeout(resolve, ${Math.round(seconds * 1000)}));`,
    `  })`,
    `), layer));`,
  ].join('\n');
};

const firstLine = async (stream: ReadableStream<Uint8Array>): Promise<string> => {
  const reader = stream.getReader();
  let buffer = '';
  for (;;) {
    const chunk = await reader.read();
    if (chunk.done) break;
    buffer += new TextDecoder().decode(chunk.value);
    const newline = buffer.indexOf('\n');
    if (newline >= 0) {
      reader.releaseLock();
      return buffer.slice(0, newline).trim();
    }
  }
  reader.releaseLock();
  return buffer.trim();
};

describe('the singleton', () => {
  test('holds against a second process, not just a second closure', async () => {
    // `flock` is per-open-file-description, so in-process agreement proves
    // nothing about the case that matters: two `play monitor` invocations from
    // a shell.
    const seat = bench();
    const cwd = path.resolve(import.meta.dir, '..');
    const first = Bun.spawn(['bun', '-e', holderScript(seat, 5)], {
      cwd,
      stdout: 'pipe',
      stderr: 'pipe',
    });
    try {
      expect(await firstLine(first.stdout)).toBe('acquired');
      const second = Bun.spawnSync(['bun', '-e', holderScript(seat, 0)], {
        cwd,
        stdout: 'pipe',
        stderr: 'pipe',
      });
      expect(second.stdout.toString().trim()).toBe('blocked');
      const holder = await Effect.runPromise(
        Effect.provide(monitorHolder(seat.sessionPath), seat.scratch.layer)
      );
      expect(holder?.['pid']).toBe(first.pid);
    } finally {
      first.kill('SIGTERM');
      await first.exited;
    }
    // The kernel released it when the holder died: nothing to reap.
    expect(
      await Effect.runPromise(Effect.provide(monitorHolder(seat.sessionPath), seat.scratch.layer))
    ).toBeNull();
    const third = Bun.spawnSync(['bun', '-e', holderScript(seat, 0)], {
      cwd: path.resolve(import.meta.dir, '..'),
      stdout: 'pipe',
      stderr: 'pipe',
    });
    expect(third.stdout.toString().trim()).toBe('acquired');
  }, 60_000);
});
