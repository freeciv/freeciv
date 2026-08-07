/**
 * The four `--json` surfaces that embed a health envelope print CPython floats.
 *
 * NOTES §2 / §10.5 / §10.6 have the full account.  The short version: the
 * supervisor sends integral Python floats on **every** health response
 * (`timeout_s: 600.0`, `grace_s: 20.0`, `time.time()` deadlines, `round(...)`
 * counters), `src/services/http.ts` runs `JSON.parse` on the body so the
 * lexeme is gone by the time any unit sees it, and core's `printV2Json` then
 * prints `600` where CPython printed `600.0`.
 *
 * U06 fixed `health --json` and exported the projection —
 * `pyValueWithFloats(payload, healthFloatPathsUnder(prefix…))` + `pyDumps` —
 * but could not land the call sites, because `wait --json` (U05), `turn --json`
 * and `turn --end … --json` (U12) and `do --end … --json` (U16) are those
 * units' files.  The integrator landed them; this file is what stops a fifth
 * surface, or a revert, from silently diverging again.
 *
 * `test/health.test.ts` already pins the *serializer* against goldens generated
 * by CPython's own `json.dumps`.  What was missing, and what this file adds, is
 * that the commands actually route through it and root the path list at the
 * right prefix.
 *
 * Owned by the integrator (PORT_MAP §0 core row).
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import type { JsonValue } from 'src/schema/primitives';
import { emitTurn } from 'src/commands/turn.cmd';
import { turnEndJson } from 'src/services/turn-end';
import type { RenderTurnDeps, TurnResult } from 'src/render/turn';
import { pyDumps } from 'src/services/canonical-body';
import { healthFloatPathsUnder, pyValueWithFloats } from 'src/services/health-json';
import { compactJson } from 'src/services/json-output';
import {
  bench,
  dispositionOf,
  doArgs,
  foundCity,
  rev,
  runDoCaptured,
  UNIT_ONE,
  type Bench,
} from 'test/_do-harness';

// ---------------------------------------------------------------------------
// Payload shapes carrying the supervisor's integral floats
// ---------------------------------------------------------------------------

/**
 * The subtree `_turn_health_context` and `_validate_health` both carry.
 *
 * `timeout_s` is `V2_TIMING_MODE_TIMEOUTS["default"] = 600.0`
 * (agent_eval/supervisor.py:125) and `grace_s` is
 * `V2_AUTO_END_IDLE_GRACE_S = 20.0` (supervisor.py:288); both are published
 * verbatim, so both reach the wire as integral Python floats on every call.
 */
const phaseBlock = {
  turn: 104,
  phase: 0,
  active: true,
  timing: { mode: 'default', timeout_s: 600, deadline_started_at: 1000, deadline_at: 1600 },
  auto_end: { grace_s: 20, remaining_s: 12 },
};

const healthBlock = {
  schema_version: 2,
  game_state: 'running',
  phase: phaseBlock,
  last_phase_end: { turn: 103, phase: 0, source: 'timeout', elapsed_s: 600 },
};

const contextBlock = {
  game_state: 'running',
  phase: phaseBlock,
  last_phase_end: { turn: 103, phase: 0, source: 'timeout', elapsed_s: 600 },
};

/** Every integral float the twelve marked paths reach in these fixtures. */
const FLOAT_TOKENS = [
  '"timeout_s":600.0',
  '"deadline_started_at":1000.0',
  '"deadline_at":1600.0',
  '"grace_s":20.0',
  '"remaining_s":12.0',
  '"elapsed_s":600.0',
] as const;

const expectFloats = (text: string): void => {
  for (const token of FLOAT_TOKENS) expect(text).toContain(token);
  // The integer spellings are what core's encoder produced; none may survive.
  for (const token of FLOAT_TOKENS) {
    expect(text).not.toContain(token.replace('.0', ','));
    expect(text).not.toContain(token.replace('.0', '}'));
  }
};

const captureLog = async (body: () => Promise<unknown>): Promise<ReadonlyArray<string>> => {
  const lines: Array<string> = [];
  const original = console.log;
  console.log = (...parts: ReadonlyArray<unknown>): void => {
    lines.push(parts.join(' '));
  };
  try {
    await body();
  } finally {
    console.log = original;
  }
  return lines;
};

