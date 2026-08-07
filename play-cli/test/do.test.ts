/**
 * `do` — the order loop.
 *
 * Ports the end-to-end `test_v2_do_*` cases from `play/tests/test_client.py`:
 * one wire batch per order with an internal re-enumeration between them
 * (`test_v2_do_sends_one_batch_per_order_and_rebinds_after_a_bump`), the
 * summary that cannot contradict its own receipts
 * (`…_summary_never_contradicts_the_receipt_above_it`), the applied `batch_id`
 * that survives a later failure (`…_never_discards_an_outcome_it_already_
 * printed`), the stop-on-first-rejection rule and its `--continue-on-error`
 * escape (`…_stops_on_the_first_rejection_unless_told_to_continue`), the
 * inline options a refusal prints (`test_v2_refusal_*`), the whole-batch
 * refusal when one order will not resolve (`…_refuses_every_order_when_one_
 * cannot_be_resolved`), the pre-fetch of every unread actor
 * (`…_fetches_every_unread_actor_before_it_sends_anything`) and the two
 * spellings of the same orders (`do "…"` positionally versus `--orders`).
 */
import { afterEach, describe, expect, test } from 'bun:test';
import { Command, ValidationError } from '@effect/cli';
import { BunContext } from '@effect/platform-bun';
import { Cause, Effect, Exit, Layer, Option } from 'effect';
import { playerError, V2ResponseError } from 'src/errors';
import { doCommandWith, liveDoHooks, type DoHooksFor } from 'src/commands/do.cmd';
import { openGate } from 'src/services/do-drain';
import { readSessionLedger } from 'src/services/receipt-ledger';
import { httpFor } from 'src/services/http';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore } from 'src/services/session-store';
import { V2Client, v2ClientFor } from 'src/services/v2-client';
import { errorPayload, fakeFetch } from 'test/_fixtures';
import {
  bench,
  doArgs,
  foundCity,
  move,
  phaseEndAction,
  rev,
  runDoCaptured,
  UNIT_ONE,
  UNIT_TWO,
  type Bench,
} from 'test/_do-harness';

const benches: Bench[] = [];

const fresh = (): Bench => {
  const made = bench();
  benches.push(made);
  return made;
};

afterEach(() => {
  while (benches.length > 0) benches.pop()?.scratch.cleanup();
});

const MOVE_ONE = `action_${'1'.repeat(26)}`;
const FOUND_ONE = `action_${'2'.repeat(26)}`;
const MOVE_FRESH = `action_${'3'.repeat(26)}`;
const FOUND_FRESH = `action_${'4'.repeat(26)}`;

