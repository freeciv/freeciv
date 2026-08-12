/**
 * The two behavioural upgrades PLAN.md allows, under adversarial conditions.
 *
 * There is no CPython counterpart for either of these, which is the point:
 * they are the only two things this port does that `play/client.py` did not,
 * so they are the two things nothing else can catch.
 *
 * 1. **Concurrent drains, deterministic output.**  `do` fetches up to four
 *    catalogs at once now.  The test randomises per-request latency across
 *    many runs and asserts stdout is byte-identical every time — including
 *    the `fetched uN options (revM)` notes, whose order used to be the order
 *    the responses arrived in and must now be the order the orders were
 *    given.  A concurrency bug that only shows up when the third response
 *    beats the first is otherwise invisible until a live match.
 *
 * 2. **The streamed receipt ledger.**  A process killed mid-batch has to
 *    leave a readable record of exactly the orders that landed.  The test
 *    kills after order 2 of 4 and asserts `state/receipts.log` holds exactly
 *    two complete, parseable lines — not one and a fragment.
 */
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Layer } from 'effect';
import { ledgerEntry, readSessionLedger, recordReceipt } from 'src/services/receipt-ledger';
import { distinctActors, drainActors, fetchedOptionsNote } from 'src/services/do-drain';
import { playerError } from 'src/errors';
import { isJsonObject, type JsonObject } from 'src/schema/primitives';
import type { Revision } from 'src/schema/revision';
import { PrivateFs, type PrivateFsApi } from 'src/services/private-fs';
import {
  bench,
  dispositionOf,
  doArgs,
  move,
  rev,
  runDoCaptured,
  type Bench,
  type FakeAction,
} from 'test/_do-harness';

/**
 * A file layer whose append *throws* instead of failing.
 *
 * `PrivateFsApi.appendText` runs `fstat`/`fchmod`/`write` inside a bare
 * `try/finally` rather than an `Effect.try`, so a real `ENOSPC`, `EDQUOT`,
 * `EIO` or `EPERM` arrives as an Effect **defect**, not a typed error — and a
 * defect walks straight past `Effect.ignore`.  No temporary directory can be
 * made to run out of space on demand, so the throw is injected here instead.
 */
const explodingAppend = (api: PrivateFsApi): PrivateFsApi => ({
  ...api,
  appendText: () =>
    Effect.sync(() => {
      throw new Error('ENOSPC: no space left on device');
    }),
});

const benches: Bench[] = [];

const fresh = (): Bench => {
  const made = bench();
  benches.push(made);
  return made;
};

afterEach(() => {
  while (benches.length > 0) benches.pop()?.scratch.cleanup();
});

/** Four units, `u1`..`u4`, each with one distinguishable move. */
const ACTORS: ReadonlyArray<string> = [
  `unit_${'1'.repeat(32)}`,
  `unit_${'2'.repeat(32)}`,
  `unit_${'3'.repeat(32)}`,
  `unit_${'4'.repeat(32)}`,
];

const actionFor = (index: number): FakeAction =>
  move(`action_${String(index).padStart(26, 'f')}`, ACTORS[index] ?? '', 30 + index, 70 + index);

const ORDERS = ACTORS.map((_actor, index) => `u${index + 1} move ${30 + index},${70 + index}`).join(
  '; '
);

/**
 * Stage four seats whose *aliases* are known and whose *catalogs* are not.
 *
 * That is the state a first `do` of a turn is really in: `turn` printed the
 * units, so `u1`..`u4` resolve, but no capability was ever enumerated.  It is
 * reached here the way it is reached live — a revision bump retires the cached
 * actions and the drain record while the entity aliases survive.
 */
const coldSeats = (kit: Bench): void => {
  const learning = rev(7);
  kit.world.revision = learning;
  ACTORS.forEach((actor, index) => {
    kit.seed(actor, [actionFor(index)], learning);
  });
  const now = rev(8);
  kit.bump(now);
  ACTORS.forEach((actor, index) => {
    kit.world.catalogs.set(actor, [actionFor(index)]);
  });
  kit.world.receipt = () => ({ state: 'applied', revision: now });
};

