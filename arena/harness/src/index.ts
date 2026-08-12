/**
 * Barrel for `@arena/harness`.
 *
 * Phase 0 of the port: the package exists and — the part actually worth
 * pinning — it resolves its two workspace siblings by package name, not by
 * relative path.  The match driver and the `arena` CLI land here.
 */
import { TELEMETRY_PACKAGE } from '@arena/telemetry';
import { WIRE_PACKAGE, WIRE_REVISION } from '@arena/wire';

/** Identity of this package. */
export const HARNESS_PACKAGE = '@arena/harness' as const;

/** One arena package and the revision of the wire format it was built against. */
export interface StackEntry {
  readonly package: string;
  readonly wireRevision: number;
}

/**
 * The packages this harness is built from, in dependency order.
 *
 * The harness reports this on startup so a run's telemetry records which
 * wire revision produced it.
 */
export const stack = (): ReadonlyArray<StackEntry> =>
  [WIRE_PACKAGE, TELEMETRY_PACKAGE, HARNESS_PACKAGE].map((name) => ({
    package: name,
    wireRevision: WIRE_REVISION,
  }));