describe('do — the batch', () => {
  test('sends one batch per order and rebinds after a revision bump', async () => {
    const kit = fresh();
    const first = rev(7);
    const later = rev(9);
    kit.world.revision = first;
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72), foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    // What the re-enumeration between the two orders would serve.
    kit.world.catalogs.set(UNIT_ONE, [
      move(MOVE_FRESH, UNIT_ONE, 32, 72),
      foundCity(FOUND_FRESH, UNIT_ONE, 31, 72),
    ]);
    kit.world.receipt = () => ({ state: 'applied', revision: later });

    const run = await runDoCaptured(kit, doArgs('u1 move 32,72; u1 found_city London'));

    expect(run.code).toBe(0);
    // POST, GET (one drain of exactly u1), POST.
    expect(kit.world.trace).toEqual([
      'submit:batch_00000001',
      'drain:' + UNIT_ONE,
      'submit:batch_00000002',
    ]);
    expect(run.out).toHaveLength(3);
    expect(run.out[0]).toStartWith('u1 move 32,72 → applied');
    expect(run.out[0]).toEndWith('batch_00000001');
    expect(run.out[1]).toStartWith('u1 found_city London → applied');
    expect(run.out[1]).toEndWith('batch_00000002');
    expect(run.out[2]).toBe('2/2 applied rev9/t3');

    // The second order went out against the *re-bound* handle, never the one
    // the first receipt expired.
    const state = kit.readState();
    expect(Object.keys(state.actions).sort()).toEqual([FOUND_FRESH, MOVE_FRESH].sort());
  });

  test('the summary never contradicts the receipt above it', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    kit.world.receipt = () => ({ state: 'applied', revision: rev(9) });

    const run = await runDoCaptured(kit, doArgs('u1 found_city London'));

    expect(run.code).toBe(0);
    expect(run.out).toHaveLength(2);
    expect(run.out[0]).toContain('→ applied rev9/t3');
    // The pre-batch revision was rev7; the summary carries what the receipt
    // proved, or the agent is taught its own action did not move the game.
    expect(run.out[1]).toBe('1/1 applied rev9/t3');
  });

  test('--json carries the receipt revision, not the pre-batch one', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    kit.world.receipt = () => ({ state: 'applied', revision: rev(9) });

    const run = await runDoCaptured(kit, doArgs('u1 found_city London', { json: true }));

    expect(run.code).toBe(0);
    expect(run.out).toHaveLength(1);
    const payload: unknown = JSON.parse(run.out[0] ?? '');
    expect(payload).toMatchObject({
      schema_version: 1,
      command: 'do',
      requested: 1,
      applied: 1,
      state_revision: { turn: 3, revision: 9 },
      stopped: null,
    });
    // The composite `--end` keys only appear when `--end` asked for them.
    expect(Object.keys(payload as Record<string, unknown>)).not.toContain('end');
  });

  test('an outcome it already printed is never discarded', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72), foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    kit.world.receipt = () => ({ state: 'applied', revision: rev(9) });
    // The internal re-enumeration is where the wheels come off.
    kit.world.drainFailure = () =>
      playerError('the v2 request could not be sent: connection reset');

    const run = await runDoCaptured(kit, doArgs('u1 move 32,72; u1 found_city London'));

    expect(run.code).toBe(2);
    // The applied order, its server-issued batch_id and the remedy that
    // resolves it are all still on stdout.
    expect(run.out[0]).toStartWith('u1 move 32,72 → applied rev9/t3');
    expect(run.out[0]).toEndWith('batch_00000001');
    expect(run.out.join('\n')).toContain('connection reset');
    expect(run.out).toContain('1/2 applied rev9/t3');
    expect(run.out.some((line) => line.includes('stopped after order 1'))).toBe(true);
  });

  test('a submit that fails names the batch the agent must resolve by hand', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);
    kit.world.submitFailure = () => playerError('the v2 request could not be sent: timeout');

    const run = await runDoCaptured(kit, doArgs('u1 move 32,72'));

    expect(run.code).toBe(2);
    expect(run.out[0]).toBe('u1 move 32,72 → not sent: the v2 request could not be sent: timeout');
    expect(run.out[1]).toBe('0/1 applied rev7/t3');
    expect(run.out[2]).toBe(
      'stopped after order 0; resolve batch_00000001 with `just receipt --batch_id batch_00000001`'
    );
  });

  test('a persist that fails names no batch, because none exists', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);
    kit.world.persistFailure = () => playerError('unknown or expired action ID');

    const run = await runDoCaptured(kit, doArgs('u1 move 32,72'));

    expect(run.code).toBe(2);
    expect(run.out[0]).toBe('u1 move 32,72 → not sent: unknown or expired action ID');
    expect(run.out[2]).toBe('stopped after order 0');
  });
});

