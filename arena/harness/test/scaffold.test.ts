/**
 * Phase-0 smoke: the harness resolves its workspace siblings by package name.
 *
 * This is the assertion that would actually break if the workspace links were
 * wrong — the rest of the scaffold would still pass.
 */
import { describe, expect, test } from 'bun:test';
import { Command } from '@effect/cli';
import { HARNESS_PACKAGE, stack } from 'src/index';

describe('@arena/harness scaffold', () => {
  test('the stack lists both siblings and itself', () => {
    expect(stack().map((entry) => entry.package)).toEqual([
      '@arena/wire',
      '@arena/telemetry',
      '@arena/harness',
    ]);
  });

  test('every entry carries the wire revision', () => {
    expect(stack().every((entry) => entry.wireRevision === 0)).toBe(true);
  });

  test('@effect/cli resolves', () => {
    expect(typeof Command.make(HARNESS_PACKAGE)).toBe('object');
  });
});
