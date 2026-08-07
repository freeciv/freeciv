/**
 * What `start` prints, and what it says when it refuses.
 *
 * The rendering half of `command_start` (client.py:11013-11155) plus the two
 * refusal sentences `_pregame_choice` raises (10920-10927).
 *
 * A pregame refusal is the agent's whole recovery surface: the seat is not
 * configured, no other command works yet, and the only way out is to type a
 * name this lobby actually offers.  So an unmatched `--nation` never guesses —
 * it prints the near misses, and the near misses are the answer.
 */
import { playerError, type PlayerError } from 'src/errors';

/** How many catalog names one refusal is allowed to spend on the agent. */
export const PREGAME_SHOWN_MAX = 12;

// ---------------------------------------------------------------------------
// repr
// ---------------------------------------------------------------------------

const REPR_ESCAPES: ReadonlyMap<string, string> = new Map([
  ['\\', '\\\\'],
  ['\n', '\\n'],
  ['\r', '\\r'],
  ['\t', '\\t'],
]);

/**
 * CPython's `repr()` for a `str`, which is what `{wanted!r}` puts in the
 * refusal.  Single quotes unless the text contains one and no double quote.
 */
export const pyRepr = (text: string): string => {
  const quote = text.includes("'") && !text.includes('"') ? '"' : "'";
  const body = Array.from(text)
    .map((character) => {
      const escaped = REPR_ESCAPES.get(character);
      if (escaped !== undefined) return escaped;
      if (character === quote) return `\\${character}`;
      const code = character.codePointAt(0) ?? 0;
      if (code < 0x20 || code === 0x7f) {
        return `\\x${code.toString(16).padStart(2, '0')}`;
      }
      return character;
    })
    .join('');
  return `${quote}${body}${quote}`;
};

// ---------------------------------------------------------------------------
// The two catalog refusals
// ---------------------------------------------------------------------------

/**
 * The names a refusal offers back: the near misses when there are any, the
 * whole catalog otherwise, capped — and a sentence rather than an empty tail
 * when the lobby offered nothing at all.
 */
export const shownChoices = (names: ReadonlyArray<string>): string => {
  const shown = names.slice(0, PREGAME_SHOWN_MAX).join(' ');
  return shown === '' ? 'none were offered' : shown;
};

/** `no {label} named {wanted!r} is offered; try one of: …` */
export const unknownChoiceRefusal = (
  label: string,
  wanted: string,
  names: ReadonlyArray<string>
): PlayerError =>
  playerError(
    `no ${label} named ${pyRepr(wanted)} is offered; try one of: ${shownChoices(names)}`
  );

/** A catalog that offers the same name twice is broken, not ambiguous input. */
export const ambiguousChoiceRefusal = (label: string, wanted: string): PlayerError =>
  playerError(
    `${label} ${pyRepr(wanted)} is offered more than once; this lobby catalog is ambiguous`
  );

// ---------------------------------------------------------------------------
// The printed block
// ---------------------------------------------------------------------------

/** `male` / `female`, the one word both the headline and the intent use. */
export const sexWord = (male: boolean): string => (male ? 'male' : 'female');

/**
 * The first line: what this command is about to make this seat.
 *
 * A style the mirror could not name is `the nation default` rather than an
 * opaque id — the id is already in `--json`, and the line is for a reader.
 */
export const startingLine = (
  nation: string,
  leader: string,
  male: boolean,
  styleName: string
): string =>
  `starting as ${nation} — ${leader} (${sexWord(male)}), style ${
    styleName === '' ? 'the nation default' : styleName
  }`;

/** The intent `_render_disposition` prints beside the configure receipt. */
export const configureIntent = (nation: string, leader: string, male: boolean): string =>
  `configure ${nation} ${leader} ${sexWord(male)}`;

/** The intent for the second command of the pair. */
export const READY_INTENT = 'set ready';

/** Said instead of readying when the configuration itself was not accepted. */
export const NOT_READIED_LINE = 'not readied: the configuration was not accepted';

/**
 * The composite `--json` payload, with `command_start`'s exact part names.
 *
 * `dispositions` is `unknown` for the same reason `do`'s composite payload is
 * (`src/commands/do.cmd.ts`): the records are U13's decoded
 * `BatchDisposition`s, which are the objects `printV2Json` serializes, and a
 * decoded envelope is a nominal interface rather than a `JsonObject`.
 */
export const startJson = (parts: {
  readonly nation: string;
  readonly leader: string;
  readonly male: boolean;
  readonly styleId: string;
  readonly dispositions: ReadonlyArray<unknown>;
}): unknown => ({
  schema_version: 1,
  command: 'start',
  nation: parts.nation,
  leader: parts.leader,
  is_male: parts.male,
  style_id: parts.styleId,
  dispositions: [...parts.dispositions],
});