describe('do — refusal', () => {
  test('stops on the first rejection and prints that actor’s options', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72), foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    kit.world.receipt = (index) => ({
      state: index === 0 ? 'rejected' : 'applied',
      revision: rev(7),
    });

    const run = await runDoCaptured(kit, doArgs('u1 move 32,72; u1 found_city London'));

    expect(run.code).toBe(2);
    // The refused actor's own options print under the refusal, and the still
    // valid cached catalog answers them: still exactly one request.
    expect(kit.world.trace).toEqual(['submit:batch_00000001']);
    expect(run.out).toHaveLength(6);
    expect(run.out[0]).toContain('rejected');
    expect(run.out[1]).toBe('0/2 applied rev7/t3');
    expect(run.out[2]).toContain('stopped after order 1');
    expect(run.out[2]).toContain('1 not sent');
    expect(run.out[2]).toContain('--continue-on-error');
    expect(run.out[3]).toBe('u1 can (rev7/t3): 2 options');
    expect(run.out[4]).toStartWith('a1  unit.order/move');
  });

  test('--continue-on-error keeps issuing the later orders', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72), foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    kit.world.receipt = (index) => ({
      state: index === 0 ? 'rejected' : 'applied',
      revision: rev(7),
    });
    kit.world.focus = () => 'next: just turn';

    const run = await runDoCaptured(
      kit,
      doArgs('u1 move 32,72; u1 found_city London', { continueOnError: true })
    );

    expect(run.code).toBe(2);
    expect(kit.world.trace.filter((entry) => entry.startsWith('submit:'))).toHaveLength(2);
    expect(run.out[0]).toContain('rejected');
    expect(run.out[1]).toContain('applied');
    expect(run.out[2]).toBe('1/2 applied rev7/t3');
    expect(run.out[3]).toBe('u1 can (rev7/t3): 2 options');
    // The focus tail is still the last word of the command.
    expect(run.out[run.out.length - 1]).toStartWith('next: ');
  });

  test('one options section per refused actor, capped at three', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);
    kit.seed(UNIT_TWO, [move(`action_${'5'.repeat(26)}`, UNIT_TWO, 32, 72)]);
    kit.world.receipt = () => ({ state: 'rejected', revision: rev(7) });

    const run = await runDoCaptured(
      kit,
      doArgs('u1 move 32,72; u2 move 32,72', { continueOnError: true })
    );

    expect(run.code).toBe(2);
    // Both refused actors are answered, and the still-valid cached catalogs
    // mean neither answer cost a round trip.
    expect(kit.world.trace.filter((entry) => entry.startsWith('drain:'))).toHaveLength(0);
    expect(run.out[3]).toBe('u1 can (rev7/t3): 1 options');
    expect(run.out[5]).toBe('u2 can (rev7/t3): 1 options');
  });

  test('a refusal that moved the game re-reads the menu it hands back, bounded', async () => {
    const kit = fresh();
    const moved = rev(9);
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72), foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    // Thirty-seven options at the newer revision: far more than the twelve a
    // refusal is allowed to print.
    kit.world.catalogs.set(
      UNIT_ONE,
      Array.from({ length: 37 }, (_unused, index) =>
        move(`action_${String(index).padStart(26, '0')}`, UNIT_ONE, 30 + index, 72)
      )
    );
    kit.world.receipt = () => ({ state: 'rejected', revision: moved });

    const run = await runDoCaptured(kit, doArgs('u1 move 32,72'));

    expect(run.code).toBe(2);
    // One drain of exactly the refused actor's catalog, and nothing else.
    expect(kit.world.trace).toEqual(['submit:batch_00000001', 'drain:' + UNIT_ONE]);
    expect(run.out[0]).toContain('rejected');
    expect(run.out[1]).toBe('0/1 applied rev9/t3');
    expect(run.out[2]).toContain('stopped after order 1');
    expect(run.out[3]).toBe(
      'u1 can (rev9/t3): 12 of 37 shown — all: just legal --actor_id u1 --all'
    );
    const rows = run.out.slice(4);
    expect(rows).toHaveLength(12);
    // The rows are the `just legal` rows, alias first, so the very next call
    // can name one without another command.
    expect(rows[0]).toStartWith('a1 ');
    expect(rows[0]).toContain('unit.order/move');
    expect(rows[11]).toStartWith('a12 ');
  });

  test('an options lookup that fails leaves the refusal exactly as it was', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);
    kit.world.receipt = () => ({ state: 'rejected', revision: rev(9) });
    kit.world.drainFailure = () => playerError('the catalog could not be read');

    const run = await runDoCaptured(kit, doArgs('u1 move 32,72'));

    expect(run.code).toBe(2);
    expect(run.out).toHaveLength(3);
    expect(run.out[1]).toBe('0/1 applied rev9/t3');
    expect(run.out[2]).toContain('stopped after order 1');
  });

  test('an options drain that *throws* leaves the refusal exactly as it was', async () => {
    // `_refused_actor_options` catches `OSError` as well as the two client
    // errors, and the port's `OSError` is the defect channel:
    // `PrivateFsApi.readText`/`appendText` run their `fstat`/`read` inside a
    // bare `try/finally`, so an `EIO` or `EACCES` during the ingest a drain
    // does *throws* rather than failing.  A defect walks past `orElseSucceed`,
    // out of `doLocked` and `runDo` before `render(lines)` runs, and reaches
    // `cli-main`'s `catchAllCause` as `error: …` on stderr with an empty
    // stdout — losing an applied, server-issued `batch_id` from a command
    // CPython completed.
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72), foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    kit.world.receipt = (index) => ({
      state: index === 0 ? 'applied' : 'rejected',
      // The rejection moved the game on, so the refusal has to re-drain.
      revision: index === 0 ? rev(7) : rev(9),
    });
    const throwing: DoHooksFor = (path, session, wait) =>
      Effect.map(kit.hooks(path, session, wait), (hooks) => ({
        ...hooks,
        drainLegalUnlocked: () =>
          Effect.sync((): null => {
            throw new Error('EIO: i/o error');
          }),
      }));

    const run = await runDoCaptured(
      { ...kit, hooks: throwing },
      doArgs('u1 move 32,72; u1 found_city London', { continueOnError: true })
    );

    expect(run.killed).toBe(false);
    expect(run.code).toBe(2);
    // Both receipts survived, including the applied one's batch_id.
    expect(run.out[0]).toContain('applied');
    expect(run.out[0]).toContain('batch_00000001');
    expect(run.out[1]).toContain('rejected');
    expect(run.out[2]).toBe('1/2 applied rev9/t3');
    // And the help that could not be built is simply absent.
    expect(run.out.join('\n')).not.toContain('u1 can (');
    expect(run.err.join('\n')).not.toContain('EIO');
  });

  test('a state re-read that *throws* leaves the refusal exactly as it was', async () => {
    // The other half of the same `except OSError`: `_actor_options_section`
    // re-loads `.v2-state` before it renders, and that read defects too.
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72), foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    kit.world.receipt = (index) => ({
      state: index === 0 ? 'applied' : 'rejected',
      revision: rev(7),
    });
    kit.world.focus = () => 'next: just turn';
    // `runDo` reads the state three times on this path: once to resolve the
    // orders, once inside the refusal's options section, and once for the
    // focus tail.  Only the middle one throws, which is what isolates the
    // options section as the thing that must survive it.
    let reads = 0;
    const store: typeof kit.store = {
      ...kit.store,
      // `Effect.suspend`, because `runDo` builds its `readState` effect once
      // and yields it three times: the counter has to move when the effect
      // *runs*, not when it is described.
      readState: (path, session) =>
        Effect.suspend(() => {
          reads += 1;
          return reads === 2
            ? Effect.sync((): never => {
                throw new Error('EACCES: permission denied');
              })
            : kit.store.readState(path, session);
        }),
    };
    const layer = Layer.merge(kit.layer, Layer.succeed(SessionStore, store));

    const run = await runDoCaptured(
      { ...kit, layer },
      doArgs('u1 move 32,72; u1 found_city London', { continueOnError: true })
    );

    expect(run.killed).toBe(false);
    expect(run.code).toBe(2);
    expect(run.out[0]).toContain('batch_00000001');
    expect(run.out[2]).toBe('1/2 applied rev7/t3');
    expect(run.out.join('\n')).not.toContain('u1 can (');
    // The focus tail — the read *after* the one that threw — still prints.
    expect(run.out[run.out.length - 1]).toBe('next: just turn');
  });

  test('--json prints no inline options and no focus line', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);
    kit.world.receipt = () => ({ state: 'rejected', revision: rev(7) });
    kit.world.focus = () => 'next: never printed';

    const run = await runDoCaptured(kit, doArgs('u1 move 32,72', { json: true }));

    expect(run.code).toBe(2);
    expect(run.out).toHaveLength(1);
    const payload: unknown = JSON.parse(run.out[0] ?? '');
    expect(payload).toMatchObject({ applied: 0, requested: 1 });
    expect(run.out.join('\n')).not.toContain('u1 can (');
    expect(run.out.join('\n')).not.toContain('next: never printed');
  });
});

