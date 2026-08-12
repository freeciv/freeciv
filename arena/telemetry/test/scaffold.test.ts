/**
 * Phase-0 smoke: the barrel loads and both runtime dependencies — `effect` and
 * `evlog` — resolve and typecheck from inside the workspace.
 */
import { describe, expect, test } from 'bun:test';
import { Effect } from 'effect';
import * as Evlog from 'evlog';
import { TELEMETRY_PACKAGE } from 'src/index';

describe('@arena/telemetry scaffold', () => {
  test('the barrel names the package', () => {
    expect(TELEMETRY_PACKAGE).toBe('@arena/telemetry');
  });

  test('effect runs', () => {
    expect(Effect.runSync(Effect.succeed(TELEMETRY_PACKAGE))).toBe('@arena/telemetry');
  });

  test('evlog resolves as a module', () => {
    expect(typeof Evlog).toBe('object');
  });
});
