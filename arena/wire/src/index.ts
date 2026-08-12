/**
 * Barrel for `@arena/wire`.
 *
 * Phase 0 of the port: the package exists, the toolchain is wired, and nothing
 * else is claimed yet.  Packet schemas and codecs land here; every other arena
 * package imports them from this file rather than reaching into `src/`.
 */

/** Identity of this package, used by the harness to report its stack. */
export const WIRE_PACKAGE = '@arena/wire' as const;

/** The wire format revision this build speaks.  Bumped when packets change. */
export const WIRE_REVISION = 0 as const;