describe('do — resolution', () => {
  test('refuses every order when one cannot be resolved, before any request', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);

    const run = await runDoCaptured(kit, doArgs('u1 move 32,72; u1 teleport 99,99'));

    expect(run.code).toBe(2);
    expect(kit.world.trace).toEqual([]);
    const lines = run.error.split('\n');
    expect(lines[0]).toContain('1 of 2 orders did not resolve');
    expect(lines[0]).toContain('rev7/t3');
    expect(lines[0]).toContain('nothing was sent');
    expect(lines[1]).toContain('1 resolved');
    expect(lines[1]).toContain('u1 move 32,72');
    expect(lines[2]).toContain('2 unresolved');
    expect(lines[2]).toContain('teleport');
    // Every unresolved order names the exact command to run, in the alias
    // dialect the agent already types.
    expect(run.error).toContain('enumerate with: just legal --actor_id u1 --all');
  });

  test('the order-count bounds are refused with no request either', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);

    const many = await runDoCaptured(
      kit,
      doArgs(new Array(9).fill('u1 move 32,72').join('; '))
    );
    expect(many.code).toBe(2);
    expect(many.error).toContain('1 through 8 orders');

    const none = await runDoCaptured(kit, doArgs('  ;  '));
    expect(none.code).toBe(2);
    expect(none.error).toContain('at least one order');

    expect(kit.world.trace).toEqual([]);
  });

  test('every unread actor is fetched before anything is sent', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    // Learn the entity aliases without learning any capability: seed both
    // actors' catalogs and then let the revision bump wipe them.
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);
    kit.seed(UNIT_TWO, [move(`action_${'5'.repeat(26)}`, UNIT_TWO, 32, 72)], rev(8));
    kit.world.revision = rev(8);
    kit.world.catalogs.set(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);
    kit.world.catalogs.set(UNIT_TWO, [move(`action_${'5'.repeat(26)}`, UNIT_TWO, 32, 72)]);

    // `u1 teleport` is a bad verb, not a cold cache.  u1 is still fetched
    // first — the whole batch is re-checked before anything is sent — and then
    // the batch refuses whole.
    const run = await runDoCaptured(kit, doArgs('u1 teleport 9,9; u2 move 32,72'));

    expect(run.code).toBe(2);
    expect(kit.world.trace.filter((entry) => entry.startsWith('submit:'))).toEqual([]);
    expect(run.error).toContain('fetched u1 options (rev8)');
    expect(run.error).toContain('1 of 2 orders did not resolve');
    expect(run.error).toContain('2 resolved');
    expect(run.error).toContain('enumerate with: just legal --actor_id u1 --all');
  });

  test('an actor already read is never re-fetched for a bad verb', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);

    const run = await runDoCaptured(kit, doArgs('u1 teleport 9,9'));

    expect(run.code).toBe(2);
    expect(kit.world.trace).toEqual([]);
    expect(run.error).toContain('did not resolve');
  });

  test('--no-refresh keeps the plain refusal and fetches nothing', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);
    kit.seed(UNIT_TWO, [move(`action_${'5'.repeat(26)}`, UNIT_TWO, 32, 72)], rev(8));
    kit.world.revision = rev(8);
    kit.world.catalogs.set(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);

    const run = await runDoCaptured(kit, doArgs('u1 move 32,72', { noRefresh: true }));

    expect(run.code).toBe(2);
    expect(kit.world.trace).toEqual([]);
    expect(run.error).toContain('did not resolve');
  });

  test('a stale-alias rebind note prints above the receipt', async () => {
    const kit = fresh();
    kit.world.revision = rev(9);
    kit.seed(UNIT_ONE, [foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    kit.world.aliasNotes = ['a1 rebound at rev9'];
    kit.world.receipt = () => ({ state: 'applied', revision: rev(9) });

    const run = await runDoCaptured(kit, doArgs('u1 found_city London'));

    expect(run.code).toBe(0);
    expect(run.out[0]).toBe('a1 rebound at rev9');
    expect(run.out[1]).toContain('→ applied rev9/t3');
  });
});

