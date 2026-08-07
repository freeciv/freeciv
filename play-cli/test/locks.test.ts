/**
 * Advisory locks.
 *
 * The mechanism matters as much as the behaviour: `flock(2)` gives idempotency
 * and crash recovery for free, and the first test fails loudly if the binding
 * silently degraded to the sentinel fallback on this platform.
 */
import { afterEach, describe, expect, test } from 'bun:test';
import * as fs from 'node:fs';
import * as path from 'node:path';
import { Effect, Either, Layer } from 'effect';
import { PrivateFs } from 'src/services/private-fs';
import {
  hasNativeFlock,
  monitorLockPath,
  v2RequestLockPath,
  v2StateLockPath,
  v2StatePath,
  withAdvisoryLock,
  withSuffix,
} from 'src/services/locks';
import { scratchWorkspace, type Scratch } from 'test/_fixtures';

const scratches: Scratch[] = [];

const fresh = (): Scratch => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  return scratch;
};

afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

describe('path derivation', () => {
  test('withSuffix mirrors pathlib.Path.with_suffix', () => {
    expect(withSuffix('/a/b/seat.json', '.v2-state')).toBe('/a/b/seat.v2-state');
    expect(withSuffix('/a/b/seat', '.v2-state')).toBe('/a/b/seat.v2-state');
    expect(withSuffix('/a.b/seat.json', '.lock')).toBe('/a.b/seat.lock');
  });

  test('the four session-derived paths are siblings of the session file', () => {
    expect(v2StatePath('/w/.sessions/g/seat.json')).toBe('/w/.sessions/g/seat.v2-state');
    expect(v2StateLockPath('/w/.sessions/g/seat.json')).toBe('/w/.sessions/g/seat.v2-state.lock');
    expect(v2RequestLockPath('/w/.sessions/g/seat.json')).toBe(
      '/w/.sessions/g/seat.v2-request.lock'
    );
    expect(monitorLockPath('/w/.sessions/g/seat.json')).toBe('/w/.sessions/g/seat.monitor.lock');
  });
});

describe('holding', () => {
  test('flock(2) is bound, so the lock is kernel-backed rather than a PID file', () => {
    expect(hasNativeFlock()).toBe(true);
  });

  test('the body runs and the lock file is left mode 0600', async () => {
    const scratch = fresh();
    const target = path.join(scratch.workspace.stateRoot, 'seat.v2-state.lock');
    const result = await Effect.runPromise(
      withAdvisoryLock(target, 1, Effect.succeed('done')).pipe(
        Effect.provide(Layer.succeed(PrivateFs, scratch.files))
      )
    );
    expect(result).toBe('done');
    expect(fs.statSync(target).mode & 0o777).toBe(0o600);
  });

  test('the lock is released even when the body fails', async () => {
    const scratch = fresh();
    const target = path.join(scratch.workspace.stateRoot, 'seat.v2-state.lock');
    const provided = Layer.succeed(PrivateFs, scratch.files);
    const first = await Effect.runPromise(
      Effect.either(
        withAdvisoryLock(target, 1, Effect.fail('boom' as const)).pipe(Effect.provide(provided))
      )
    );
    expect(Either.isLeft(first)).toBe(true);
    // If the release had leaked, this second acquisition would time out.
    const second = await Effect.runPromise(
      withAdvisoryLock(target, 1, Effect.succeed(2)).pipe(Effect.provide(provided))
    );
    expect(second).toBe(2);
  });

  test('a lock outside PLAY_STATE_DIR is refused before any file is opened', async () => {
    const scratch = fresh();
    const either = await Effect.runPromise(
      Effect.either(
        withAdvisoryLock('/etc/play.lock', 1, Effect.succeed(0)).pipe(
          Effect.provide(Layer.succeed(PrivateFs, scratch.files))
        )
      )
    );
    expect(Either.isLeft(either)).toBe(true);
    if (Either.isLeft(either)) {
      expect(either.left).toMatchObject({
        message: 'private state files must stay inside PLAY_STATE_DIR',
      });
    }
  });
});