/** A small deterministic PRNG, so a failing seed is a reproducible failure. */
const rng = (seed: number): (() => number) => {
  let value = seed >>> 0;
  return () => {
    value = (value * 1664525 + 1013904223) >>> 0;
    return value / 0x100000000;
  };
};

describe('do-drain — the ordering primitives', () => {
  test('drainActors reports in input order however the responses land', async () => {
    const order = ['c', 'a', 'b'];
    const delays: Readonly<Record<string, number>> = { a: 6, b: 3, c: 9 };
    const finished: string[] = [];
    const outcomes = await Effect.runPromise(
      drainActors(order, (actorId) =>
        Effect.gen(function* () {
          yield* Effect.sleep(`${delays[actorId] ?? 0} millis`);
          finished.push(actorId);
          return rev(7);
        })
      )
    );
    // The responses landed fastest-first; the report is still input order.
    expect(finished).toEqual(['b', 'a', 'c']);
    expect(outcomes.map((outcome) => outcome.actorId)).toEqual(['c', 'a', 'b']);
    expect(outcomes.every((outcome) => outcome.failure === null)).toBe(true);
  });

  test('a failure closes the gate on every later drain, so none of them commits', async () => {
    // `_resolve_orders_fetching` and `_refresh_orders` are sequential `for`
    // loops that let `_drain_legal_unlocked` raise, so the actors after the
    // failing one are never fetched *and never ingested*.  Concurrency may
    // spend the redundant round trip — a GET that keeps nothing is invisible —
    // but it may not keep what the trip read: a committed catalog joins
    // `drained_actors` and consumes `a1..aN`, which silently renumbers the
    // aliases the next `just legal` prints.
    const committed: string[] = [];
    const outcomes = await Effect.runPromise(
      drainActors(['a', 'b', 'c'], (actorId, gate) =>
        actorId === 'a'
          ? Effect.fail(playerError('a is unreadable'))
          : gate(
              Effect.sync(() => {
                committed.push(actorId);
                return rev(7);
              })
            )
      )
    );
    expect(committed).toEqual([]);
    expect(outcomes[0]?.failure?.message).toBe('a is unreadable');
    expect(outcomes[0]?.skipped).toBe(false);
    expect(outcomes[1]).toMatchObject({ actorId: 'b', failure: null, skipped: true });
    expect(outcomes[2]).toMatchObject({ actorId: 'c', failure: null, skipped: true });
  });

  test('a drain that ignores its gate is still reported as never issued', async () => {
    // Belt and braces: the gate is the contract, but a row that claimed a
    // commit CPython never made would be a lie the caller acts on.
    const outcomes = await Effect.runPromise(
      drainActors(['a', 'b'], (actorId) =>
        actorId === 'a' ? Effect.fail(playerError('nope')) : Effect.succeed(rev(7))
      )
    );
    expect(outcomes[1]).toMatchObject({ actorId: 'b', revision: null, skipped: true });
  });

  test('`stopOnFailure: false` is the refusal loop: a failure skips only itself', async () => {
    // `_refused_actor_options` (client.py:9433-9440) catches per actor and
    // `continue`s, so the later menus are still fetched and still printed.
    const committed: string[] = [];
    const outcomes = await Effect.runPromise(
      drainActors(
        ['a', 'b', 'c'],
        (actorId, gate) =>
          actorId === 'a'
            ? Effect.fail(playerError('a is unreadable'))
            : gate(
                Effect.sync(() => {
                  committed.push(actorId);
                  return rev(7);
                })
              ),
        { stopOnFailure: false }
      )
    );
    expect(committed).toEqual(['b', 'c']);
    expect(outcomes[0]?.failure?.message).toBe('a is unreadable');
    expect(outcomes.every((outcome) => ! outcome.skipped)).toBe(true);
  });

  test('a genuine defect out of a drain is never captured as a row', async () => {
    // `Effect.match` let defects and interruption through, and so must the
    // cause-matching that replaced it: only the private closed-gate marker is
    // converted, everything else is the caller's to see.
    const exit = await Effect.runPromiseExit(
      drainActors(['a'], () =>
        Effect.sync((): Revision => {
          throw new Error('EIO: i/o error');
        })
      )
    );
    expect(exit._tag).toBe('Failure');
  });

  test('distinctActors keeps first-seen order and keeps the empty actor', () => {
    // `_refresh_orders` dedupes with a plain `set` and drains whatever is in
    // it, `""` included — that drain is the global catalog re-enumeration an
    // actorless order (`end`, `research set …`) depends on.  Dropping it is a
    // silent "no longer offers …" refusal for an order CPython re-issues.
    expect(distinctActors(['u2', '', 'u1', 'u2', 'u3', ''])).toEqual(['u2', '', 'u1', 'u3']);
    expect(distinctActors(['', ''])).toEqual(['']);
  });

  test('the fetch note carries the revision it actually fetched', () => {
    expect(fetchedOptionsNote('u1', rev(7))).toBe('fetched u1 options (rev7)');
    expect(fetchedOptionsNote('u1', null)).toBe('fetched u1 options');
  });
});

