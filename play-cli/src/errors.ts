/**
 * Tagged errors, as values.
 *
 * Port of `play/client.py:167-195` (`PlayerError`, `V2ResponseError`) plus the
 * three port-local failures the Python expressed as `PlayerError` subclasses of
 * convenience.  Nothing here ever escapes as a thrown exception: every failure
 * travels in the Effect error channel and is mapped to stderr text and an exit
 * code in exactly one place, `src/cli-main.ts`.
 */
import { Data, Either } from 'effect';

/** A stable, user-facing player client failure. Python: `PlayerError`. */
export class PlayerError extends Data.TaggedError('PlayerError')<{
  readonly message: string;
}> {}

/**
 * A validated non-receipt full-control-v2 error response.
 *
 * Python builds the message as
 * `HTTP {status}: {payload.error.message} ({payload.error.code})`; the port
 * keeps that byte shape because `cli-main` prints it as `error: {message}`.
 */
export class V2ResponseError extends Data.TaggedError('V2ResponseError')<{
  readonly message: string;
  readonly status: number;
  readonly payload: unknown;
}> {}

/**
 * A validated payload that does not match the documented contract.
 *
 * `label` names the drifted thing (`"v2 page envelope"`), `message` is the
 * verbatim user-facing sentence the Python raised.  The two are separate
 * because renderers key remedies off the label while `cli-main` prints the
 * message.
 */
export class DriftError extends Data.TaggedError('DriftError')<{
  readonly label: string;
  readonly message: string;
}> {}

/** No session file this workspace could act with. Python: `_no_session_error`. */
export class SessionMissingError extends Data.TaggedError('SessionMissingError')<{
  readonly message: string;
}> {}

/** An alias whose revision is no longer the newest this client knows. */
export class AliasStaleError extends Data.TaggedError('AliasStaleError')<{
  readonly message: string;
  readonly alias: string;
}> {}

/** An advisory lock that could not be taken inside its timeout. */
export class LockTimeoutError extends Data.TaggedError('LockTimeoutError')<{
  readonly message: string;
  readonly path: string;
}> {}

/** Every failure this CLI maps to an exit code. */
export type PlayError =
  | PlayerError
  | V2ResponseError
  | DriftError
  | SessionMissingError
  | AliasStaleError
  | LockTimeoutError;

/** Build a {@link DriftError} whose message is the label's `invalid …` form. */
export const invalid = (label: string, detail?: string): DriftError =>
  new DriftError({
    label,
    message: detail === undefined ? `invalid ${label}` : `invalid ${label}: ${detail}`,
  });

/** Build a {@link DriftError} with a message that is not the `invalid …` form. */
export const drifted = (label: string, message: string): DriftError =>
  new DriftError({ label, message });

/** Build a {@link PlayerError}. Sugar for the single-field constructor. */
export const playerError = (message: string): PlayerError => new PlayerError({ message });

/** The user-facing sentence any of the mapped failures prints after `error: `. */
export const errorMessage = (error: PlayError): string => error.message;

// ---------------------------------------------------------------------------
// Sync boundary runners — the codebase writes no bare try/catch
// ---------------------------------------------------------------------------

/**
 * Run a synchronous boundary thunk (fs, URL, JSON.parse); a throw becomes the
 * fallback.  Errors stay values everywhere above this line.
 */
export const attemptOr = <A, B>(thunk: () => A, orElse: (cause: unknown) => B): A | B => {
  const outcome = Either.try(thunk);
  return Either.isLeft(outcome) ? orElse(outcome.left) : outcome.right;
};
