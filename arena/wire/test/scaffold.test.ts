/**
 * Phase-0 smoke: the barrel loads, `effect` resolves, and `bun test` runs
 * against the same tsconfig the typechecker sees.
 */
import { describe, expect, test } from 'bun:test';
import { Effect } from 'effect';
import { WIRE_PACKAGE, WIRE_REVISION } from 'src/index';

describe('@arena/wire scaffold', () => {
  test('the barrel names the package and pins a wire revision', () => {
    expect(WIRE_PACKAGE).toBe('@arena/wire');
    expect(WIRE_REVISION).toBe(0);
  });

  test('effect runs', () => {
    expect(Effect.runSync(Effect.succeed(WIRE_PACKAGE))).toBe('@arena/wire');
  });
});
