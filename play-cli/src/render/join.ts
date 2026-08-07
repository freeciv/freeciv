/**
 * The join card, the seat-binding sentence and the one protocol card.
 *
 * Ports `_seat_binding_line` (client.py:898-913), `_render_join` (5965-6009),
 * `V2_PROTOCOL_CARD` (2956-3000) and the two stderr conduct blocks
 * `command_join` prints after the card (6371-6417).
 *
 * OWNERSHIP NOTE (NOTES.md §U02): PORT_MAP §0 puts every shared `V2_*` constant
 * in `src/constants.ts`, but the card at client.py:2956 is outside the line
 * ranges that row enumerates and the port brief assigns it to U02.  It is
 * exported from here; `state/header.txt` (U04's `_mirror_health` bridge, which
 * passes it as `commands=`) imports it from this module.
 */
import { Option } from 'effect';
import { FULL_CONTROL_V2 } from 'src/constants';
import { formatG, scalar } from 'src/render/primitives';
import { pyFloatRepr } from 'src/services/canonical-body';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import type { SeatBinding } from 'src/services/session-store';

// ---------------------------------------------------------------------------
// client.py:2952-3000 — the one protocol card
// ---------------------------------------------------------------------------

/**
 * The one protocol card.  Join prints it, and `state/header.txt` carries the
 * same text, so an agent that lost its join output can re-read the contract
 * from a file instead of re-reading the docs.  Everything here names a command
 * that exists; nothing here restates a contract an error already carries.
 */
export const V2_PROTOCOL_CARD: ReadonlyArray<string> = [
  'ALIASES — type these anywhere an ID is accepted: a1..aN one enumerated ' +
    'action (dies with its revision), u1/c1/p1/r1 a unit, city, player, or ' +
    'relation (stable all game), T(x,y) a tile you have seen. They expand ' +
    "locally; the wire carries the server's opaque ID.",
  'ERRORS carry their own remedy: every refusal names the exact command ' +
    'that fixes it.',
  'just start                                get into the game; every flag ' +
    '(--nation --leader --male|--female --style) is an override',
  'just turn                                 one briefing, one revision',
  'just turn --decisions                     one row per actor that can act',
  'just do "u1 VERB ARGS; c1 VERB ARGS"      1..8 orders, one receipt each',
  'ONE CALL PER TURN — just do "u1 VERB ARGS; c1 VERB ARGS" --end --await ' +
    '--brief orders every actor, ends the phase, blocks, and prints the next ' +
    'briefing. Batch every actor into one line; the `next N actors:` tail ' +
    'writes that line for you.',
  'just turn --end --await --brief           end, block, next full briefing',
  'just show [ALIAS|--grep PATTERN]          read local files, zero network',
  'just state --section SECTION              one bounded state page; ' +
    'section chat is the typed event feed (tech learned, growth, huts)',
  "just legal --actor_id ID --all            one actor's whole menu",
  'just legal --kind KIND --all              one class of action',
  'just batch --action_id ID --arguments JSON',
  'just receipt --batch_id ID | just retry --batch_id ID | just wait',
  'MULTIPLAYER — seats alternate; one phase is open at a time and nothing ' +
    "shortens another seat's. Health says NOT YOUR TURN and names who holds " +
    'it, for how long, and how long is left.',
  'WHICH BINDING: if you can hold one command open for up to 10m, step 4 ' +
    '(--end --await --brief) is one call per turn and needs nothing else. If ' +
    'your harness backgrounds long calls instead, use `just do "..." --end` ' +
    'and `just monitor --once` in the background — its exit is your wake-up. ' +
    'Either way a persistent `just monitor` is the safety net, and starting ' +
    'one is expected.',
  'just wait --for-turn                      block until the phase is ' +
    'yours. exit 0 yours, 75 still theirs (call again), 66 game over',
  'just monitor                              sanctioned read-only watcher: ' +
    'announces every turn you are given, and is the only thing that will tell ' +
    'you a turn opened and died unplayed. It watches and notifies, it never ' +
    'acts — --exec may notify your harness, never issue game commands (that ' +
    'is the automated bot the rules forbid). --once blocks, announces, exits.',
  "just use GAME_ID                          rebind this workspace's seat",
  'add --json to any of these for the full wire payload; join bound this ' +
    'workspace to your seat, so no command takes a session argument',
];

// ---------------------------------------------------------------------------
// client.py:898-913 — the seat-binding sentence
// ---------------------------------------------------------------------------

