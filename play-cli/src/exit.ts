/**
 * The exit-code contract.
 *
 * Port of `play/client.py:155-160` (`V2_WAIT_EXIT_*`) and `main()` at
 * 11888-11905.  Four codes and nothing else:
 *
 * - `0`  success, or `wait` woke on a reason that satisfied it;
 * - `2`  a refusal — every `PlayerError`/`V2ResponseError` path;
 * - `75` EX_TEMPFAIL — another seat still holds the phase, call again;
 * - `66` EX_NOINPUT — there is nothing left to wait for.
 */
import { Data } from 'effect';

export const EXIT_OK = 0;
export const EXIT_REFUSED = 2;
/** sysexits EX_TEMPFAIL: "call me again". */
export const EXIT_TEMPFAIL = 75;
/** sysexits EX_NOINPUT: "there is nothing left to wait for". */
export const EXIT_NOINPUT = 66;

export const V2_WAIT_EXIT_ACTIVE = EXIT_OK;
export const V2_WAIT_EXIT_RETRY = EXIT_TEMPFAIL;
export const V2_WAIT_EXIT_TERMINAL = EXIT_NOINPUT;

/** The complete set of statuses this CLI is allowed to exit with. */
export type ExitCode = 0 | 2 | 75 | 66;

export const EXIT_CODES: ReadonlySet<number> = new Set<number>([
  EXIT_OK,
  EXIT_REFUSED,
  EXIT_TEMPFAIL,
  EXIT_NOINPUT,
]);

/** Narrow an arbitrary number to the contract, refusing anything outside it. */
export const isExitCode = (value: number): value is ExitCode => EXIT_CODES.has(value);

/**
 * Map a wake-driven status to the contract.
 *
 * `wait`, `monitor` and `do --end --await` all pass their own status through
 * unchanged; anything they hand back that is not in the contract is a bug, and
 * collapsing it to `EXIT_REFUSED` is what the Python's `int(handler(args))`
 * plus its two `except` arms effectively did.
 */
export const passThroughExit = (status: number): ExitCode =>
  isExitCode(status) ? status : EXIT_REFUSED;

/**
 * "Finish with this status and print nothing more."
 *
 * `wait`, `monitor` and `turn --end --await` all succeed while exiting 75 or
 * 66; there is no error to report and nothing further to render, so they end by
 * failing with this signal and `cli-main` turns it straight into the process
 * status.  It is deliberately NOT a `PlayerError`: nothing prints `error: …`
 * for it, and an applied phase end must never exit non-zero *as an error*
 * because of how the wait after it turned out.
 */
export class ExitCodeSignal extends Data.TaggedError('ExitCodeSignal')<{
  readonly code: ExitCode;
}> {}

export const exitWith = (status: number): ExitCodeSignal =>
  new ExitCodeSignal({ code: passThroughExit(status) });