describe('do — determinism under randomised latency', () => {
  test('stdout is byte-identical across 24 runs with shuffled response times', async () => {
    const transcripts = new Set<string>();
    const issueOrders = new Set<string>();

    for (let attempt = 0; attempt < 24; attempt += 1) {
      const kit = fresh();
      coldSeats(kit);
      const random = rng(attempt * 7919 + 13);
      kit.world.latency = () => Math.floor(random() * 6);

      const run = await runDoCaptured(kit, doArgs(ORDERS, { continueOnError: true }));

      expect(run.code).toBe(0);
      transcripts.add(run.out.join('\n'));
      // Every drain was *issued* before any of them finished: the region is
      // genuinely concurrent, not accidentally serial.
      issueOrders.add(kit.world.issued.slice(0, 4).join(','));
    }

    expect(transcripts.size).toBe(1);
    const only = [...transcripts][0] ?? '';
    const lines = only.split('\n');
    // Four notes in the orders' own order, four receipts in the orders' own
    // order, then the summary.
    expect(lines.slice(0, 4)).toEqual([
      'fetched u1 options (rev8)',
      'fetched u2 options (rev8)',
      'fetched u3 options (rev8)',
      'fetched u4 options (rev8)',
    ]);
    expect(lines[4]).toStartWith('u1 move 30,70 → applied');
    expect(lines[7]).toStartWith('u4 move 33,73 → applied');
    expect(lines[8]).toBe('4/4 applied rev8/t3');
    expect(lines).toHaveLength(9);

    // All four drains were issued up front on every run, whatever the
    // latencies were.
    expect(issueOrders.size).toBe(1);
    expect([...issueOrders][0]).toBe(ACTORS.map((actor) => `drain:${actor}`).join(','));
  }, 30_000);

  test('a refusal renders its inline options in the orders’ order, not the responses’', async () => {
    const transcripts = new Set<string>();
    for (let attempt = 0; attempt < 12; attempt += 1) {
      const kit = fresh();
      const before = rev(7);
      const after = rev(9);
      kit.world.revision = before;
      // Three seats, all refused, all with catalogs the refusal must re-read
      // because the rejection carried a newer revision.
      ACTORS.slice(0, 3).forEach((actor, index) => {
        kit.seed(actor, [actionFor(index)], before);
        kit.world.catalogs.set(actor, [actionFor(index)]);
      });
      kit.world.receipt = () => ({ state: 'rejected', revision: after });
      const random = rng(attempt * 104729 + 5);
      kit.world.latency = () => Math.floor(random() * 6);

      const orders = ACTORS.slice(0, 3)
        .map((_actor, index) => `u${index + 1} move ${30 + index},${70 + index}`)
        .join('; ');
      const run = await runDoCaptured(kit, doArgs(orders, { continueOnError: true }));

      expect(run.code).toBe(2);
      transcripts.add(run.out.join('\n'));
    }
    expect(transcripts.size).toBe(1);
    const lines = ([...transcripts][0] ?? '').split('\n');
    // Both the section order *and* the `aN` digits inside them are stable.
    // The digits are the part concurrency breaks silently: they are assigned
    // at commit, so without `drainActors`' ordered-commit gate they follow
    // whichever response landed first and `just legal` renumbers itself
    // between two identical runs.
    // `a1`/`a2` went to u2 and u3 when the first rejection re-enumerated the
    // not-yet-sent orders, and `a3` to u1 when the refusal re-read its menu —
    // the same sequence CPython's serial loop produces.
    expect(lines.slice(4)).toEqual([
      'u1 can (rev9/t3): 1 options',
      'a3  unit.order/move  Move  T(30,70)',
      'u2 can (rev9/t3): 1 options',
      'a1  unit.order/move  Move  T(31,71)',
      'u3 can (rev9/t3): 1 options',
      'a2  unit.order/move  Move  T(32,72)',
    ]);
  }, 30_000);

  test('the ordered-commit gate admits the drains in input order', async () => {
    const committed: string[] = [];
    const started: string[] = [];
    const delays: Readonly<Record<string, number>> = { a: 9, b: 1, c: 5 };
    await Effect.runPromise(
      drainActors(['a', 'b', 'c'], (actorId, gate) =>
        Effect.gen(function* () {
          started.push(actorId);
          // The fetch is outside the gate, so it overlaps…
          yield* Effect.sleep(`${delays[actorId] ?? 0} millis`);
          // …and the commit is inside it, so it does not.
          return yield* gate(
            Effect.sync(() => {
              committed.push(actorId);
              return rev(7);
            })
          );
        })
      )
    );
    expect(started).toEqual(['a', 'b', 'c']);
    expect(committed).toEqual(['a', 'b', 'c']);
  });

  test('the concurrent pre-fetch never sends an order before every drain is in', async () => {
    const kit = fresh();
    coldSeats(kit);
    kit.world.latency = (kind) => (kind === 'drain' ? 4 : 0);

    const run = await runDoCaptured(kit, doArgs(ORDERS, { continueOnError: true }));

    expect(run.code).toBe(0);
    const firstSubmit = kit.world.trace.findIndex((entry) => entry.startsWith('submit:'));
    const lastDrain = kit.world.trace.reduce(
      (best, entry, index) => (entry.startsWith('drain:') ? index : best),
      -1
    );
    expect(lastDrain).toBeLessThan(firstSubmit);
  });
});