/** State the one fact a bound workspace changes: no command needs a seat. */
export const seatBindingLine = (
  gameId: string,
  replaced: Option.Option<SeatBinding> = Option.none()
): string => {
  const rebound = Option.isNone(replaced)
    ? ''
    : replaced.value.gameId !== gameId
      ? `, rebound from ${replaced.value.gameId}`
      : ', rebound to another seat in the same game';
  return `this workspace is now playing ${gameId}${rebound} — commands need no --session`;
};

// ---------------------------------------------------------------------------
// client.py:5965-6009 — the join card
// ---------------------------------------------------------------------------

/** `dict.get(key)`: a missing key is `None`, exactly as CPython reads it. */
const at = (value: JsonObject, key: string): JsonValue => {
  const found = value[key];
  return found === undefined ? null : found;
};

const text = (value: JsonObject, key: string): string => {
  const found = value[key];
  return typeof found === 'string' ? found : '';
};

/**
 * The join block.
 *
 * The session file path deliberately does not appear: printing it is what
 * taught every observed agent to re-type it on 79 of 82 commands, and `just
 * use` prints it when an operator actually needs it.  CPython took the path as
 * an argument and never used it, so the port drops the parameter.
 */
export const renderJoin = (
  session: JsonObject,
  result: JsonObject,
  replaced: Option.Option<SeatBinding> = Option.none()
): ReadonlyArray<string> => {
  const timeout = at(session, 'action_timeout_s');
  const timing =
    `timing ${scalar(at(session, 'timing_mode'))}` +
    (timeout === null ? ' no deadline' : ` ${scalar(timeout)}s per turn`);
  const protocol = text(session, 'control_protocol');
  const gameId = text(session, 'game_id');
  const head =
    `joined ${gameId} as ${text(session, 'controller_label')} | ` +
    `seat ${scalar(at(session, 'place'))} ` +
    `${scalar(at(session, 'player_name'))} | proto ${protocol} | ` +
    `state ${scalar(at(result, 'state'))} | ${timing}`;
  const objective =
    'objective' in session
      ? [
          `objective ${scalar(at(session, 'objective'))} | max_turns ` +
            `${scalar(at(session, 'max_turns'))} | turns_remaining ` +
            `${scalar(at(session, 'turns_remaining'))}`,
        ]
      : [];
  const protocolLines =
    protocol === FULL_CONTROL_V2
      ? [
          'PROTOCOL full-control-v2 — read state, execute one enumerated ' +
            'opaque action, resolve its receipt. The same card is ' +
            'state/header.txt.',
          ...V2_PROTOCOL_CARD,
        ]
      : [
          'PROTOCOL strategic-v1 — poll a turn, submit one action.',
          '  just next --after_turn LAST_TURN',
          '  just act --turn TURN --observation_id ID --action JSON',
        ];
  return [head, seatBindingLine(gameId, replaced), ...objective, ...protocolLines];
};

// ---------------------------------------------------------------------------
// client.py:6371-6417 — the conduct block, on stderr
// ---------------------------------------------------------------------------

/** `f"{action_timeout_s:g} seconds per agent turn"`, or why there is none. */
export const deadlineText = (actionTimeoutS: JsonValue): string => {
  if (actionTimeoutS === null) return 'no agent deadline';
  if (typeof actionTimeoutS === 'number' && Number.isFinite(actionTimeoutS)) {
    return `${formatG(actionTimeoutS)} seconds per agent turn`;
  }
  return 'deadline unavailable';
};

const capitalized = (line: string): string => line.charAt(0).toUpperCase() + line.slice(1);

// ---------------------------------------------------------------------------
// `str(result.get("timing_mode") or "unknown")` — client.py:6350
// ---------------------------------------------------------------------------

/**
 * CPython truthiness.  `None`, `False`, `0`, `0.0`, `""`, `[]` and `{}` are all
 * falsy, which is the whole reason `_render_join`'s sibling writes `or
 * "unknown"`: `timing_mode` is a v1 join field with no schema in
 * `full-control-v2.openapi.json`, so `or` is the drift absorber, and a port
 * that only catches `None` prints `Timing mode: ;` on an empty string.
 */
const pyTruthy = (value: JsonValue): boolean => {
  if (value === null || value === false) return false;
  if (value === true) return true;
  if (typeof value === 'number') return value !== 0;
  if (typeof value === 'string') return value !== '';
  if (Array.isArray(value)) return value.length > 0;
  return Object.keys(value).length > 0;
};

/** CPython's `repr()` of a `str`: single quotes unless that costs an escape. */
const reprString = (text: string): string => {
  const escaped = text
    .replace(/\\/g, '\\\\')
    .replace(/\n/g, '\\n')
    .replace(/\r/g, '\\r')
    .replace(/\t/g, '\\t');
  return escaped.includes("'") && !escaped.includes('"')
    ? `"${escaped}"`
    : `'${escaped.replace(/'/g, "\\'")}'`;
};

