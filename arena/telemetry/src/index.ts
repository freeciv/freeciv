/**
 * Barrel for `@arena/telemetry`.
 *
 * Phase 0 of the port: the package exists and its dependency on `evlog` is
 * pinned and resolving.  The wide-event drain and the run-trace writer land
 * here; callers import them from this file rather than from `src/`.
 */

/** Identity of this package, used by the harness to report its stack. */
export const TELEMETRY_PACKAGE = '@arena/telemetry' as const;
