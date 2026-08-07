/**
 * `health --json`, printed the way CPython prints it — floats included.
 *
 * Ports the serialization half of `_print_v2_json(_validate_health(...))`
 * (client.py:3073-3074, 6552-6555).
 *
 * `_validate_health` hands `json.dumps` the numbers **as CPython parsed them**:
 * a wire `600.0` is a `float` and dumps as `600.0`, a wire `600` is an `int`
 * and dumps as `600`.  JavaScript has one number type, `src/services/http.ts`
 * has already run the body through `JSON.parse` by the time any unit sees it,
 * and `String(600)` is `"600"` — so core's `compactJson` prints
 * `{"timeout_s":600}` where CPython prints `{"timeout_s":600.0}`.
 *
 * That is not hypothetical.  Every field this module marks is a Python `float`
 * in the supervisor that produces it:
 *
 * | field | supervisor |
 * | --- | --- |
 * | `phase.timing.timeout_s` | `config["action_timeout_s"]`, from `V2_TIMING_MODE_TIMEOUTS = {"default": 600.0, …}` (supervisor.py:125) or `--action-timeout-s type=float` |
 * | `phase.timing.deadline_started_at` / `deadline_at` | `time.time()`, and that plus the timeout (9492-9508) |
 * | `phase.timing.elapsed_s` / `remaining_s` | `round(max(0.0, …), 3)` (9509-9513) |
 * | `phase.auto_end.grace_s` | `V2_AUTO_END_IDLE_GRACE_S = 20.0` (supervisor.py:288, 3643) |
 * | `phase.auto_end.remaining_s` | `max(0.0, grace_until - time.monotonic())` (3644-3647) |
 * | `phase.waiting_on.waiting_s` | `round(max(0.0, …), 3)` (9310-9313) |
 * | `phase.prior_end.elapsed_s` | the phase-event journal's own `elapsed_s` (9204) |
 * | `last_phase_end.deadline_started_at` / `ended_at` / `elapsed_s` | the same journal row |
 *
 * `600.0` and `20.0` are integral in every default game, so without this
 * module `play health --json` byte-differs from `python3 client.py health
 * --json` on essentially every live invocation.  Every *other* number the
 * envelope carries — `place`, `turn`, `phase`, `sequence`, `incarnation`,
 * `orders_submitted`, `max_turns`, `turns_remaining`, `sidecar.generation` and
 * the whole recovery block — is a Python `int`, checked with
 * `type(x) is not int` in `_validate_health` (client.py:1951-1966, 2226-2233),
 * and stays an int here.
 *
 * The number model and the encoder are U13's `canonical-body.ts`; this file
 * only says *which* fields are floats.  The honest caveat, recorded in
 * NOTES.md §10.5: that answer is reconstructed from the field, not from the
 * wire lexeme, because the body is parsed in core before a unit sees it.
 *
 * **The envelope is rarely printed alone.**  `wait --json` embeds it under
 * `"health"`, `turn --json` embeds `_turn_health_context`'s projection of it
 * under `"context"`, and `turn --end --await --brief --json` embeds both.  All
 * of those carry the same twelve floats, so the module exports the path set and
 * a re-rooting helper rather than one hard-wired entry point:
 * {@link HEALTH_FLOAT_PATHS}, {@link healthFloatPathsUnder},
 * {@link pyValueWithFloats}, {@link turnHealthContextPyValue}.
 */
import { Console, Effect } from 'effect';
import { pyDumps, pyFloat, type PyValue } from 'src/services/canonical-body';
import { isJsonObject, type HealthEnvelope } from 'src/schema/index';
import type { TurnHealthContext } from 'src/services/health-context';

/**
 * Dotted paths into the validated envelope whose numbers are Python floats.
 *
 * Relative to the envelope's own root.  {@link healthFloatPathsUnder} re-roots
 * them for a payload that carries the envelope — or a projection of it — some
 * levels down.
 */
export const HEALTH_FLOAT_PATHS: ReadonlySet<string> = new Set([
  'phase.timing.timeout_s',
  'phase.timing.deadline_started_at',
  'phase.timing.deadline_at',
  'phase.timing.elapsed_s',
  'phase.timing.remaining_s',
  'phase.auto_end.grace_s',
  'phase.auto_end.remaining_s',
  'phase.waiting_on.waiting_s',
  'phase.prior_end.elapsed_s',
  'last_phase_end.deadline_started_at',
  'last_phase_end.ended_at',
  'last_phase_end.elapsed_s',
]);