describe('do — the actorless order', () => {
  const END_ONE = `action_${'7'.repeat(26)}`;
  const END_FRESH = `action_${'8'.repeat(26)}`;

  test('a bump re-enumerates the global catalog for an order with no actor', async () => {
    const kit = fresh();
    const later = rev(9);
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);
    // `phase.end` carries no `subject.actor`, so `_order_actor` reads it as
    // `""` and it lives in the global catalog — the one
    // `_drain_legal_unlocked(actor_id="")` fetches.
    kit.seed('', [phaseEndAction(END_ONE)]);
    // What the global re-enumeration serves after the first receipt bumps the
    // revision and wipes every cached capability.
    kit.world.catalogs.set('', [phaseEndAction(END_FRESH)]);
    kit.world.receipt = () => ({ state: 'applied', revision: later });

    const run = await runDoCaptured(kit, doArgs('u1 move 32,72; end'));

    // `_refresh_orders` dedupes pending actors with a plain set and drains
    // whatever is in it, `""` included.  Skipping the empty id leaves
    // `rebindOrder` a wiped cache, and the order that CPython re-issues turns
    // into `rev9/t3 no longer offers end; …` with an exit 2 and nothing sent.
    expect(run.code).toBe(0);
    expect(kit.world.trace).toEqual([
      'submit:batch_00000001',
      'drain:',
      'submit:batch_00000002',
    ]);
    expect(run.out[1]).toStartWith('end → applied rev9/t3');
    expect(run.out[1]).toEndWith('batch_00000002');
    expect(run.out[2]).toBe('2/2 applied rev9/t3');
    // It went out against the re-bound handle, not the one the first receipt
    // expired.
    expect(Object.keys(kit.readState().actions)).toEqual([END_FRESH]);
  });

  test('the global catalog is drained once, however many actorless orders name it', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);
    kit.seed('', [phaseEndAction(END_ONE)]);
    kit.world.catalogs.set('', [phaseEndAction(END_FRESH)]);
    kit.world.receipt = (index) => ({ state: 'applied', revision: index === 0 ? rev(9) : rev(9) });

    const run = await runDoCaptured(kit, doArgs('u1 move 32,72; end; end'));

    expect(run.code).toBe(0);
    expect(kit.world.trace.filter((entry) => entry === 'drain:')).toHaveLength(1);
    expect(run.out[3]).toBe('3/3 applied rev9/t3');
  });
});

