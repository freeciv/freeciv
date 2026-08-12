/**
 * SPIKE S1 — can Bun hand an arbitrary fd to a child process?
 *
 * The Python sidecar builds a `socketpair(AF_UNIX, SOCK_STREAM)`, marks the
 * child end inheritable, and passes it through `subprocess.Popen(pass_fds=...)`
 * together with `--ipc-fd <n>` (`agent_eval/headless_sidecar.py:1156-1176`).
 * The C client then talks framed IPC over that fd
 * (`client/gui-agent/gui_main.c:851`).  If Bun could not reproduce that, the
 * port would need a Unix-socket-path handshake and a change on the C side.
 *
 * These tests stand in for the C client with a `python3 -c` one-liner that
 * reads and writes fd 3, and assert the nonce round-trips both directions.
 *
 * Result: it works.  Two facts do the load-bearing work, and each has its own
 * test so a Bun upgrade that breaks one names which one:
 *
 *   1. `Bun.spawn`'s `stdio` array accepts a raw fd at index >= 3, and
 *      *remaps* it to that index in the child — pass a fd at `stdio[3]` and
 *      the child reads it as fd 3, whatever number it had here.  This is
 *      nicer than `pass_fds`, which preserves the parent's fd number and so
 *      forces `--ipc-fd` to be computed at spawn time.
 *   2. The parent must `close(2)` its copy of the child end right after
 *      spawning, exactly as the Python does.  Skip it and the parent never
 *      observes EOF, because it is still holding the peer open itself.  The
 *      first draft of this spike hung for two minutes on precisely that.
 */
import { describe, expect, test } from 'bun:test';
import { Effect, Exit } from 'effect';
import {
  closeFd,
  decode,
  encode,
  isFdPairError,
  openCursor,
  openFdPair,
  openWriter,
  readExactly,
  type FdPair,
} from './fd-channel';

/**
 * Fail at the edge rather than threading the error union through every test.
 *
 * The `expect` below throws on a failed `socketpair`, so the trailing return
 * is unreachable — but it keeps the signature honest without a cast.
 */
const requirePair = (): FdPair => {
  const pair = openFdPair();
  if (isFdPairError(pair)) {
    expect(`socketpair failed, errno ${pair.errno}`).toBe('socketpair succeeded');
    return { parent: -1, child: -1 };
  }
  return pair;
};

/**
 * The child: a python3 one-liner talking fd 3, i.e. what the real C client
 * does after parsing `--ipc-fd 3`.
 *
 * Bun types `stdio` as `[In, Out, Err, ...Readable[]]`, so the extra fds need
 * no cast — the tail is a documented part of the signature.
 */
const spawnChild = (
  source: string,
  extraFds: readonly number[],
): Bun.Subprocess<'ignore', 'pipe', 'pipe'> =>
  Bun.spawn({
    cmd: ['python3', '-c', source],
    // Each fd lands at the index it occupies here: index 3 -> child fd 3.
    stdio: ['ignore', 'pipe', 'pipe', ...extraFds],
  });