/**
 * CPython's `repr()` of a decoded JSON value.
 *
 * The int/float distinction JSON carries and JavaScript does not is resolved
 * the way every other renderer in this port resolves it (NOTES §16.4): an
 * integral number is an `int`.  `repr(5.0)` is therefore `5`, not `5.0` — the
 * one residue, and it is unreachable from `timing_mode` in practice.
 */
const pyRepr = (value: JsonValue): string => {
  if (value === null) return 'None';
  if (value === true) return 'True';
  if (value === false) return 'False';
  if (typeof value === 'number') {
    return Number.isInteger(value) ? String(value) : pyFloatRepr(value);
  }
  if (typeof value === 'string') return reprString(value);
  if (Array.isArray(value)) return `[${value.map((item) => pyRepr(item)).join(', ')}]`;
  return `{${Object.entries(value)
    .map(([key, item]) => `${reprString(key)}: ${pyRepr(item)}`)
    .join(', ')}}`;
};

/** CPython's `str(value)`: a `str` is itself, everything else is its `repr()`. */
const pyStr = (value: JsonValue): string =>
  typeof value === 'string' ? value : pyRepr(value);

/**
 * `str(result.get("timing_mode") or "unknown")`.
 *
 * Exported because both branches of `joinGuidance` interpolate it and a test
 * has to be able to pin the falsy table (`null`, `''`, `0`, `false`, `[]`,
 * `{}` all print `unknown`) without going through the whole command.
 */
export const timingModeText = (result: JsonObject): string => {
  const mode = at(result, 'timing_mode');
  return pyTruthy(mode) ? pyStr(mode) : 'unknown';
};

/**
 * Turn one is where the model forms its API model, so exactly one protocol
 * contract is taught: the card already printed on stdout (and kept in
 * `state/header.txt`).  This block carries only what the card cannot — the
 * evaluation frame and the three rules that are about conduct rather than
 * syntax.
 */
export const joinGuidance = (
  session: JsonObject,
  result: JsonObject,
  binding: string
): string => {
  const timingMode = timingModeText(result);
  const deadline = deadlineText(at(result, 'action_timeout_s'));
  if (text(session, 'control_protocol') !== FULL_CONTROL_V2) {
    return (
      `\nJoined in ${timingMode} timing mode: ${deadline}.\n` +
      'You—the assigned harness/model—must inspect each observation and ' +
      'choose its action directly. Do not write, launch, or delegate to ' +
      'an automated bot solely to beat the clock.\n' +
      `${capitalized(binding)}.\n` +
      'Read docs/gameplay.md, then start with ' +
      '`just next --after_turn 0`; every next and act command works ' +
      'against this bound seat.'
    );
  }
  const evaluationLine = !('objective' in session)
    ? ''
    : `Objective: ${scalar(at(session, 'objective'))}\n` +
      `Turn budget: ${scalar(at(session, 'max_turns'))} maximum; ` +
      (at(session, 'turns_remaining') === null
        ? 'remaining turns unavailable until native play starts.\n'
        : `${scalar(at(session, 'turns_remaining'))} remaining.\n`);
  return (
    '\nJoined a full-control-v2 session.\n' +
    `${capitalized(binding)}.\n` +
    `Timing mode: ${timingMode}; ${deadline}.\n` +
    `${evaluationLine}` +
    'Do not use strategic `just next` or `just act`. The command ' +
    'contract is the protocol card printed on stdout above; it is ' +
    'also saved in state/header.txt, so `just show header` re-reads ' +
    'it without a network call.\n' +
    'You—the assigned harness/model—must choose every action ' +
    'yourself. Do not write, launch, or delegate to an automated bot, ' +
    "and do not hand a unit to the game's own AI.\n" +
    'LOBBY FIRST: while health says game_state=lobby, do not call ' +
    '`just wait`. Run `just start --nation N --leader L ' +
    '--male|--female [--style S]`: it reads pregame_nations, ' +
    'pregame_styles and pregame_teams for you, then executes the ' +
    'enumerated pregame.configure and pregame.set_ready. The last ' +
    'ready seat starts Freeciv.\n' +
    'An ambiguous receipt is terminal and must never be replayed; ' +
    'resolve it with `just receipt --batch_id ID`.\n' +
    'Keep playing until the game is terminal; do not final-answer ' +
    'merely because one turn completed. A failed wait command is a ' +
    'harness error to correct, not a terminal game.'
  );
};