describe('do — the argparse surface', () => {
  const explodingHooks: DoHooksFor = () =>
    Effect.die(new Error('no hook may be built for a refused invocation'));

  /**
   * How far one argv got: refused by the parser, or handed to the handler.
   *
   * The distinction is the whole test.  `Options.text` would have *also* ended
   * in a non-zero status here — as a die out of `explodingHooks` — so a test
   * that only checked the exit code would pass against the bug.
   */
  type Reached = 'validation-error' | 'handler' | 'other';

  const parse = async (kit: Bench, argv: ReadonlyArray<string>): Promise<Reached> => {
    const log = console.log;
    const error = console.error;
    console.log = () => undefined;
    console.error = () => undefined;
    try {
      const exit = await Effect.runPromiseExit(
        Effect.provide(
          Command.run(doCommandWith(explodingHooks), { name: 'play', version: '0.1.0' })([
            'bun',
            'play',
            '--session',
            kit.sessionPath,
            ...argv,
          ]),
          Layer.mergeAll(kit.layer, BunContext.layer)
        )
      );
      if (Exit.isSuccess(exit)) return 'other';
      const failure = Option.getOrNull(Cause.failureOption(exit.cause));
      if (failure !== null && ValidationError.isValidationError(failure)) return 'validation-error';
      const died = Option.getOrNull(Cause.dieOption(exit.cause));
      return died instanceof Error && died.message.includes('no hook may be built')
        ? 'handler'
        : 'other';
    } finally {
      console.log = log;
      console.error = error;
    }
  };

  test('--until is a choice, so a bad value is refused before any order is sent', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);

    // argparse declared `choices=("phase", "revision")`, so CPython refused at
    // parse time with a usage dump and status 2, having sent nothing.  A plain
    // text option would have let the whole batch reach the wire and only
    // surfaced the bad value from the wait engine — and only if `--end
    // --await` was passed at all.
    expect(await parse(kit, ['u1 move 32,72', '--until', 'bogus'])).toBe('validation-error');
    expect(kit.world.trace).toEqual([]);
    // …and the control: a good value is not refused by the parser, it reaches
    // the handler.
    expect(await parse(kit, ['u1 move 32,72', '--until', 'revision'])).toBe('handler');
    expect(await parse(kit, ['u1 move 32,72'])).toBe('handler');

    // Both accepted spellings still parse, and reach the handler.
    for (const value of ['phase', 'revision']) {
      const kept = fresh();
      kept.world.revision = rev(7);
      kept.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);
      const run = await runDoCaptured(
        kept,
        doArgs('u1 move 32,72', { wait: { waitS: 120, pollS: 1, until: value } })
      );
      expect(run.code).toBe(0);
    }
  });
});