describe('S1: Bun.spawn extra-fd inheritance', () => {
  test('a socketpair child end passed at stdio[3] arrives as the child fd 3', async () => {
    const nonce = `S1-${Bun.randomUUIDv7()}`;
    const pair = requirePair();
    const child = spawnChild(`import os; os.write(3, b'${nonce}')`, [pair.child]);

    // Drop the parent's copy of the child end, or the read below never EOFs.
    expect(closeFd(pair.child)).toBe(0);

    const [bytes] = await readExactly(openCursor(pair.parent), nonce.length);
    const status = await child.exited;
    const stderr = await new Response(child.stderr).text();
    closeFd(pair.parent);

    expect(stderr).toBe('');
    expect(status).toBe(0);
    expect(decode(bytes)).toBe(nonce);
  }, 15_000);

  test('traffic flows both ways over one inherited socketpair fd', async () => {
    const nonce = `S1-${Bun.randomUUIDv7()}`;
    const pair = requirePair();
    // Echo three frames back with a prefix: proves the fd is a full-duplex
    // socket, and that the parent's reader survives across separate reads.
    const child = spawnChild(
      [
        'import os',
        'def recv(n):',
        '    b = b""',
        '    while len(b) < n: b += os.read(3, n - len(b))',
        '    return b',
        `for i in range(3): os.write(3, b"P" + recv(${nonce.length}))`,
      ].join('\n'),
      [pair.child],
    );
    expect(closeFd(pair.child)).toBe(0);

    const writer = openWriter(pair.parent);
    const cursor0 = openCursor(pair.parent);

    // Three sequential round trips, threaded functionally rather than looped.
    const round = async (
      cursor: Awaited<ReturnType<typeof readExactly>>[1],
    ): Promise<readonly [string, typeof cursor]> => {
      await writer.send(encode(nonce));
      const [bytes, next] = await readExactly(cursor, nonce.length + 1);
      return [decode(bytes), next];
    };
    const [first, cursor1] = await round(cursor0);
    const [second, cursor2] = await round(cursor1);
    const [third] = await round(cursor2);

    const status = await child.exited;
    const stderr = await new Response(child.stderr).text();
    closeFd(pair.parent);

    expect(stderr).toBe('');
    expect(status).toBe(0);
    expect([first, second, third]).toEqual([`P${nonce}`, `P${nonce}`, `P${nonce}`]);
  }, 15_000);

  test('two extra fds land at 3 and 4, in stdio-array order', async () => {
    const a = requirePair();
    const b = requirePair();
    const child = spawnChild(
      'import os; os.write(3, b"THREE"); os.write(4, b"FOUR")',
      [a.child, b.child],
    );
    expect(closeFd(a.child)).toBe(0);
    expect(closeFd(b.child)).toBe(0);

    const [onThree] = await readExactly(openCursor(a.parent), 5);
    const [onFour] = await readExactly(openCursor(b.parent), 4);
    const status = await child.exited;
    closeFd(a.parent);
    closeFd(b.parent);

    expect(status).toBe(0);
    expect([decode(onThree), decode(onFour)]).toEqual(['THREE', 'FOUR']);
  }, 15_000);

  test('holding the parent copy of the child fd withholds EOF, not data', async () => {
    // The negative control for fact (2). The bytes still arrive — it is only
    // end-of-stream that never comes, because this process is itself still
    // holding the peer end open. Anything that reads to EOF (`Bun.file(fd)
    // .text()`, `new Response(stream).text()`) therefore hangs forever rather
    // than failing, which is exactly how it wastes an afternoon.
    const pair = requirePair();
    const child = spawnChild("import os; os.write(3, b'HI')", [pair.child]);
    expect(await child.exited).toBe(0);
    // Deliberately NOT closing pair.child here.

    const cursor = openCursor(pair.parent);
    // The two bytes the child sent do arrive.
    const [sent, after] = await readExactly(cursor, 2);
    expect(decode(sent)).toBe('HI');

    // Asking for one more byte should see EOF instantly — the child is gone.
    // It does not, because this process still holds the peer end.
    const outcome = await Promise.race([
      readExactly(after, 1).then(([extra]) => `eof after ${extra.length} bytes`),
      Bun.sleep(400).then(() => '<no EOF>'),
    ]);
    expect(outcome).toBe('<no EOF>');

    // Closing the copy is what releases it; the pending read then reports EOF.
    expect(closeFd(pair.child)).toBe(0);
    const [extra] = await readExactly(after, 1);
    expect(extra.length).toBe(0);
    closeFd(pair.parent);
  }, 15_000);

  test('the whole exchange composes under Effect with a scoped fd lifetime', async () => {
    const nonce = `S1-${Bun.randomUUIDv7()}`;

    const scopedPair = Effect.acquireRelease(
      Effect.sync(requirePair),
      (pair) => Effect.sync(() => closeFd(pair.parent)),
    );

    const program = Effect.gen(function* () {
      const pair = yield* scopedPair;
      const child = yield* Effect.sync(() =>
        spawnChild(`import os; os.write(3, b'${nonce}')`, [pair.child]),
      );
      yield* Effect.sync(() => closeFd(pair.child));
      const [bytes] = yield* Effect.promise(() =>
        readExactly(openCursor(pair.parent), nonce.length),
      );
      const status = yield* Effect.promise(() => child.exited);
      return { echoed: decode(bytes), status };
    });

    const exit = await Effect.runPromiseExit(Effect.scoped(program));
    expect(Exit.isSuccess(exit)).toBe(true);
    expect(Exit.isSuccess(exit) ? exit.value : undefined).toEqual({
      echoed: nonce,
      status: 0,
    });
  }, 15_000);
});
