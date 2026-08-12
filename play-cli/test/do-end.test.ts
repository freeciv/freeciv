/**
 * `do --end`, `--await` and `--brief` — the composed one-call turn.
 *
 * Ports `test_v2_do_end_never_ends_a_phase_whose_batch_stopped`,
 * `test_v2_do_end_prints_the_receipts_then_the_end_failure`,
 * `test_v2_brief_without_a_wake_is_refused_with_the_form_that_works`,
 * `test_v2_await_failure_never_hides_an_applied_phase_end` and the
 * `_composite_json` half of `test_v2_do_*`.
 *
 * The invariant every case here defends: **an applied phase end is never
 * swallowed by what happened after it.**  A validation error in the wait once
 * hid three consecutive applied phase-ends from a live agent, which then
 * re-ended phases it had already ended.
 */
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect } from 'effect';
import { playerError } from 'src/errors';
import {
  bench,
  dispositionOf,
  doArgs,
  foundCity,
  move,
  rev,
  runDoCaptured,
  UNIT_ONE,
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
const END_BATCH = 'batch_phase_end_1';

/** One seat with a two-action catalog, staged at `revision`. */
const seat = (kit: Bench, revision = rev(7)): void => {
  kit.world.revision = revision;
  kit.seed(UNIT_ONE, [
    move(MOVE_ONE, UNIT_ONE, 32, 72),
    foundCity(FOUND_ONE, UNIT_ONE, 31, 72),
  ]);
  kit.world.receipt = () => ({ state: 'applied', revision });
};

/** `_phase_end_locked` succeeding with `state`. */
const endsWith = (kit: Bench, state: 'applied' | 'rejected', revision = rev(7)): void => {
  kit.world.phaseEnd = () =>
    Effect.succeed({
      disposition: dispositionOf(END_BATCH, state, revision),
      warning: '',
      exitCode: state === 'applied' ? 0 : 2,
      lines: [`phase end → ${state} rev${revision.revision}/t${revision.turn}  ${END_BATCH}`],
    });
};

describe('the flag matrix', () => {
  test('--await without --end is refused, and sends nothing', async () => {
    const kit = fresh();
    seat(kit);
    const run = await runDoCaptured(kit, doArgs('u1 move 32,72', { awaitPhase: true }));
    expect(run.code).toBe(2);
    expect(run.error).toContain('just do --await');
    expect(kit.world.trace).toEqual([]);
  });

  test('--brief without a wake names the form that works', async () => {
    const kit = fresh();
    seat(kit);
    for (const overrides of [
      { endPhase: true, brief: true },
      { brief: true },
    ]) {
      const run = await runDoCaptured(kit, doArgs('u1 move 32,72', overrides));
      expect(run.code).toBe(2);
      expect(run.error).toContain('--end --await');
    }
    expect(kit.world.trace).toEqual([]);
  });
});

describe('--end', () => {
  test('never ends a phase whose batch stopped', async () => {
    const kit = fresh();
    seat(kit);
    kit.world.receipt = () => ({ state: 'rejected', revision: rev(7) });
    kit.world.phaseEnd = () => Effect.die(new Error('a stopped batch ended its phase'));
    kit.world.awaitBrief = () => Effect.die(new Error('a stopped batch awaited'));

    const run = await runDoCaptured(
      kit,
      doArgs('u1 move 32,72; u1 found_city London', {
        endPhase: true,
        awaitPhase: true,
        brief: true,
      })
    );

    expect(run.code).toBe(2);
    const printed = run.out.join('\n');
    // The first order was refused, so the batch stopped, nothing enumerated
    // phase.end, and the turn is still the agent's.
    expect(printed).toContain('phase NOT ended');
    expect(printed).toContain('0/2 orders applied');
    expect(printed).toContain('0/2 applied');
    expect(printed).toContain('just turn --end --await --brief');
  });

  test('prints the receipts, then the end failure, then the tail', async () => {
    const kit = fresh();
    seat(kit);
    kit.world.phaseEnd = () =>
      Effect.fail(
        playerError(
          'no phase.end action is enumerable for this seat right now; run just turn --end'
        )
      );
    kit.world.awaitBrief = () => Effect.die(new Error('awaited an unended phase'));
    kit.world.focus = () => 'next 2 actors: just do "u2 road T(31,72); c1 build Warriors"';

    const run = await runDoCaptured(
      kit,
      doArgs('u1 found_city London', { endPhase: true, awaitPhase: true, brief: true })
    );

    expect(run.code).toBe(2);
    expect(run.out[0]).toStartWith('u1 found_city London →');
    expect(run.out[0]).toContain('applied');
    expect(run.out[1]).toBe('1/1 applied rev7/t3');
    expect(run.out[2]).toStartWith('phase NOT ended: ');
    expect(run.out[2]).toContain('just turn');
    // The turn is still the agent's, so the tail still names it.
    expect(run.out[3]).toBe('next 2 actors: just do "u2 road T(31,72); c1 build Warriors"');
  });

  test('an end that was not accepted says so, and says it was not awaited', async () => {
    const kit = fresh();
    seat(kit);
    endsWith(kit, 'rejected');
    kit.world.awaitBrief = () => Effect.die(new Error('awaited an unaccepted end'));

    const run = await runDoCaptured(
      kit,
      doArgs('u1 found_city London', { endPhase: true, awaitPhase: true })
    );

    expect(run.code).toBe(2);
    expect(run.out).toContain('phase NOT ended: the phase end above was not accepted; not awaited');
  });

  test('an applied end without --await teaches the one-call ending', async () => {
    const kit = fresh();
    seat(kit);
    endsWith(kit, 'applied');

    const run = await runDoCaptured(kit, doArgs('u1 found_city London', { endPhase: true }));

    expect(run.code).toBe(0);
    expect(run.out[0]).toStartWith('u1 found_city London →');
    expect(run.out[1]).toBe('1/1 applied rev7/t3');
    expect(run.out[2]).toBe(`phase end → applied rev7/t3  ${END_BATCH}`);
    expect(run.out[3]).toBe(
      'next: just wait — or add --await --brief to spend one call on the whole turn'
    );
    // The phase ended, so no focus line: this turn is over.
    expect(run.out).toHaveLength(4);
  });
});

describe('--end --await', () => {
  test('the wake lines follow the receipts', async () => {
    const kit = fresh();
    seat(kit);
    endsWith(kit, 'applied');
    kit.world.awaitBrief = () =>
      Effect.succeed({
        wait: { wake_reason: 'phase_started' },
        briefing: null,
        briefError: '',
        lines: ['awake after 4s — turn 4 phase 0 is yours', 'next: just turn'],
      });

    const run = await runDoCaptured(
      kit,
      doArgs('u1 found_city London', { endPhase: true, awaitPhase: true })
    );

    expect(run.code).toBe(0);
    expect(run.out).toEqual([
      run.out[0] ?? '',
      '1/1 applied rev7/t3',
      `phase end → applied rev7/t3  ${END_BATCH}`,
      'awake after 4s — turn 4 phase 0 is yours',
      'next: just turn',
    ]);
  });

  test('the prelude flushes ahead of the first progress line and is not reprinted', async () => {
    const kit = fresh();
    seat(kit);
    endsWith(kit, 'applied');
    // The real wait prints its own tick lines; flushing the prelude is what
    // keeps the phase end from appearing after ten minutes of waiting.
    kit.world.awaitBrief = (options) =>
      Effect.gen(function* () {
        const prelude = options.prelude;
        expect(prelude).not.toBeNull();
        if (prelude !== null) {
          for (const line of prelude.slice()) yield* Effect.sync(() => console.log(line));
          prelude.length = 0;
        }
        yield* Effect.sync(() => console.log('waiting… 15s'));
        return {
          wait: { wake_reason: 'phase_started' },
          briefing: null,
          briefError: '',
          lines: ['awake after 20s — turn 4 phase 0 is yours'],
        };
      });

    const run = await runDoCaptured(
      kit,
      doArgs('u1 found_city London', { endPhase: true, awaitPhase: true })
    );

    expect(run.code).toBe(0);
    // Receipt, summary, phase end — then the tick, then the wake.  Each line
    // exactly once.
    expect(run.out).toHaveLength(5);
    expect(run.out[1]).toBe('1/1 applied rev7/t3');
    expect(run.out[2]).toBe(`phase end → applied rev7/t3  ${END_BATCH}`);
    expect(run.out[3]).toBe('waiting… 15s');
    expect(run.out[4]).toBe('awake after 20s — turn 4 phase 0 is yours');
  });

  test('an await failure never hides an applied phase end', async () => {
    const kit = fresh();
    seat(kit);
    endsWith(kit, 'applied');
    kit.world.awaitBrief = () =>
      Effect.fail(playerError('invalid v2 health: unexpected future_field'));

    const run = await runDoCaptured(
      kit,
      doArgs('u1 found_city London', { endPhase: true, awaitPhase: true, brief: true })
    );

    // The end applied, so the receipt is authoritative and the exit status is
    // the refusal the *brief* earned — never a lost receipt.
    expect(run.code).toBe(2);
    const printed = run.out.join('\n');
    expect(printed).toContain(`phase end → applied rev7/t3  ${END_BATCH}`);
    expect(printed).toContain('phase ended: the receipts above are authoritative');
    expect(printed).toContain('await failed: invalid v2 health: unexpected future_field');
    expect(printed).toContain('do not re-run `turn --end` for this phase');
  });

  test('a briefing that failed is an exit-2 note, not a lost wake', async () => {
    const kit = fresh();
    seat(kit);
    endsWith(kit, 'applied');
    kit.world.awaitBrief = () =>
      Effect.succeed({
        wait: { wake_reason: 'phase_started' },
        briefing: null,
        briefError: 'the state page could not be read',
        lines: ['awake after 4s', 'briefing failed: the state page could not be read', 'next: just turn'],
      });

    const run = await runDoCaptured(
      kit,
      doArgs('u1 found_city London', { endPhase: true, awaitPhase: true, brief: true })
    );

    expect(run.code).toBe(2);
    expect(run.out.join('\n')).toContain('briefing failed');
  });
});

describe('--end --json', () => {
  test('the composite carries end, wait, turn and turn_error', async () => {
    const kit = fresh();
    seat(kit);
    endsWith(kit, 'applied');
    kit.world.awaitBrief = () =>
      Effect.succeed({
        wait: { wake_reason: 'phase_started' },
        briefing: { turn: 4 },
        briefError: '',
        lines: ['awake'],
      });

    const run = await runDoCaptured(
      kit,
      doArgs('u1 found_city London', {
        endPhase: true,
        awaitPhase: true,
        brief: true,
        json: true,
      })
    );

    expect(run.code).toBe(0);
    expect(run.out).toHaveLength(1);
    const payload: unknown = JSON.parse(run.out[0] ?? '');
    expect(payload).toMatchObject({
      command: 'do',
      requested: 1,
      applied: 1,
      stopped: null,
      wait: { wake_reason: 'phase_started' },
      turn: { turn: 4 },
      turn_error: null,
    });
    const end = (payload as { readonly end: { readonly batch_id: string } }).end;
    expect(end.batch_id).toBe(END_BATCH);
  });

  test('--json keeps the whole rendering for one payload, so no prelude is passed', async () => {
    const kit = fresh();
    seat(kit);
    endsWith(kit, 'applied');
    kit.world.awaitBrief = (options) => {
      expect(options.prelude).toBeNull();
      return Effect.succeed({
        wait: { wake_reason: 'phase_started' },
        briefing: null,
        briefError: '',
        lines: ['awake'],
      });
    };

    const run = await runDoCaptured(
      kit,
      doArgs('u1 found_city London', { endPhase: true, awaitPhase: true, json: true })
    );
    expect(run.code).toBe(0);
    expect(run.out).toHaveLength(1);
  });

  test('--json re-raises an await failure instead of turning it into lines', async () => {
    const kit = fresh();
    seat(kit);
    endsWith(kit, 'applied');
    kit.world.awaitBrief = () => Effect.fail(playerError('invalid v2 health'));

    const run = await runDoCaptured(
      kit,
      doArgs('u1 found_city London', { endPhase: true, awaitPhase: true, json: true })
    );

    expect(run.code).toBe(2);
    expect(run.error).toBe('invalid v2 health');
  });
});