/**
 * The same twelve paths, re-rooted under each prefix a payload embeds them at.
 *
 * `health` is almost never printed alone.  `wait --json` prints it under
 * `"health"` (client.py:2353), `turn --json` prints
 * `_turn_health_context`'s projection of it under `"context"`
 * (client.py:7535 and the two `_turn_briefing_locked` payloads), and the
 * `--end --await --brief` composite prints *both*, at `wait.health` and
 * `turn.context` (client.py:6914-6919).  Every one of those is the same twelve
 * numbers; nothing else in a wait envelope or a turn result is a float.
 *
 * `''` means "the envelope is the payload".  So:
 *
 * ```ts
 * pyDumps(pyValueWithFloats(wait, healthFloatPathsUnder('health')), true)
 * pyDumps(pyValueWithFloats(result, healthFloatPathsUnder('context')), true)
 * pyDumps(pyValueWithFloats(composite,
 *   healthFloatPathsUnder('wait.health', 'turn.context')), true)
 * ```
 *
 * A projection re-roots cleanly because `_turn_health_context` copies
 * `health["phase"]` and `health["last_phase_end"]` **by reference and under
 * their own names** (client.py:6582-6595) — the two subtrees that hold all
 * twelve floats keep their keys, so only the prefix changes.
 */
export const healthFloatPathsUnder = (
  ...prefixes: ReadonlyArray<string>
): ReadonlySet<string> =>
  new Set(
    prefixes.flatMap((prefix) =>
      [...HEALTH_FLOAT_PATHS].map((path) => (prefix === '' ? path : `${prefix}.${path}`))
    )
  );

const annotate = (value: unknown, path: string, floatPaths: ReadonlySet<string>): PyValue => {
  if (value === null || value === undefined) return null;
  if (typeof value === 'boolean' || typeof value === 'string') return value;
  // An unmarked number is left plain: `pyDumps` prints an exact integer as an
  // int and anything fractional through `repr`, which is right either way.
  if (typeof value === 'number') return floatPaths.has(path) ? pyFloat(value) : value;
  if (Array.isArray(value)) {
    const items: ReadonlyArray<unknown> = value;
    return items.map((item) => annotate(item, `${path}[]`, floatPaths));
  }
  if (isJsonObject(value)) {
    const out: Record<string, PyValue> = {};
    for (const [key, item] of Object.entries(value)) {
      out[key] = annotate(item, path === '' ? key : `${path}.${key}`, floatPaths);
    }
    return out;
  }
  return null;
};

/**
 * Any payload, with the numbers at `floatPaths` marked as Python floats.
 *
 * This is the reusable half of the module: a unit that embeds a health
 * envelope, or `turnHealthContext`'s projection of one, hands its **whole**
 * payload here with {@link healthFloatPathsUnder} and serializes the result
 * with `pyDumps` instead of core's `printV2Json`.  Doing it any other way
 * prints `"timeout_s":600` where CPython prints `"timeout_s":600.0`.
 */
export const pyValueWithFloats = (value: unknown, floatPaths: ReadonlySet<string>): PyValue =>
  annotate(value, '', floatPaths);

/**
 * The validated envelope, with every wire float marked as one.
 *
 * Exported because `wait --json` (U05) embeds this same envelope under
 * `"health"`; splice it in with
 * `pyDumps({ ...wait, health: healthPyValue(wait.health) }, true)`, or hand the
 * whole envelope to {@link pyValueWithFloats}.
 */
export const healthPyValue = (health: HealthEnvelope): PyValue =>
  pyValueWithFloats(health, HEALTH_FLOAT_PATHS);

/**
 * `_turn_health_context`'s projection, with the same twelve floats marked.
 *
 * `turnHealthContext` (`src/services/health-context.ts`) returns a *typed*
 * object for the renderers, so its numbers are plain JavaScript numbers — and
 * `turn --json`, `turn --decisions --json` and the `--end --await --brief`
 * composite all print that object under `"context"`.  U12/U16 must serialize
 * it through this function (or `pyValueWithFloats` over the whole payload), or
 * `turn --json` byte-diverges from CPython on every live invocation: the
 * context carries `phase` and `last_phase_end` verbatim, so all twelve floats
 * are in there.
 */
export const turnHealthContextPyValue = (context: TurnHealthContext): PyValue =>
  pyValueWithFloats(context, HEALTH_FLOAT_PATHS);

/** `_print_v2_json(_validate_health(...))`, byte for byte. */
export const printHealthJson = (health: HealthEnvelope): Effect.Effect<void> =>
  Console.log(healthJsonText(health));

/** The one line `--json` prints, split out so tests can assert the bytes. */
export const healthJsonText = (health: HealthEnvelope): string =>
  pyDumps(healthPyValue(health), true);