/** `{aN: action_id}` — the alias digits `just legal` prints, in order. */
const byAlias = (state: { readonly action_aliases: JsonObject }): ReadonlyArray<string> => {
  const table = state.action_aliases['by_alias'];
  if (!isJsonObject(table)) return [];
  return Object.entries(table).map(([alias, bound]) => {
    const identifier = isJsonObject(bound) ? bound['action_id'] : bound;
    return `${alias}=${typeof identifier === 'string' ? identifier : ''}`;
  });
};

describe('do — a failed drain leaves the cache where CPython’s raise left it', () => {
  test('the actors after the failing one are neither drained nor numbered', async () => {
    const kit = fresh();
    coldSeats(kit);
    // Three cold seats and a transient error on the middle one.  CPython's
    // `for actor_id in targets:` raises on u2 having fetched only u1, so u3 is
    // never fetched, never ingested, never in `drained_actors`, and consumes
    // none of `a1..aN`.
    const three = ACTORS.slice(0, 3);
    kit.world.drainFailure = (actorId) =>
      actorId === three[1] ? playerError('the catalog could not be read') : null;
    const orders = three
      .map((_actor, index) => `u${index + 1} move ${30 + index},${70 + index}`)
      .join('; ');

    const first = await runDoCaptured(kit, doArgs(orders));

    expect(first.code).toBe(2);
    expect(first.error).toBe('the catalog could not be read');
    // Nothing was rendered and nothing was sent.
    expect(first.out).toEqual([]);
    expect(kit.world.trace.some((entry) => entry.startsWith('submit:'))).toBe(false);

    const after = kit.readState();
    expect(after.drained_actors).toEqual([three[0] ?? '']);
    // One action cached, holding `a1` — u3's would have taken `a2`.
    expect(byAlias(after)).toEqual([`a1=${actionFor(0).actionId}`]);

    // The agent retries the identical batch once the blip has passed.  Both
    // still-unread actors are fetched, and both notes print — the line a
    // committed sibling would silently have dropped.
    kit.world.drainFailure = () => null;
    const retry = await runDoCaptured(kit, doArgs(orders, { continueOnError: true }));

    expect(retry.code).toBe(0);
    expect(retry.out.slice(0, 2)).toEqual([
      'fetched u2 options (rev8)',
      'fetched u3 options (rev8)',
    ]);
    expect(retry.out[retry.out.length - 1]).toBe('3/3 applied rev8/t3');
  });

  test('a mid-batch re-enumeration that fails stops at the failing actor too', async () => {
    const kit = fresh();
    const before = rev(7);
    const after = rev(9);
    kit.world.revision = before;
    // Three seats whose catalogs are already read, so nothing is pre-fetched;
    // the first receipt bumps the revision and `_refresh_orders` re-drains the
    // two remaining actors — sequentially, raising on the first failure.
    ACTORS.slice(0, 3).forEach((actor, index) => {
      kit.seed(actor, [actionFor(index)], before);
      kit.world.catalogs.set(actor, [actionFor(index)]);
    });
    kit.world.receipt = () => ({ state: 'applied', revision: after });
    kit.world.drainFailure = (actorId) => (actorId === ACTORS[1] ? playerError('gone') : null);

    const orders = ACTORS.slice(0, 3)
      .map((_actor, index) => `u${index + 1} move ${30 + index},${70 + index}`)
      .join('; ');
    const numbered = byAlias(kit.readState());
    const run = await runDoCaptured(kit, doArgs(orders));

    expect(run.code).toBe(2);
    expect(run.out[1]).toBe('could not re-enumerate the remaining orders: gone');
    expect(run.out[2]).toBe('1/3 applied rev9/t3');
    expect(run.out[3]).toBe('stopped after order 1; 2 not sent');
    // u3's drain may have gone out, but nothing it read was kept: no actor is
    // drained at rev9 and not one alias was re-numbered, so the table is still
    // the retired rev7 one CPython's raise would have left behind.  A
    // committed u3 would have taken `a1` at rev9 and moved every later digit.
    expect(kit.readState().drained_actors).toEqual([]);
    expect(byAlias(kit.readState())).toEqual(numbered);
  });
});