// ---------------------------------------------------------------------------
// turn --json
// ---------------------------------------------------------------------------

/** `--json` never renders, so the deps are never called; refusing proves it. */
const unusedDeps: RenderTurnDeps = {
  routeSummary: () => Effect.die('renderer reached under --json'),
  economyText: () => Effect.die('renderer reached under --json'),
  researchText: () => Effect.die('renderer reached under --json'),
  scoreText: () => {
    throw new Error('renderer reached under --json');
  },
  renderSectionItems: () => Effect.die('renderer reached under --json'),
  renderTiles: () => Effect.die('renderer reached under --json'),
};

const turnResult = (): TurnResult =>
  ({
    schema_version: 1,
    command: 'turn',
    status: 'not_ready',
    context: contextBlock as unknown as TurnResult['context'],
    next_commands: ['just wait --for-turn'],
  }) as TurnResult;

describe('turn --json', () => {
  test('the context prints the supervisor floats, not their integer spellings', async () => {
    const lines = await captureLog(() =>
      Effect.runPromise(
        emitTurn(turnResult(), unusedDeps, { json: true }) as Effect.Effect<void, PlayerError>
      ).catch((cause: unknown) => {
        throw cause;
      })
    );
    expect(lines).toHaveLength(1);
    expectFloats(lines[0] ?? '');
    // Still one valid JSON document, and still sorted-keys/compact-separator.
    expect(() => JSON.parse(lines[0] ?? '') as unknown).not.toThrow();
    expect(lines[0]).toContain('"command":"turn"');
  });

  test('the projection is rooted at `context`, which is where the briefing puts it', () => {
    const paths = healthFloatPathsUnder('context');
    expect(paths.has('context.phase.timing.timeout_s')).toBe(true);
    expect(paths.has('phase.timing.timeout_s')).toBe(false);
    // A wrong prefix is a silent no-op, so assert the payload key by name.
    expect(Object.keys(turnResult())).toContain('context');
  });
});

// ---------------------------------------------------------------------------
// turn --end --json  and  turn --end --await --brief --json
// ---------------------------------------------------------------------------

describe('turn --end --json', () => {
  const waitEnvelope = { wake_reason: 'phase_started', health: healthBlock };
  const disposition = dispositionOf('batch_end', 'applied', rev(9));

  test('the plain end embeds the wake, so `wait.health` is the prefix', () => {
    const payload = turnEndJson(false, disposition, waitEnvelope as never, null, '');
    const text = pyDumps(
      pyValueWithFloats(payload, healthFloatPathsUnder('wait.health', 'turn.context')),
      true
    );
    expectFloats(text);
  });

  test('the --brief composite embeds both the wake and the next briefing', () => {
    const payload = turnEndJson(
      true,
      disposition,
      waitEnvelope as never,
      turnResult(),
      ''
    );
    const text = pyDumps(
      pyValueWithFloats(payload, healthFloatPathsUnder('wait.health', 'turn.context')),
      true
    );
    expectFloats(text);
    expect(text).toContain('"status":"briefed"');
    // Both subtrees, not just the first one found.
    expect(text.split('"timeout_s":600.0')).toHaveLength(3);
  });
});

// ---------------------------------------------------------------------------
// wait --json
// ---------------------------------------------------------------------------

describe('wait --json', () => {
  test('the envelope nests health under `health`, which is the prefix', () => {
    const envelope = {
      schema_version: 2,
      wake_reason: 'phase_started',
      health: healthBlock,
      state_revision: null,
    };
    const text = pyDumps(pyValueWithFloats(envelope, healthFloatPathsUnder('health')), true);
    expectFloats(text);
  });

  test('core’s encoder is what these assertions are catching', () => {
    // The self-proof: every token above is *absent* from `compactJson`, so a
    // call site that reverts to `printV2Json` fails the tests in this file
    // rather than passing them vacuously.
    const text = compactJson({ health: healthBlock });
    for (const token of FLOAT_TOKENS) expect(text).not.toContain(token);
    expect(text).toContain('"timeout_s":600}');
    expect(text).toContain('"grace_s":20,');
  });
});

