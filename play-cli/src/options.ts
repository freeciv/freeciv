/**
 * The dual-spelling flag helper, in a module the command files can import.
 *
 * PLAN.md §3 rule 3: *both `--flag_s` and `--flag-s` spellings are accepted;
 * supplying both is an error.*  The justfile veneer is where the underscored
 * spelling came from — `[arg("after_turn", long)]`, `[arg("observation_id",
 * long)]`, `[arg("game_id", long)]` (justfile:104-117, 386) — and the bootstrap
 * card `prompt` prints teaches the underscored form (`just next --after_turn
 * LAST_TURN`), so folding the veneer into this CLI means folding both spellings
 * in with it.
 *
 * PORT_MAP §4.8 promises these four names from `src/cli-main.ts`, and they are
 * still exported from there — but a *command* file cannot import them from
 * `cli-main`, because `cli-main` imports the command files.  The cycle is
 * resolved during module evaluation, while `Command.make` is building its
 * options at the top level of the command module, so the import would read an
 * uninitialized binding and crash on `play --help`.  Core keeps the promised
 * export by re-exporting from here; see NOTES.md.
 */
import { Options } from '@effect/cli';
import { Effect, Option } from 'effect';
import { PlayerError, playerError } from 'src/errors';

/** One flag, two spellings, at most one of them supplied. */
export interface DualSpelling<A> {
  readonly dashed: Option.Option<A>;
  readonly underscored: Option.Option<A>;
}

const underscored = (dashedName: string): string => dashedName.replace(/-/g, '_');

const dual = <A>(
  dashedName: string,
  build: (name: string) => Options.Options<A>
): Options.Options<DualSpelling<A>> =>
  Options.all({
    dashed: Options.optional(build(dashedName)),
    underscored: Options.optional(build(underscored(dashedName))),
  });

/** `game-id` → an `Options` accepting `--game-id` and `--game_id`. */
export const dualText = (dashedName: string): Options.Options<DualSpelling<string>> =>
  dual(dashedName, Options.text);

export const dualFloat = (dashedName: string): Options.Options<DualSpelling<number>> =>
  dual(dashedName, Options.float);

export const dualInteger = (dashedName: string): Options.Options<DualSpelling<number>> =>
  dual(dashedName, Options.integer);

/**
 * Collapse the two spellings to at most one value, refusing the pair.
 *
 * The `Option` result is what callers that must distinguish *supplied* from
 * *defaulted* need — `next` is one, because argparse never ran `--wait-s`'s
 * `type=float` over its own default and so puts a different byte on the wire.
 */
export const resolveDualOption = <A>(
  dashedName: string,
  value: DualSpelling<A>
): Effect.Effect<Option.Option<A>, PlayerError> =>
  Option.isSome(value.dashed) && Option.isSome(value.underscored)
    ? Effect.fail(
        playerError(`pass only one of --${dashedName} or --${underscored(dashedName)}`)
      )
    : Effect.succeed(Option.isSome(value.underscored) ? value.underscored : value.dashed);

/** The same, with the default folded in. Matches PORT_MAP §4.8's signature. */
export const resolveDual = <A>(
  dashedName: string,
  value: DualSpelling<A>,
  fallback: A
): Effect.Effect<A, PlayerError> =>
  Effect.map(resolveDualOption(dashedName, value), Option.getOrElse(() => fallback));

/**
 * The same for a flag argparse marked `required=True`.
 *
 * Accepting two spellings costs the built-in required check, because neither
 * spelling can be individually mandatory.  The refusal text is argparse's, so
 * the stderr line `cli-main` prints stays the sentence the Python printed.
 */
export const resolveDualRequired = <A>(
  dashedName: string,
  value: DualSpelling<A>
): Effect.Effect<A, PlayerError> =>
  Effect.flatMap(resolveDualOption(dashedName, value), (resolved) =>
    Option.isSome(resolved)
      ? Effect.succeed(resolved.value)
      : Effect.fail(playerError(`the following arguments are required: --${dashedName}`))
  );