describe('do — the streamed receipt ledger survives a kill', () => {
  test('a SIGKILL after order 2 of 4 leaves exactly two well-formed lines', async () => {
    const kit = fresh();
    coldSeats(kit);
    kit.world.killAfterReceipt = 2;

    const run = await runDoCaptured(kit, doArgs(ORDERS, { continueOnError: true }));

    // The process died mid-command: nothing was rendered, because CPython
    // renders once at the end and this port does too.
    expect(run.killed).toBe(true);
    expect(run.out).toEqual([]);

    const ledger = await Effect.runPromise(
      Effect.provide(readSessionLedger(kit.sessionPath), kit.layer)
    );
    expect(ledger).toHaveLength(2);
    expect(ledger[0]).toMatchObject({
      order: 'u1 move 30,70',
      batch_id: 'batch_00000001',
      receipt_state: 'applied',
      ok: true,
      state_revision: { turn: 3, revision: 8 },
    });
    expect(ledger[1]).toMatchObject({
      order: 'u2 move 31,71',
      batch_id: 'batch_00000002',
      receipt_state: 'applied',
      ok: true,
    });
  });

  test('every line is complete JSON, terminated, and 4 KiB or under', async () => {
    const kit = fresh();
    coldSeats(kit);
    kit.world.killAfterReceipt = 3;

    await runDoCaptured(kit, doArgs(ORDERS, { continueOnError: true }));

    const raw = await Effect.runPromise(
      Effect.provide(
        Effect.flatMap(
          Effect.succeed(kit.sessionPath),
          () => kit.scratch.files.readText(ledgerPath(kit), 'ledger')
        ),
        kit.layer
      )
    );
    expect(raw.endsWith('\n')).toBe(true);
    const lines = raw.split('\n').filter((line) => line !== '');
    expect(lines).toHaveLength(3);
    for (const line of lines) {
      expect(Buffer.byteLength(line, 'utf8') + 1).toBeLessThanOrEqual(4096);
      // Parses whole; a torn append would throw here.
      expect(() => JSON.parse(line)).not.toThrow();
    }
  });

  test('a ledger that cannot be written never fails the command', async () => {
    const kit = fresh();
    coldSeats(kit);
    // A file where the directory has to go: the append cannot succeed.
    const dir = kit.sessionPath.replace(/\.json$/, '');
    await Effect.runPromise(Effect.provide(kit.scratch.files.writeText(dir, 'not a dir'), kit.layer));

    const run = await runDoCaptured(kit, doArgs(ORDERS, { continueOnError: true }));

    expect(run.code).toBe(0);
    expect(run.out[run.out.length - 1]).toBe('4/4 applied rev8/t3');
    // And nothing about the ledger reached stderr.
    expect(run.err.join('\n')).not.toContain('receipts');
  });

  test('a ledger write that throws is swallowed as completely as one that fails', async () => {
    const kit = bench({ files: explodingAppend });
    benches.push(kit);

    const recorded = await Effect.runPromiseExit(
      Effect.provide(
        recordReceipt(
          kit.sessionPath,
          ledgerEntry('u1 move 30,70', 'action_x', {}, dispositionOf('batch_1', 'applied', rev(8)))
        ),
        Layer.succeed(PrivateFs, explodingAppend(kit.scratch.files))
      )
    );
    // Not merely "did not fail": `Effect.ignore` alone would have let the
    // defect through and this would be a `Die`.
    expect(recorded._tag).toBe('Success');
  });

  test('a throwing ledger still prints every applied batch_id and exits 0', async () => {
    const kit = bench({ files: explodingAppend });
    benches.push(kit);
    coldSeats(kit);

    const run = await runDoCaptured(kit, doArgs(ORDERS, { continueOnError: true }));

    // The failure mode this file exists to prevent, inverted: a defect out of
    // the ledger reaches `cli-main`'s `catchAllCause`, which prints
    // `error: <Cause.pretty>` on stderr with an *empty* stdout and status 2 —
    // losing four server-issued `batch_id`s from a command CPython would have
    // completed, which is precisely the "agent re-issues a real duplicate
    // action" scenario.
    expect(run.killed).toBe(false);
    expect(run.code).toBe(0);
    expect(run.out).toHaveLength(9);
    expect(run.out[4]).toStartWith('u1 move 30,70 → applied');
    expect(run.out[4]).toEndWith('batch_00000001');
    expect(run.out[run.out.length - 1]).toBe('4/4 applied rev8/t3');
    expect(run.err.join('\n')).not.toContain('ENOSPC');
    // And the throw really was in the path: nothing was appended.
    const ledger = await Effect.runPromise(
      Effect.provide(readSessionLedger(kit.sessionPath), Layer.succeed(PrivateFs, kit.scratch.files))
    );
    expect(ledger).toEqual([]);
  });

  test('a reader whose file layer throws answers with an empty ledger, not a crash', async () => {
    const kit = bench({ files: explodingAppend });
    benches.push(kit);
    const exploding: PrivateFsApi = {
      ...kit.scratch.files,
      readText: () =>
        Effect.sync(() => {
          throw new Error('EIO: i/o error');
        }),
    };

    const read = await Effect.runPromiseExit(
      Effect.provide(readSessionLedger(kit.sessionPath), Layer.succeed(PrivateFs, exploding))
    );

    // `readText` has the same bare `try/finally` as `appendText`, and
    // `readLedger`/`readSessionLedger` both declare `never` in their error
    // channel — a promise their callers are entitled to hold.
    expect(read._tag).toBe('Success');
    expect(read._tag === 'Success' ? read.value : null).toEqual([]);
  });
});

/** The absolute path of one bench's ledger. */
const ledgerPath = (kit: Bench): string =>
  `${kit.sessionPath.replace(/\.json$/, '')}/state/receipts.log`;