// ---------------------------------------------------------------------------
// do --end --await --brief --json
// ---------------------------------------------------------------------------

describe('do --end --await --brief --json', () => {
  const benches: Bench[] = [];
  afterEach(() => {
    while (benches.length > 0) benches.pop()?.scratch.cleanup();
  });

  const FOUND_ONE = `action_${'2'.repeat(26)}`;
  const END_BATCH = 'batch_phase_end_1';

  /** One seat with a one-action catalog, staged at rev 7. */
  const seated = (): Bench => {
    const kit = bench();
    benches.push(kit);
    const revision = rev(7);
    kit.world.revision = revision;
    kit.seed(UNIT_ONE, [foundCity(FOUND_ONE, UNIT_ONE, 31, 72)]);
    kit.world.receipt = () => ({ state: 'applied', revision });
    kit.world.phaseEnd = () =>
      Effect.succeed({
        disposition: dispositionOf(END_BATCH, 'applied', revision),
        warning: '',
        exitCode: 0,
        lines: [`phase end → applied rev${revision.revision}/t${revision.turn}  ${END_BATCH}`],
      });
    return kit;
  };

  test('the composite carries the wake and the briefing with CPython floats', async () => {
    const kit = seated();
    kit.world.awaitBrief = () =>
      Effect.succeed({
        wait: { wake_reason: 'phase_started', health: healthBlock },
        briefing: turnResult() as unknown as JsonValue,
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

    expect(run.out).toHaveLength(1);
    expectFloats(run.out[0] ?? '');
    expect(run.out[0]).toContain('"command":"do"');
  });

  test('without --end the payload carries neither key, so the projection is inert', async () => {
    const kit = seated();
    const run = await runDoCaptured(kit, doArgs('u1 found_city London', { json: true }));
    expect(run.out).toHaveLength(1);
    const payload: unknown = JSON.parse(run.out[0] ?? '');
    expect(Object.keys(payload as Record<string, unknown>)).not.toContain('wait');
    expect(Object.keys(payload as Record<string, unknown>)).not.toContain('turn');
  });
});

// ---------------------------------------------------------------------------
// The guard
// ---------------------------------------------------------------------------

/**
 * The behavioural tests above can only see the call sites that exist today.
 * NOTES §10.6's failure mode was *inaction*: `healthPyValue` shipped with zero
 * importers and the whole suite stayed green, because nothing asserted those
 * bytes.  So the invariant is also asserted structurally: a file that prints a
 * payload able to carry a health block must not reach for core's encoder.
 *
 * `test/docs-surfaces.test.ts` sets the precedent for a test that reads repo
 * files; this one reads four, by name.  If the permanent repair in NOTES §10.5
 * lands — `http.ts` decoding with `parsePython`, so float-ness survives from
 * the wire — this guard and every path list can be deleted together.
 */
describe('no health-carrying --json surface reaches for printV2Json', () => {
  const SOURCES = [
    'src/commands/wait.cmd.ts',
    'src/commands/turn.cmd.ts',
    'src/services/turn-end.ts',
    'src/commands/do.cmd.ts',
  ] as const;

  const root = path.resolve(import.meta.dir, '..');

  for (const relative of SOURCES) {
    test(`${relative} serializes through pyValueWithFloats`, () => {
      const source = fs.readFileSync(path.join(root, relative), 'utf8');
      expect(source).toContain('pyValueWithFloats');
      expect(source).toContain('healthFloatPathsUnder');
    });
  }

  test('turn.cmd keeps printV2Json only for the decisions payload, which has no health', () => {
    const source = fs.readFileSync(path.join(root, 'src/commands/turn.cmd.ts'), 'utf8');
    // One import and exactly one remaining call: `turn --decisions --json`,
    // whose payload is {schema_version, command, status, state_revision,
    // decisions} — no phase, no last_phase_end, no float to lose.
    const calls = source.split('printV2Json(').length - 1;
    expect(calls).toBe(1);
  });
});