describe('do — the drain refusal payload', () => {
  test('a supervisor refusal during the pre-send drain keeps its wire payload', async () => {
    const kit = fresh();
    const payload = errorPayload();
    // One route, one refusal: the very first legal page the pre-send drain
    // asks for comes back 409.
    const client = v2ClientFor(httpFor(fakeFetch([{ status: 409, body: payload }])));
    const hooks = await Effect.runPromise(
      Effect.provide(
        liveDoHooks(kit.sessionPath, kit.session, { waitS: 120, pollS: 1, until: 'phase' }),
        Layer.mergeAll(
          kit.layer,
          Layer.succeed(V2Client, client),
          Layer.succeed(SessionStore, kit.store),
          Layer.succeed(PrivateFs, kit.scratch.files)
        )
      )
    );
    const failure = await Effect.runPromise(
      Effect.either(
        Effect.provide(
          hooks.drainLegalUnlocked(UNIT_ONE, openGate),
          Layer.mergeAll(
            kit.layer,
            Layer.succeed(V2Client, client),
            Layer.succeed(SessionStore, kit.store),
            Layer.succeed(PrivateFs, kit.scratch.files)
          )
        )
      )
    );

    // CPython called `_drain_legal_unlocked` from `_resolve_orders_fetching`
    // outside any try/except, so the refusal reached `main` and printed
    // `_render_error_payload(exc.payload)` on stdout — the byte-identical wire
    // payload under `--json`.  Collapsing it into a `PlayerError` here would
    // leave stdout empty and only `error: …` on stderr.
    expect(failure._tag).toBe('Left');
    const error = failure._tag === 'Left' ? failure.left : null;
    expect(error).toBeInstanceOf(V2ResponseError);
    expect(error instanceof V2ResponseError ? error.status : 0).toBe(409);
    expect(error instanceof V2ResponseError ? error.payload : null).toEqual(payload);
  });
});

describe('do — the two spellings', () => {
  test('positional orders equal --orders', async () => {
    const positional = fresh();
    positional.world.revision = rev(7);
    positional.seed(UNIT_ONE, [foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    positional.world.receipt = () => ({ state: 'applied', revision: rev(7) });

    const written = fresh();
    written.world.revision = rev(7);
    written.seed(UNIT_ONE, [foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    written.world.receipt = () => ({ state: 'applied', revision: rev(7) });

    const one = await runDoCaptured(
      positional,
      doArgs('', { positionalOrders: ['u1 found_city London'] })
    );
    const two = await runDoCaptured(written, doArgs('u1 found_city London'));

    expect(one.code).toBe(two.code);
    expect(one.out).toEqual(two.out);
  });

  test('orders given twice are refused by name', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);

    const run = await runDoCaptured(
      kit,
      doArgs('u1 found_city London', { positionalOrders: ['u1 found_city York'] })
    );

    expect(run.code).toBe(2);
    expect(run.error).toContain('orders were given twice');
    expect(kit.world.trace).toEqual([]);
  });
});

describe('do — the streamed receipt ledger', () => {
  test('every applied receipt is on disk before the command renders', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72), foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    kit.world.receipt = () => ({ state: 'applied', revision: rev(7) });

    const run = await runDoCaptured(
      kit,
      doArgs('u1 move 32,72; u1 found_city London', { continueOnError: true })
    );
    expect(run.code).toBe(0);

    const ledger = await Effect.runPromise(
      Effect.provide(readSessionLedger(kit.sessionPath), kit.layer)
    );
    expect(ledger).toHaveLength(2);
    expect(ledger[0]).toMatchObject({
      schema_version: 1,
      order: 'u1 move 32,72',
      batch_id: 'batch_00000001',
      action_id: MOVE_ONE,
      receipt_state: 'applied',
      ok: true,
    });
    expect(ledger[1]).toMatchObject({
      order: 'u1 found_city London',
      batch_id: 'batch_00000002',
      arguments: { city_name: 'London' },
      ok: true,
    });
  });

  test('a rejected receipt is recorded too, marked not ok', async () => {
    const kit = fresh();
    kit.world.revision = rev(7);
    kit.seed(UNIT_ONE, [move(MOVE_ONE, UNIT_ONE, 32, 72)]);
    kit.world.receipt = () => ({ state: 'rejected', revision: rev(7) });

    await runDoCaptured(kit, doArgs('u1 move 32,72'));

    const ledger = await Effect.runPromise(
      Effect.provide(readSessionLedger(kit.sessionPath), kit.layer)
    );
    expect(ledger).toHaveLength(1);
    expect(ledger[0]).toMatchObject({ receipt_state: 'rejected', ok: false });
  });
});
