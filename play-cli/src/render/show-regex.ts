/**
 * `show --grep --regex` — CPython's `re` dialect, on JavaScript's engine.
 *
 * `new RegExp(pattern, 'i')` is not `re.compile(pattern, re.IGNORECASE)`, and
 * the ways they differ all cost the agent a wrong answer:
 *
 * * **Silently different match sets.**  `\A` and `\Z` are anchors in `re` and
 *   identity escapes in a non-`u` JavaScript regex, so `--grep '\A' --regex`
 *   matched every line under CPython and only lines containing a literal `A`
 *   under the port.  `.` excludes `\r` in JavaScript and does not in `re`;
 *   `$` accepts a trailing newline in `re` and does not in JavaScript;
 *   `A{,2}` is `A{0,2}` in `re` and three literal characters in JavaScript;
 *   `\d`, `\w` and `\s` are Unicode-wide in `re` and ASCII in JavaScript.
 * * **Refusing what `re` accepts.**  Inline flags (`(?i)`, `(?s)`, `(?m)`,
 *   `(?a)`, `(?i:…)`), comments (`(?#…)`), Python-named groups (`(?P<x>…)`,
 *   `(?P=x)`), atomic groups and possessive quantifiers are all `re` syntax
 *   that JavaScript's parser rejects outright.
 * * **Accepting what `re` refuses.**  `(?<name>…)`, `\p{L}`, `\h`, `\Q`,
 *   `\cA`, `\8`, `\x{41}`, `[a-\d]` and a variable-width lookbehind are all
 *   errors in `re`; JavaScript compiles them and answers a different question.
 *
 * So the pattern is not handed to JavaScript at all.  It is parsed by a port of
 * CPython 3.14's `re/_parser.py` — same tokenizer, same checks, same messages
 * and same positions — into an AST, and the AST is then *emitted* as a
 * JavaScript source string whose semantics are `re`'s: `\A`/`\Z` become `^`/`$`
 * and `^`/`$` become the lookarounds that mean what `re` means by them, `.` and
 * every class become explicit code point ranges, `\d`/`\s`/`\w` become
 * CPython's own ranges (see `show-unicode.ts`), `IGNORECASE` is expanded into
 * CPython's case-equivalence classes rather than delegated to the `i` flag, and
 * atomic groups and possessive quantifiers are emulated with the
 * `(?=(…))\k<…>` identity.  Astral code points are written as surrogate pairs
 * and the whole pattern is prefixed with a guard against starting mid-pair, so
 * matching steps by code point exactly as `re` does over a `str` — see
 * `rangesMatcher` for why the `u` flag could not be used for that instead.
 *
 * One piece of CPython's *optimiser* is observable and is reproduced too — see
 * `charsetPrefilter`, which is why `(?a)(?u:\w)` refuses `ﬁ` here as it does
 * there.
 *
 * Measured against this machine's CPython over 140,000 generated patterns and
 * their subjects: no pattern matched differently, no message differed, and
 * nothing was accepted that `re` rejects or rejected that `re` accepts, apart
 * from the two constructs recorded in NOTES.md §19.3 — a conditional group reference
 * `(?(1)…)`, and a backreference to a group that may not have taken part, both
 * of which are refused rather than answered.  Also in §19.3: `\N{…}` resolves
 * names from a bounded table; group names are validated with JavaScript's
 * `ID_Start` tables rather than this CPython's; CPython's `FutureWarning`
 * for `[[` and friends is not reproduced on stderr; and a pattern whose
 * *emitted* source outgrows JavaScriptCore's ~1 MB ceiling (`\w` a hundred
 * times over) is refused rather than answered — as a returned `Either.left`,
 * never a throw.
 */
import { Either } from 'effect';
import {
  ASCII_CASED_CODE_POINTS,
  CASED_CODE_POINTS,
  UNICODE_DIGIT_RANGES,
  UNICODE_SPACE_RANGES,
  UNICODE_WORD_RANGES,
  asciiCaseClass,
  asciiIsCased,
  lookupCharacterName,
  unicodeCaseClass,
  unicodeIsCased,
  type CodeRange,
} from 'src/render/show-unicode';

// ---------------------------------------------------------------------------
// Flags — the `sre` bit values, so the inline-flag parser can be a transcript
// ---------------------------------------------------------------------------

export const PY_IGNORECASE = 2;
const PY_LOCALE = 4;
const PY_MULTILINE = 8;
const PY_DOTALL = 16;
const PY_UNICODE = 32;
const PY_VERBOSE = 64;
const PY_DEBUG = 128;
const PY_ASCII = 256;

const TYPE_FLAGS = PY_ASCII | PY_LOCALE | PY_UNICODE;
const GLOBAL_FLAGS = PY_DEBUG;

const FLAG_LETTERS: ReadonlyMap<string, number> = new Map([
  ['i', PY_IGNORECASE],
  ['L', PY_LOCALE],
  ['m', PY_MULTILINE],
  ['s', PY_DOTALL],
  ['x', PY_VERBOSE],
  ['a', PY_ASCII],
  ['u', PY_UNICODE],
]);

/** `_constants.MAXREPEAT`: both the "unbounded" sentinel and the overflow wall. */
const MAXREPEAT = 4294967295;
/** `_constants.MAXGROUPS`. */
const MAXGROUPS = 1073741823;
/** `_compiler.MAXCODE`, the bound on how far a lookbehind may look. */
const MAXCODE = 0xffff;

const MAX_CODE_POINT = 0x10ffff;

// ---------------------------------------------------------------------------
// The failure value
// ---------------------------------------------------------------------------

/**
 * A pattern CPython would not compile — or, for `kind: 'unsupported'`, one it
 * would compile and this build cannot emit.
 *
 * `message` is `str(exc)` verbatim: the bare message, `" at position N"` when
 * the parser knew where it was, and the `" (line L, column C)"` tail CPython
 * adds when the pattern itself holds a newline.
 */
export interface PyRegexFailure {
  readonly _tag: 'PyRegexFailure';
  readonly kind: 'error' | 'uncaught' | 'unsupported';
  readonly message: string;
}

const failure = (kind: PyRegexFailure['kind'], message: string): PyRegexFailure => ({
  _tag: 'PyRegexFailure',
  kind,
  message,
});

/**
 * The parser signals through a throw and the module boundary turns it back into
 * a value.  A recursive-descent parser threading `Either` through forty call
 * sites would say nothing the transcript of `_parser.py` does not already say,
 * and nothing outside this file can observe the throw: `compilePythonRegex`
 * returns `Either` and never raises.
 */
class ParseSignal extends Error {
  constructor(readonly failure: PyRegexFailure) {
    super(failure.message);
  }
}

// ---------------------------------------------------------------------------
// The AST
// ---------------------------------------------------------------------------

type AtKind =
  | 'beginning'
  | 'beginningString'
  | 'end'
  | 'endString'
  | 'boundary'
  | 'nonBoundary';

type CategoryKind = 'digit' | 'notDigit' | 'space' | 'notSpace' | 'word' | 'notWord';

type SetItem =
  | { readonly kind: 'literal'; readonly code: number }
  | { readonly kind: 'range'; readonly low: number; readonly high: number }
  | { readonly kind: 'category'; readonly category: CategoryKind };

type RepeatMode = 'greedy' | 'lazy' | 'possessive';

type Node =
  | { readonly kind: 'literal'; readonly code: number }
  | { readonly kind: 'any' }
  | { readonly kind: 'in'; readonly negate: boolean; readonly items: ReadonlyArray<SetItem> }
  | { readonly kind: 'at'; readonly at: AtKind }
  | { readonly kind: 'branch'; readonly branches: ReadonlyArray<Sub> }
  | {
      readonly kind: 'group';
      readonly group: number | null;
      readonly addFlags: number;
      readonly delFlags: number;
      readonly body: Sub;
    }
  | { readonly kind: 'atomic'; readonly body: Sub }
  | {
      readonly kind: 'repeat';
      readonly min: number;
      readonly max: number;
      readonly mode: RepeatMode;
      readonly body: Sub;
    }
  | { readonly kind: 'ref'; readonly group: number }
  | {
      readonly kind: 'assert';
      readonly direction: 1 | -1;
      readonly negate: boolean;
      readonly body: Sub;
    }
  | { readonly kind: 'failure' }
  | {
      readonly kind: 'conditional';
      readonly group: number;
      readonly yes: Sub;
      readonly no: Sub | null;
    };

/** A parsed sequence.  Mutable while parsing, exactly like `SubPattern.data`. */
type Sub = Node[];

/** An escape's meaning outside a character class. */
type Escape =
  | { readonly kind: 'item'; readonly item: SetItem }
  | { readonly kind: 'at'; readonly at: AtKind }
  | { readonly kind: 'ref'; readonly group: number };

// ---------------------------------------------------------------------------
// Tokenizer — `_parser.Tokenizer`, indexed by code point
// ---------------------------------------------------------------------------

const DIGITS = '0123456789';
const OCTDIGITS = '01234567';
const HEXDIGITS = '0123456789abcdefABCDEF';
const ASCIILETTERS = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ';
const WHITESPACE = ' \t\n\r\v\f';
const SPECIAL_CHARS = '.\\[{()*+?^$|';
const REPEAT_CHARS = '*+?{';

/**
 * `re` indexes a `str` by code point, so every position in an error message is
 * a code point offset.  The tokenizer therefore walks an array of code points
 * and never the UTF-16 units `String` would hand it, which is the difference
 * between CPython's `at position 3` and `at position 4` on a pattern holding an
 * astral character.
 */
class Tokenizer {
  private readonly chars: ReadonlyArray<string>;
  private index = 0;
  next: string | null = null;

  constructor(readonly pattern: string) {
    this.chars = [...pattern];
    this.advance();
  }

  get length(): number {
    return this.chars.length;
  }

  private advance(): void {
    let index = this.index;
    const first = this.chars[index];
    if (first === undefined) {
      this.next = null;
      return;
    }
    let char = first;
    if (char === '\\') {
      index += 1;
      const second = this.chars[index];
      if (second === undefined) {
        throw new ParseSignal(this.formatted('bad escape (end of pattern)', this.length - 1));
      }
      char += second;
    }
    this.index = index + 1;
    this.next = char;
  }

  match(char: string): boolean {
    if (char === this.next) {
      this.advance();
      return true;
    }
    return false;
  }

  get(): string | null {
    const current = this.next;
    this.advance();
    return current;
  }

  getWhile(count: number, charset: string): string {
    let result = '';
    for (let taken = 0; taken < count; taken += 1) {
      const char = this.next;
      if (char === null || !charset.includes(char)) break;
      result += char;
      this.advance();
    }
    return result;
  }

  getUntil(terminator: string, name: string): string {
    let result = '';
    for (;;) {
      const char = this.next;
      this.advance();
      if (char === null) {
        if (result === '') throw this.error(`missing ${name}`);
        throw this.error(`missing ${terminator}, unterminated name`, [...result].length);
      }
      if (char === terminator) {
        if (result === '') throw this.error(`missing ${name}`, 1);
        break;
      }
      result += char;
    }
    return result;
  }

  tell(): number {
    return this.index - (this.next === null ? 0 : [...this.next].length);
  }

  seek(index: number): void {
    this.index = index;
    this.advance();
  }

  /** `Tokenizer.error` — the position is `tell() - offset`, negatives included. */
  error(message: string, offset = 0): ParseSignal {
    return new ParseSignal(this.formatted(message, this.tell() - offset));
  }

  private formatted(message: string, position: number): PyRegexFailure {
    return failure('error', formatPatternError(message, this.chars, position));
  }

  /** `Tokenizer.checkgroupname`. */
  checkGroupName(name: string, offset: number): void {
    if (!isPythonIdentifier(name)) {
      throw this.error(`bad character in group name ${pyRepr(name)}`, [...name].length + offset);
    }
  }
}

/**
 * `PatternError.__init__` — message, position, and the line/column tail CPython
 * appends only when the pattern itself contains a newline.
 */
const formatPatternError = (
  message: string,
  chars: ReadonlyArray<string>,
  position: number
): string => {
  const withPosition = `${message} at position ${position}`;
  if (!chars.includes('\n')) return withPosition;
  const before = chars.slice(0, Math.max(position, 0));
  const lineNumber = before.filter((char) => char === '\n').length + 1;
  const columnNumber = position - before.lastIndexOf('\n');
  return `${withPosition} (line ${lineNumber}, column ${columnNumber})`;
};

/**
 * `str.isidentifier`.  CPython tests XID_Start/XID_Continue from *its* Unicode
 * build; this uses JavaScript's `ID_Start`/`ID_Continue`, which agree on every
 * ASCII name and can disagree on a group name in a script added between the two
 * Unicode versions.  NOTES.md §19.3 records it.
 */
const IDENTIFIER_RE = /^[_\p{ID_Start}][_\p{ID_Continue}]*$/u;

const isPythonIdentifier = (name: string): boolean => IDENTIFIER_RE.test(name);

/**
 * `repr()` for the strings CPython interpolates into `re` messages (group and
 * character names).  Those are short and printable in every reachable case, so
 * this is the quote choice plus a backslash escape.
 */
const pyRepr = (text: string): string => {
  const quote = text.includes("'") && !text.includes('"') ? '"' : "'";
  const escaped = [...text]
    .map((char) => {
      if (char === '\\') return '\\\\';
      if (char === quote) return `\\${char}`;
      const code = char.codePointAt(0) ?? 0;
      if (code < 0x20 || code === 0x7f) return `\\x${code.toString(16).padStart(2, '0')}`;
      return char;
    })
    .join('');
  return `${quote}${escaped}${quote}`;
};

// ---------------------------------------------------------------------------
// Parser state — `_parser.State`
// ---------------------------------------------------------------------------

type Width = readonly [number, number];

class ParserState {
  flags = 0;
  readonly groupdict = new Map<string, number>();
  readonly groupwidths: Array<Width | null> = [null];
  lookbehindgroups: number | null = null;
  readonly grouprefpos = new Map<number, number>();

  get groups(): number {
    return this.groupwidths.length;
  }

  openGroup(source: Tokenizer, name: string | null): number {
    const gid = this.groups;
    this.groupwidths.push(null);
    if (this.groups > MAXGROUPS) throw source.error('too many groups');
    if (name !== null) {
      const existing = this.groupdict.get(name);
      if (existing !== undefined) {
        throw source.error(
          `redefinition of group name ${pyRepr(name)} as group ${gid}; was group ${existing}`,
          [...name].length + 1
        );
      }
      this.groupdict.set(name, gid);
    }
    return gid;
  }

  closeGroup(gid: number, body: Sub): void {
    this.groupwidths[gid] = getWidth(body, this);
  }

  checkGroup(gid: number): boolean {
    return gid < this.groups && this.groupwidths[gid] !== null && this.groupwidths[gid] !== undefined;
  }

  checkLookbehindGroup(gid: number, source: Tokenizer): void {
    if (this.lookbehindgroups === null) return;
    if (!this.checkGroup(gid)) throw source.error('cannot refer to an open group');
    if (gid >= this.lookbehindgroups) {
      throw source.error('cannot refer to group defined in the same lookbehind subpattern');
    }
  }
}

const MAXWIDTH = Number.MAX_SAFE_INTEGER;

/** `SubPattern.getwidth`, the input to the fixed-width lookbehind check. */
const getWidth = (nodes: Sub, state: ParserState): Width => {
  let low = 0;
  let high = 0;
  for (const node of nodes) {
    switch (node.kind) {
      case 'branch': {
        let branchLow = MAXWIDTH;
        let branchHigh = 0;
        for (const branch of node.branches) {
          const [subLow, subHigh] = getWidth(branch, state);
          branchLow = Math.min(branchLow, subLow);
          branchHigh = Math.max(branchHigh, subHigh);
        }
        low += branchLow;
        high += branchHigh;
        break;
      }
      case 'atomic':
      case 'group': {
        const [subLow, subHigh] = getWidth(node.body, state);
        low += subLow;
        high += subHigh;
        break;
      }
      case 'repeat': {
        const [subLow, subHigh] = getWidth(node.body, state);
        low += subLow * node.min;
        high = node.max === MAXREPEAT && subHigh !== 0 ? MAXWIDTH : high + subHigh * node.max;
        break;
      }
      case 'any':
      case 'in':
      case 'literal':
        low += 1;
        high += 1;
        break;
      case 'ref': {
        const width = state.groupwidths[node.group] ?? [0, 0];
        low += width[0];
        high += width[1];
        break;
      }
      case 'conditional': {
        const [yesLow, yesHigh] = getWidth(node.yes, state);
        const [noLow, noHigh] =
          node.no === null ? ([0, 0] as const) : getWidth(node.no, state);
        low += node.no === null ? 0 : Math.min(yesLow, noLow);
        high += Math.max(yesHigh, noHigh);
        break;
      }
      default:
        break;
    }
  }
  return [Math.min(low, MAXWIDTH), Math.min(high, MAXWIDTH)];
};

// ---------------------------------------------------------------------------
// Escapes — `_parser._class_escape` and `_parser._escape`
// ---------------------------------------------------------------------------

const SIMPLE_ESCAPES: ReadonlyMap<string, number> = new Map([
  ['\\a', 0x07],
  ['\\b', 0x08],
  ['\\f', 0x0c],
  ['\\n', 0x0a],
  ['\\r', 0x0d],
  ['\\t', 0x09],
  ['\\v', 0x0b],
  ['\\\\', 0x5c],
]);

const CATEGORY_ESCAPES: ReadonlyMap<string, CategoryKind> = new Map([
  ['\\d', 'digit'],
  ['\\D', 'notDigit'],
  ['\\s', 'space'],
  ['\\S', 'notSpace'],
  ['\\w', 'word'],
  ['\\W', 'notWord'],
]);

const AT_ESCAPES: ReadonlyMap<string, AtKind> = new Map([
  ['\\A', 'beginningString'],
  ['\\b', 'boundary'],
  ['\\B', 'nonBoundary'],
  ['\\z', 'endString'],
  ['\\Z', 'endString'],
]);

/**
 * `\N{…}`.  The syntax errors are CPython's; the name lookup is the bounded
 * table in `show-unicode.ts`, so a name outside it is reported undefined —
 * CPython's own answer for a name that does not exist, and a refusal rather
 * than a wrong match for one that does.
 */
const readNamedCharacter = (source: Tokenizer): number => {
  if (!source.match('{')) throw source.error('missing {');
  const name = source.getUntil('}', 'character name');
  const code = lookupCharacterName(name);
  if (code === null) {
    throw source.error(
      `undefined character name ${pyRepr(name)}`,
      [...name].length + '\\N{}'.length
    );
  }
  return code;
};

/** `_class_escape` — the escapes legal *inside* `[...]`. */
const classEscape = (source: Tokenizer, escape: string): SetItem => {
  const simple = SIMPLE_ESCAPES.get(escape);
  if (simple !== undefined) return { kind: 'literal', code: simple };
  const category = CATEGORY_ESCAPES.get(escape);
  if (category !== undefined) return { kind: 'category', category };

  const char = escape.slice(1, 2);
  if (char === 'x') {
    const full = escape + source.getWhile(2, HEXDIGITS);
    if (full.length !== 4) throw source.error(`incomplete escape ${full}`, full.length);
    return { kind: 'literal', code: Number.parseInt(full.slice(2), 16) };
  }
  if (char === 'u') {
    const full = escape + source.getWhile(4, HEXDIGITS);
    if (full.length !== 6) throw source.error(`incomplete escape ${full}`, full.length);
    return { kind: 'literal', code: Number.parseInt(full.slice(2), 16) };
  }
  if (char === 'U') {
    const full = escape + source.getWhile(8, HEXDIGITS);
    if (full.length !== 10) throw source.error(`incomplete escape ${full}`, full.length);
    const code = Number.parseInt(full.slice(2), 16);
    if (code > MAX_CODE_POINT) throw source.error(`bad escape ${full}`, full.length);
    return { kind: 'literal', code };
  }
  if (char === 'N') return { kind: 'literal', code: readNamedCharacter(source) };
  if (OCTDIGITS.includes(char)) {
    const full = escape + source.getWhile(2, OCTDIGITS);
    const code = Number.parseInt(full.slice(1), 8);
    if (code > 0o377) {
      throw source.error(
        `octal escape value ${full} outside of range 0-0o377`,
        full.length
      );
    }
    return { kind: 'literal', code };
  }
  if (DIGITS.includes(char)) throw source.error(`bad escape ${escape}`, escape.length);
  if ([...escape].length === 2) {
    if (ASCIILETTERS.includes(char)) {
      throw source.error(`bad escape ${escape}`, escape.length);
    }
    return { kind: 'literal', code: escape.codePointAt(1) ?? 0 };
  }
  throw source.error(`bad escape ${escape}`, escape.length);
};

/** `_escape` — the escapes legal outside `[...]`, where `\b` is an anchor. */
const patternEscape = (source: Tokenizer, escape: string, state: ParserState): Escape => {
  const at = AT_ESCAPES.get(escape);
  if (at !== undefined) return { kind: 'at', at };
  const category = CATEGORY_ESCAPES.get(escape);
  if (category !== undefined) return { kind: 'item', item: { kind: 'category', category } };
  const simple = SIMPLE_ESCAPES.get(escape);
  if (simple !== undefined) return { kind: 'item', item: { kind: 'literal', code: simple } };

  const char = escape.slice(1, 2);
  if (char === 'x' || char === 'u' || char === 'U' || char === 'N') {
    return { kind: 'item', item: classEscape(source, escape) };
  }
  if (char === '0') {
    const full = escape + source.getWhile(2, OCTDIGITS);
    return { kind: 'item', item: { kind: 'literal', code: Number.parseInt(full.slice(1), 8) } };
  }
  if (DIGITS.includes(char)) {
    let full = escape;
    if (source.next !== null && DIGITS.includes(source.next)) {
      full += source.get() ?? '';
      const second = full[1] ?? '';
      const third = full[2] ?? '';
      if (
        OCTDIGITS.includes(second) &&
        OCTDIGITS.includes(third) &&
        source.next !== null &&
        OCTDIGITS.includes(source.next)
      ) {
        full += source.get() ?? '';
        const code = Number.parseInt(full.slice(1), 8);
        if (code > 0o377) {
          throw source.error(
            `octal escape value ${full} outside of range 0-0o377`,
            full.length
          );
        }
        return { kind: 'item', item: { kind: 'literal', code } };
      }
    }
    const group = Number.parseInt(full.slice(1), 10);
    if (group < state.groups) {
      if (!state.checkGroup(group)) {
        throw source.error('cannot refer to an open group', full.length);
      }
      state.checkLookbehindGroup(group, source);
      return { kind: 'ref', group };
    }
    throw source.error(`invalid group reference ${group}`, full.length - 1);
  }
  if ([...escape].length === 2) {
    if (ASCIILETTERS.includes(char)) {
      throw source.error(`bad escape ${escape}`, escape.length);
    }
    return { kind: 'item', item: { kind: 'literal', code: escape.codePointAt(1) ?? 0 } };
  }
  throw source.error(`bad escape ${escape}`, escape.length);
};

// ---------------------------------------------------------------------------
// The recursive descent — `_parse_sub`, `_parse`, `_parse_flags`
// ---------------------------------------------------------------------------

const parseSub = (
  source: Tokenizer,
  state: ParserState,
  verbose: boolean,
  nested: number
): Sub => {
  const items: Sub[] = [];
  let currentVerbose = verbose;
  for (;;) {
    items.push(parseOne(source, state, currentVerbose, nested + 1, nested === 0 && items.length === 0));
    if (!source.match('|')) break;
    if (nested === 0) currentVerbose = (state.flags & PY_VERBOSE) !== 0;
  }
  const only = items[0];
  if (items.length === 1 && only !== undefined) return only;
  return [{ kind: 'branch', branches: items }];
};

const parseOne = (
  source: Tokenizer,
  state: ParserState,
  verbose: boolean,
  nested: number,
  first: boolean
): Sub => {
  const nodes: Sub = [];
  let currentVerbose = verbose;

  for (;;) {
    const peeked = source.next;
    if (peeked === null) break;
    if (peeked === '|' || peeked === ')') break;
    source.get();
    const this_ = peeked;

    if (currentVerbose) {
      if (WHITESPACE.includes(this_)) continue;
      if (this_ === '#') {
        for (;;) {
          const skipped = source.get();
          if (skipped === null || skipped === '\n') break;
        }
        continue;
      }
    }

    if (this_.startsWith('\\')) {
      const escape = patternEscape(source, this_, state);
      if (escape.kind === 'at') nodes.push({ kind: 'at', at: escape.at });
      else if (escape.kind === 'ref') nodes.push({ kind: 'ref', group: escape.group });
      else if (escape.item.kind === 'literal') {
        nodes.push({ kind: 'literal', code: escape.item.code });
      } else nodes.push({ kind: 'in', negate: false, items: [escape.item] });
      continue;
    }

    if (!SPECIAL_CHARS.includes(this_)) {
      nodes.push({ kind: 'literal', code: this_.codePointAt(0) ?? 0 });
      continue;
    }

    if (this_ === '[') {
      const here = source.tell() - 1;
      const items: SetItem[] = [];
      const negate = source.match('^');
      for (;;) {
        const token = source.get();
        if (token === null) {
          throw source.error('unterminated character set', source.tell() - here);
        }
        if (token === ']' && items.length > 0) break;
        const code1: SetItem = token.startsWith('\\')
          ? classEscape(source, token)
          : { kind: 'literal', code: token.codePointAt(0) ?? 0 };
        if (source.match('-')) {
          const that = source.get();
          if (that === null) {
            throw source.error('unterminated character set', source.tell() - here);
          }
          if (that === ']') {
            items.push(code1);
            items.push({ kind: 'literal', code: 0x2d });
            break;
          }
          const code2: SetItem = that.startsWith('\\')
            ? classEscape(source, that)
            : { kind: 'literal', code: that.codePointAt(0) ?? 0 };
          if (code1.kind !== 'literal' || code2.kind !== 'literal') {
            throw source.error(
              `bad character range ${token}-${that}`,
              [...token].length + 1 + [...that].length
            );
          }
          if (code2.code < code1.code) {
            throw source.error(
              `bad character range ${token}-${that}`,
              [...token].length + 1 + [...that].length
            );
          }
          items.push({ kind: 'range', low: code1.code, high: code2.code });
        } else {
          items.push(code1);
        }
      }
      nodes.push({ kind: 'in', negate, items });
      continue;
    }

    if (REPEAT_CHARS.includes(this_)) {
      const here = source.tell();
      let min = 0;
      let max = MAXREPEAT;
      if (this_ === '?') {
        min = 0;
        max = 1;
      } else if (this_ === '*') {
        min = 0;
        max = MAXREPEAT;
      } else if (this_ === '+') {
        min = 1;
        max = MAXREPEAT;
      } else {
        if (source.next === '}') {
          nodes.push({ kind: 'literal', code: 0x7b });
          continue;
        }
        let low = '';
        let high = '';
        while (source.next !== null && DIGITS.includes(source.next)) low += source.get() ?? '';
        if (source.match(',')) {
          while (source.next !== null && DIGITS.includes(source.next)) high += source.get() ?? '';
        } else {
          high = low;
        }
        if (!source.match('}')) {
          nodes.push({ kind: 'literal', code: 0x7b });
          source.seek(here);
          continue;
        }
        if (low !== '') {
          min = Number.parseInt(low, 10);
          if (min >= MAXREPEAT) {
            throw new ParseSignal(failure('uncaught', 'the repetition number is too large'));
          }
        }
        if (high !== '') {
          max = Number.parseInt(high, 10);
          if (max >= MAXREPEAT) {
            throw new ParseSignal(failure('uncaught', 'the repetition number is too large'));
          }
          if (max < min) {
            throw source.error('min repeat greater than max repeat', source.tell() - here);
          }
        }
      }

      const last = nodes[nodes.length - 1];
      if (last === undefined || last.kind === 'at') {
        throw source.error('nothing to repeat', source.tell() - here + [...this_].length);
      }
      if (last.kind === 'repeat') {
        throw source.error('multiple repeat', source.tell() - here + [...this_].length);
      }
      let item: Sub = [last];
      if (last.kind === 'group' && last.group === null && !last.addFlags && !last.delFlags) {
        item = last.body;
      }
      const mode: RepeatMode = source.match('?')
        ? 'lazy'
        : source.match('+')
          ? 'possessive'
          : 'greedy';
      nodes[nodes.length - 1] = { kind: 'repeat', min, max, mode, body: item };
      continue;
    }

    if (this_ === '.') {
      nodes.push({ kind: 'any' });
      continue;
    }

    if (this_ === '(') {
      const start = source.tell() - 1;
      let capture = true;
      let atomic = false;
      let name: string | null = null;
      let addFlags = 0;
      let delFlags = 0;

      if (source.match('?')) {
        let char = source.get();
        if (char === null) throw source.error('unexpected end of pattern');

        if (char === 'P') {
          if (source.match('<')) {
            name = source.getUntil('>', 'group name');
            source.checkGroupName(name, 1);
          } else if (source.match('=')) {
            const backref = source.getUntil(')', 'group name');
            source.checkGroupName(backref, 1);
            const gid = state.groupdict.get(backref);
            if (gid === undefined) {
              throw source.error(
                `unknown group name ${pyRepr(backref)}`,
                [...backref].length + 1
              );
            }
            if (!state.checkGroup(gid)) {
              throw source.error('cannot refer to an open group', [...backref].length + 1);
            }
            state.checkLookbehindGroup(gid, source);
            nodes.push({ kind: 'ref', group: gid });
            continue;
          } else {
            const extension = source.get();
            if (extension === null) throw source.error('unexpected end of pattern');
            throw source.error(
              `unknown extension ?P${extension}`,
              [...extension].length + 2
            );
          }
        } else if (char === ':') {
          capture = false;
        } else if (char === '#') {
          for (;;) {
            if (source.next === null) {
              throw source.error('missing ), unterminated comment', source.tell() - start);
            }
            if (source.get() === ')') break;
          }
          continue;
        } else if (char === '=' || char === '!' || char === '<') {
          let direction: 1 | -1 = 1;
          let outerLookbehind: number | null = null;
          if (char === '<') {
            const kind = source.get();
            if (kind === null) throw source.error('unexpected end of pattern');
            if (kind !== '=' && kind !== '!') {
              throw source.error(`unknown extension ?<${kind}`, [...kind].length + 2);
            }
            char = kind;
            direction = -1;
            outerLookbehind = state.lookbehindgroups;
            if (outerLookbehind === null) state.lookbehindgroups = state.groups;
          }
          const body = parseSub(source, state, currentVerbose, nested + 1);
          if (direction < 0 && outerLookbehind === null) state.lookbehindgroups = null;
          if (!source.match(')')) {
            throw source.error('missing ), unterminated subpattern', source.tell() - start);
          }
          if (direction < 0) {
            const [low, high] = getWidth(body, state);
            if (low > MAXCODE) {
              throw new ParseSignal(failure('error', 'looks too much behind'));
            }
            if (low !== high) {
              throw new ParseSignal(
                failure('error', 'look-behind requires fixed-width pattern')
              );
            }
          }
          if (char === '=') nodes.push({ kind: 'assert', direction, negate: false, body });
          else if (body.length > 0) {
            nodes.push({ kind: 'assert', direction, negate: true, body });
          } else nodes.push({ kind: 'failure' });
          continue;
        } else if (char === '(') {
          const condname = source.getUntil(')', 'group name');
          let condgroup: number;
          if (!/^[0-9]+$/.test(condname)) {
            source.checkGroupName(condname, 1);
            const known = state.groupdict.get(condname);
            if (known === undefined) {
              throw source.error(
                `unknown group name ${pyRepr(condname)}`,
                [...condname].length + 1
              );
            }
            condgroup = known;
          } else {
            condgroup = Number.parseInt(condname, 10);
            if (condgroup === 0) {
              throw source.error('bad group number', [...condname].length + 1);
            }
            if (condgroup >= MAXGROUPS) {
              throw source.error(
                `invalid group reference ${condgroup}`,
                [...condname].length + 1
              );
            }
            if (!state.grouprefpos.has(condgroup)) {
              state.grouprefpos.set(condgroup, source.tell() - [...condname].length - 1);
            }
          }
          state.checkLookbehindGroup(condgroup, source);
          const yes = parseOne(source, state, currentVerbose, nested + 1, false);
          let no: Sub | null = null;
          if (source.match('|')) {
            no = parseOne(source, state, currentVerbose, nested + 1, false);
            if (source.next === '|') {
              throw source.error('conditional backref with more than two branches');
            }
          }
          if (!source.match(')')) {
            throw source.error('missing ), unterminated subpattern', source.tell() - start);
          }
          nodes.push({ kind: 'conditional', group: condgroup, yes, no });
          continue;
        } else if (char === '>') {
          capture = false;
          atomic = true;
        } else if (FLAG_LETTERS.has(char) || char === '-') {
          const parsed = parseFlags(source, state, char);
          if (parsed === null) {
            if (!first || nodes.length > 0) {
              throw source.error(
                'global flags not at the start of the expression',
                source.tell() - start
              );
            }
            currentVerbose = (state.flags & PY_VERBOSE) !== 0;
            continue;
          }
          addFlags = parsed[0];
          delFlags = parsed[1];
          capture = false;
        } else {
          throw source.error(`unknown extension ?${char}`, [...char].length + 1);
        }
      }

      const group = capture ? state.openGroup(source, name) : null;
      const subVerbose =
        (currentVerbose || (addFlags & PY_VERBOSE) !== 0) && (delFlags & PY_VERBOSE) === 0;
      const body = parseSub(source, state, subVerbose, nested + 1);
      if (!source.match(')')) {
        throw source.error('missing ), unterminated subpattern', source.tell() - start);
      }
      if (group !== null) state.closeGroup(group, body);
      if (atomic) nodes.push({ kind: 'atomic', body });
      else nodes.push({ kind: 'group', group, addFlags, delFlags, body });
      continue;
    }

    if (this_ === '^') {
      nodes.push({ kind: 'at', at: 'beginning' });
      continue;
    }
    if (this_ === '$') {
      nodes.push({ kind: 'at', at: 'end' });
      continue;
    }
  }

  return nodes;
};

/** `_parse_flags` — returns `null` for a global `(?…)` switch. */
const parseFlags = (
  source: Tokenizer,
  state: ParserState,
  initial: string
): readonly [number, number] | null => {
  let char: string | null = initial;
  let addFlags = 0;
  let delFlags = 0;

  if (char !== '-') {
    for (;;) {
      const flag = FLAG_LETTERS.get(char ?? '');
      if (flag === undefined) break;
      if (char === 'L') {
        throw source.error("bad inline flags: cannot use 'L' flag with a str pattern");
      }
      addFlags |= flag;
      if ((flag & TYPE_FLAGS) !== 0 && (addFlags & TYPE_FLAGS) !== flag) {
        throw source.error("bad inline flags: flags 'a', 'u' and 'L' are incompatible");
      }
      char = source.get();
      if (char === null) throw source.error('missing -, : or )');
      if (char === ')' || char === '-' || char === ':') break;
      if (!FLAG_LETTERS.has(char)) {
        throw source.error(
          isAlpha(char) ? 'unknown flag' : 'missing -, : or )',
          [...char].length
        );
      }
    }
  }

  if (char === ')') {
    state.flags |= addFlags;
    return null;
  }
  if ((addFlags & GLOBAL_FLAGS) !== 0) {
    throw source.error('bad inline flags: cannot turn on global flag', 1);
  }
  if (char === '-') {
    char = source.get();
    if (char === null) throw source.error('missing flag');
    if (!FLAG_LETTERS.has(char)) {
      throw source.error(isAlpha(char) ? 'unknown flag' : 'missing flag', [...char].length);
    }
    for (;;) {
      const flag = FLAG_LETTERS.get(char ?? '');
      if (flag === undefined) break;
      if ((flag & TYPE_FLAGS) !== 0) {
        throw source.error("bad inline flags: cannot turn off flags 'a', 'u' and 'L'");
      }
      delFlags |= flag;
      char = source.get();
      if (char === null) throw source.error('missing :');
      if (char === ':') break;
      if (!FLAG_LETTERS.has(char)) {
        throw source.error(isAlpha(char) ? 'unknown flag' : 'missing :', [...char].length);
      }
    }
  }
  if ((delFlags & GLOBAL_FLAGS) !== 0) {
    throw source.error('bad inline flags: cannot turn off global flag', 1);
  }
  if ((addFlags & delFlags) !== 0) {
    throw source.error('bad inline flags: flag turned on and off', 1);
  }
  return [addFlags, delFlags];
};

const isAlpha = (char: string): boolean => /^\p{Alphabetic}$/u.test(char);

// ---------------------------------------------------------------------------
// Emission — the AST as a JavaScript regex source
// ---------------------------------------------------------------------------

/**
 * Every code point matcher is emitted in UTF-16, and the compiled regex carries
 * no flags at all.
 *
 * The obvious spelling is the `u` flag, which makes JavaScript step by code
 * point exactly as `re` steps over a `str`.  JavaScriptCore miscompiles it:
 * once a class that spans the astral planes has consumed an astral character at
 * a non-zero offset, the rest of the pattern fails.  `/.$/u` — the port's `.`
 * and `$` — answers `false` for `x𝕏` under Bun and `true` under V8 and CPython,
 * and no arrangement of the class or the anchor avoids it.
 *
 * So astral ranges are written out as surrogate pairs, which is what the `u`
 * flag would have done internally, and the whole pattern is prefixed with
 * `(?<![\uD800-\uDBFF])` so a search can never begin between the halves of a
 * pair.  Every consuming atom then spans whole code points, so every interior
 * position is a code point boundary too, and `\b` and the lookarounds see the
 * same positions `re` sees.  The surrogate block itself is excluded from the
 * BMP half, so a lone surrogate — which well-formed mirror text cannot contain
 * — never stands in for a character.
 */
const LEAD_LOW = 0xd800;
const LEAD_HIGH = 0xdbff;
const TRAIL_LOW = 0xdc00;
const TRAIL_HIGH = 0xdfff;
const ASTRAL_LOW = 0x10000;

const escapeCode = (code: number): string =>
  `\\u${code.toString(16).padStart(4, '0')}`;

const classBody = (ranges: ReadonlyArray<CodeRange>): string =>
  ranges
    .map(([low, high]) =>
      low === high ? escapeCode(low) : `${escapeCode(low)}-${escapeCode(high)}`
    )
    .join('');

const leadOf = (code: number): number => LEAD_LOW + ((code - ASTRAL_LOW) >> 10);
const trailOf = (code: number): number => TRAIL_LOW + ((code - ASTRAL_LOW) & 0x3ff);

/** One `[lead][trail]` alternative: a lead range paired with one trail range. */
interface SurrogatePair {
  leadLow: number;
  leadHigh: number;
  readonly trailLow: number;
  readonly trailHigh: number;
}

const surrogatePairs = (ranges: ReadonlyArray<CodeRange>): ReadonlyArray<SurrogatePair> => {
  const pairs: SurrogatePair[] = [];
  for (const [rangeLow, rangeHigh] of ranges) {
    let start = Math.max(rangeLow, ASTRAL_LOW);
    while (start <= rangeHigh) {
      const lead = leadOf(start);
      const blockEnd = ASTRAL_LOW + ((lead - LEAD_LOW + 1) << 10) - 1;
      const end = Math.min(rangeHigh, blockEnd);
      const trailLow = trailOf(start);
      const trailHigh = trailOf(end);
      const previous = pairs[pairs.length - 1];
      if (
        previous !== undefined &&
        previous.trailLow === trailLow &&
        previous.trailHigh === trailHigh &&
        previous.leadHigh + 1 === lead
      ) {
        previous.leadHigh = lead;
      } else {
        pairs.push({ leadLow: lead, leadHigh: lead, trailLow, trailHigh });
      }
      start = end + 1;
    }
  }
  return pairs;
};

const pairSource = (pair: SurrogatePair): string => {
  const lead =
    pair.leadLow === pair.leadHigh
      ? escapeCode(pair.leadLow)
      : `[${escapeCode(pair.leadLow)}-${escapeCode(pair.leadHigh)}]`;
  const trail =
    pair.trailLow === pair.trailHigh
      ? escapeCode(pair.trailLow)
      : `[${escapeCode(pair.trailLow)}-${escapeCode(pair.trailHigh)}]`;
  return `${lead}${trail}`;
};

/** Nothing matches: an empty alternation would be a syntax error. */
const MATCHES_NOTHING = '[^\\s\\S]';

/** The BMP half and the astral half one set of code points splits into. */
interface RangeParts {
  readonly bmp: ReadonlyArray<CodeRange>;
  readonly pairs: ReadonlyArray<SurrogatePair>;
}

const splitRanges = (ranges: ReadonlyArray<CodeRange>): RangeParts => {
  const bmp: Array<CodeRange> = [];
  for (const [low, high] of ranges) {
    if (low > 0xffff) continue;
    const clippedHigh = Math.min(high, 0xffff);
    if (low <= LEAD_HIGH && clippedHigh >= LEAD_LOW) {
      if (low < LEAD_LOW) bmp.push([low, LEAD_LOW - 1]);
      if (clippedHigh > TRAIL_HIGH) bmp.push([TRAIL_HIGH + 1, clippedHigh]);
      continue;
    }
    if (low <= TRAIL_HIGH && clippedHigh >= TRAIL_LOW) {
      if (clippedHigh > TRAIL_HIGH) bmp.push([TRAIL_HIGH + 1, clippedHigh]);
      continue;
    }
    bmp.push([low, clippedHigh]);
  }
  return { bmp, pairs: surrogatePairs(ranges) };
};

/** Join alternatives without wrapping a single one in a needless group. */
const anyOf = (alternatives: ReadonlyArray<string>): string => {
  const only = alternatives[0];
  if (only === undefined) return MATCHES_NOTHING;
  return alternatives.length === 1 ? only : `(?:${alternatives.join('|')})`;
};

/**
 * A set of code points as a matcher — a bare class when it is all BMP, an
 * alternation of the class and the surrogate pairs when it reaches past it.
 */
const rangesMatcher = (ranges: ReadonlyArray<CodeRange>): string => {
  const { bmp, pairs } = splitRanges(ranges);
  return anyOf([...(bmp.length > 0 ? [`[${classBody(bmp)}]`] : []), ...pairs.map(pairSource)]);
};

const LEAD_CLASS = `[${escapeCode(LEAD_LOW)}-${escapeCode(LEAD_HIGH)}]`;
const TRAIL_CLASS = `[${escapeCode(TRAIL_LOW)}-${escapeCode(TRAIL_HIGH)}]`;

/**
 * "A code point from `ranges` sits immediately behind / ahead of here", as a
 * zero-width assertion that costs **one** character-class test to reject.
 *
 * The obvious spelling is `(?<=${rangesMatcher(ranges)})`, and it is what this
 * emitted for `\b` until it was measured: `rangesMatcher(UNICODE_WORD_RANGES)`
 * is a 50 KB alternation of one BMP class and ~230 surrogate-pair branches, so
 * a lookaround over it makes JavaScriptCore try every branch at every position
 * where the character is not a word character — which is most positions, in
 * most subjects.  A leading `\b` tests every offset in the line, so
 * `show --grep '\bu1\b' --regex` spent 2.2 s on a workspace CPython answers in
 * 0.15 s and tripped `V2_SHOW_GREP_BUDGET_S`; a *trailing* `\b` was fine
 * because the preceding atom had already narrowed where it is tested.
 *
 * The astral branches are therefore guarded by the one thing every one of them
 * begins (or, looking backwards, ends) with: a surrogate half.  Rejecting a
 * non-word ASCII character now costs the BMP class plus that guard rather than
 * the whole alternation, and the answer is unchanged — an astral code point is
 * a lead followed by a trail, so the guard can never exclude a pair the
 * alternation would have matched.
 */
const rangesAssertion = (
  ranges: ReadonlyArray<CodeRange>,
  direction: 'behind' | 'ahead'
): string => {
  const { bmp, pairs } = splitRanges(ranges);
  const behind = direction === 'behind';
  const look = (source: string): string => (behind ? `(?<=${source})` : `(?=${source})`);
  const alternatives: Array<string> = [];
  if (bmp.length > 0) alternatives.push(look(`[${classBody(bmp)}]`));
  if (pairs.length > 0) {
    const guard = behind ? `(?<=${TRAIL_CLASS})` : `(?=${LEAD_CLASS})`;
    alternatives.push(`${guard}${look(anyOf(pairs.map(pairSource)))}`);
  }
  // An empty set is behind and ahead of nothing; `(?!)` never matches.
  return alternatives.length === 0 ? '(?!)' : anyOf(alternatives);
};

/** No search may begin between the halves of a surrogate pair. */
const CODE_POINT_START = '(?<![\\ud800-\\udbff])';

const coversEverything = (ranges: ReadonlyArray<CodeRange>): boolean => {
  const only = ranges[0];
  return ranges.length === 1 && only !== undefined && only[0] === 0 && only[1] === MAX_CODE_POINT;
};

const complement = (ranges: ReadonlyArray<CodeRange>): ReadonlyArray<CodeRange> => {
  const out: Array<CodeRange> = [];
  let next = 0;
  for (const [low, high] of ranges) {
    if (low > next) out.push([next, low - 1]);
    next = high + 1;
  }
  if (next <= MAX_CODE_POINT) out.push([next, MAX_CODE_POINT]);
  return out;
};

const ASCII_WORD_RANGES: ReadonlyArray<CodeRange> = [
  [0x30, 0x39],
  [0x41, 0x5a],
  [0x5f, 0x5f],
  [0x61, 0x7a],
];
const ASCII_SPACE_RANGES: ReadonlyArray<CodeRange> = [
  [0x09, 0x0d],
  [0x20, 0x20],
];
const ASCII_DIGIT_RANGES: ReadonlyArray<CodeRange> = [[0x30, 0x39]];

/**
 * `_compiler._combine_flags` — a scoped `(?a:…)` or `(?u:…)` *replaces* the
 * enclosing character-set flag rather than joining it, so `(?a)…(?u:\w)` reads
 * the Unicode tables and `(?u)…(?a:\w)` reads the ASCII ones.  Joining instead
 * leaves both bits set and answers whichever the reader happens to test first.
 */
const combineFlags = (flags: number, addFlags: number, delFlags: number): number => {
  const base = (addFlags & TYPE_FLAGS) !== 0 ? flags & ~TYPE_FLAGS : flags;
  return (base | addFlags) & ~delFlags;
};

/** Effective flags decide which of CPython's two table sets a category reads. */
const isAsciiMode = (flags: number): boolean => (flags & PY_ASCII) !== 0;

const wordRanges = (flags: number): ReadonlyArray<CodeRange> =>
  isAsciiMode(flags) ? ASCII_WORD_RANGES : UNICODE_WORD_RANGES;

const categoryRanges = (
  category: CategoryKind,
  flags: number
): ReadonlyArray<CodeRange> => {
  const ascii = isAsciiMode(flags);
  switch (category) {
    case 'digit':
      return ascii ? ASCII_DIGIT_RANGES : UNICODE_DIGIT_RANGES;
    case 'notDigit':
      return complement(ascii ? ASCII_DIGIT_RANGES : UNICODE_DIGIT_RANGES);
    case 'space':
      return ascii ? ASCII_SPACE_RANGES : UNICODE_SPACE_RANGES;
    case 'notSpace':
      return complement(ascii ? ASCII_SPACE_RANGES : UNICODE_SPACE_RANGES);
    case 'word':
      return ascii ? ASCII_WORD_RANGES : UNICODE_WORD_RANGES;
    case 'notWord':
      return complement(ascii ? ASCII_WORD_RANGES : UNICODE_WORD_RANGES);
  }
};

const caseClassOf = (code: number, flags: number): ReadonlyArray<number> =>
  isAsciiMode(flags) ? asciiCaseClass(code) : unicodeCaseClass(code);

const casedCodePoints = (flags: number): ReadonlyArray<number> =>
  isAsciiMode(flags) ? ASCII_CASED_CODE_POINTS : CASED_CODE_POINTS;

const ignoring = (flags: number): boolean =>
  (flags & PY_IGNORECASE) !== 0 && (flags & PY_LOCALE) === 0;

const normalize = (ranges: ReadonlyArray<CodeRange>): ReadonlyArray<CodeRange> => {
  const sorted = [...ranges].sort((left, right) => left[0] - right[0] || left[1] - right[1]);
  const merged: Array<[number, number]> = [];
  for (const [low, high] of sorted) {
    const last = merged[merged.length - 1];
    if (last !== undefined && low <= last[1] + 1) last[1] = Math.max(last[1], high);
    else merged.push([low, high]);
  }
  return merged;
};

/**
 * The code points a set item stands for.
 *
 * Under `IGNORECASE` CPython lowers the subject and tests it against the set
 * lowered the same way, which is exactly "the subject's equivalence class meets
 * the set" — so expanding each literal and each cased code point inside a range
 * into its class reproduces it without the `i` flag, and keeps `(?-i:…)`
 * meaningful.  Categories are left alone: lowering maps word to word, space to
 * space and digit to digit, so it can never change a category's answer.
 */
const setItemRanges = (item: SetItem, flags: number): ReadonlyArray<CodeRange> => {
  if (item.kind === 'category') return categoryRanges(item.category, flags);
  if (item.kind === 'literal') {
    const codes = ignoring(flags) ? caseClassOf(item.code, flags) : [item.code];
    return codes.map((code) => [code, code] as const);
  }
  const base: Array<CodeRange> = [[item.low, item.high]];
  if (!ignoring(flags)) return base;
  for (const code of casedCodePoints(flags)) {
    if (code < item.low || code > item.high) continue;
    for (const relative of caseClassOf(code, flags)) {
      if (relative < item.low || relative > item.high) base.push([relative, relative]);
    }
  }
  return base;
};

/**
 * A character class, always spelled as a *positive* set of ranges.
 *
 * A negated class is complemented here rather than handed to JavaScript as
 * `[^…]`, because JavaScriptCore miscompiles a negated class in `u` mode: once
 * `[^\n]` has consumed an astral character at a non-zero offset, the rest of the
 * pattern fails to match (`/[^\n](?=\n?$)/u.test('x𝕏')` is `false` under Bun and
 * `true` under V8 and CPython).  Complementing costs a longer source string and
 * removes the shape that triggers it; the same reasoning covers `.`, which is a
 * negated class in CPython too.
 */
const classSource = (
  items: ReadonlyArray<SetItem>,
  negate: boolean,
  flags: number
): string => {
  const ranges = normalize(items.flatMap((item) => setItemRanges(item, flags)));
  return rangesMatcher(negate ? complement(ranges) : ranges);
};

const NEWLINE_ONLY: ReadonlyArray<SetItem> = [{ kind: 'literal', code: 0x0a }];
const EVERYTHING: ReadonlyArray<CodeRange> = [[0, MAX_CODE_POINT]];

const anyMatcher = (flags: number): string =>
  (flags & PY_DOTALL) !== 0
    ? rangesMatcher(EVERYTHING)
    : classSource(NEWLINE_ONLY, true, flags & ~PY_IGNORECASE);

/**
 * `\b` / `\B` — a word character on exactly one side, or on neither/both.
 *
 * `behind`/`ahead` are already zero-width, so `(?<!W)` is `(?!${behind})` and
 * `(?!W)` is `(?!${ahead})`: negating an assertion that asserts nothing is
 * negating the assertion itself.
 */
const boundaryMatcher = (flags: number, negate: boolean): string => {
  const ranges = wordRanges(flags);
  const behind = rangesAssertion(ranges, 'behind');
  const ahead = rangesAssertion(ranges, 'ahead');
  return negate
    ? `(?:${behind}${ahead}|(?!${behind})(?!${ahead}))`
    : `(?:${behind}(?!${ahead})|(?!${behind})${ahead})`;
};

/**
 * The emitted regex never carries `m`, so JavaScript's own `^` and `$` *are*
 * `\A` and `\Z` — no lookaround needed.  That is not only shorter: the obvious
 * spelling `(?<![\s\S])` is miscompiled by JavaScriptCore, which fails the
 * lookbehind when the character before the position is an astral one, so an
 * anchor written that way would silently lose matches on any line holding an
 * emoji.  `[^]` is the "any character" class that survives the same test.
 */
const START_OF_STRING = '^';
const END_OF_STRING = '$';

const atMatcher = (at: AtKind, flags: number): string => {
  const multiline = (flags & PY_MULTILINE) !== 0;
  switch (at) {
    case 'beginningString':
      return START_OF_STRING;
    case 'endString':
      return END_OF_STRING;
    case 'beginning':
      return multiline ? `(?:${START_OF_STRING}|(?<=\\n))` : START_OF_STRING;
    case 'end':
      return multiline ? `(?=\\n|${END_OF_STRING})` : `(?=\\n?${END_OF_STRING})`;
    case 'boundary':
      return boundaryMatcher(flags, false);
    case 'nonBoundary':
      return boundaryMatcher(flags, true);
  }
};

const quantifier = (min: number, max: number): string => {
  if (min === 0 && max === 1) return '?';
  if (min === 0 && max === MAXREPEAT) return '*';
  if (min === 1 && max === MAXREPEAT) return '+';
  if (max === MAXREPEAT) return `{${min},}`;
  if (min === max) return `{${min}}`;
  return `{${min},${max}}`;
};

const isCased = (code: number, flags: number): boolean =>
  isAsciiMode(flags) ? asciiIsCased(code) : unicodeIsCased(code);

/** `_get_charset_prefix`'s guard: a cased member disqualifies the whole set. */
const prefilterIsUncased = (items: ReadonlyArray<SetItem>, flags: number): boolean =>
  items.every((item) => {
    if (item.kind === 'category') return true;
    if (item.kind === 'literal') return !isCased(item.code, flags);
    if (item.high > 0xffff) return false;
    for (let code = item.low; code <= item.high; code += 1) {
      if (isCased(code, flags)) return false;
    }
    return true;
  });

/**
 * `_compile_info`'s `SRE_INFO_CHARSET` — the one place CPython's *optimiser* is
 * observable, and therefore part of the spec.
 *
 * When a pattern cannot match empty and has no literal prefix, CPython builds a
 * character set from the pattern's first atom and uses it to skip start
 * positions.  `_get_charset_prefix` descends through leading groups combining
 * their flags, but `_compile_info` then emits that set with the **outer** flags
 * — so in `(?a)(?u:\w)` the body reads the Unicode tables and the prefilter
 * reads the ASCII ones, and `ﬁ` never matches even though the body accepts it.
 * That is CPython's answer, so it is this one's: the prefilter is emitted as a
 * leading lookahead whenever the two flag sets disagree.
 *
 * Only a category can disagree.  Under `IGNORECASE` the guard above rejects any
 * set holding a cased literal or range, and what survives means the same thing
 * in both table sets — so nothing else needs the prefilter to be reproduced.
 */
const charsetPrefilter = (nodes: Sub, flags: number, state: ParserState): string => {
  const [low] = getWidth(nodes, state);
  if (low === 0) return '';
  let current: Sub = nodes;
  let inner = flags;
  let descended = false;
  for (;;) {
    const head = current[0];
    if (head === undefined) return '';
    if (head.kind === 'group') {
      inner = combineFlags(inner, head.addFlags, head.delFlags);
      current = head.body;
      descended = true;
      continue;
    }
    if (!descended || head.kind !== 'in') return '';
    if ((inner & TYPE_FLAGS) === (flags & TYPE_FLAGS)) return '';
    if (!prefilterIsUncased(head.items, inner)) return '';
    // `_compile_charset` does no case expansion; the guard has already ruled
    // out every member for which it would have mattered.
    const literal = flags & ~PY_IGNORECASE;
    const ranges = normalize(head.items.flatMap((item) => setItemRanges(item, literal)));
    const effective = head.negate ? complement(ranges) : ranges;
    if (effective.length === 0 || coversEverything(effective)) return '';
    return `(?=${rangesMatcher(effective)})`;
  }
};

/**
 * Emitter state: a counter for the atomic-group helpers, and the set of groups
 * that are certain to have taken part by the time emission reaches this point.
 *
 * The set exists because a backreference to a group that did not participate
 * *fails* in `re` and matches the **empty string** in JavaScript — so
 * `(a)??\1` finds nothing under CPython and matches everywhere under a naive
 * translation.  Where participation is certain the backreference is emitted and
 * the two agree; where it is not, the pattern is refused rather than answered
 * wrongly.  `(a)\1`, `(a)+\1` and `((a)\2)` are all certain; `(a)?\1` and
 * `(?:(a)|b)\1` are not.
 */
interface Emitter {
  helpers: number;
  guaranteed: Set<number>;
}

const emitScoped = (
  nodes: Sub,
  flags: number,
  emitter: Emitter,
  keep: boolean
): { readonly source: string; readonly gained: ReadonlySet<number> } => {
  const scoped: Emitter = { helpers: emitter.helpers, guaranteed: new Set(emitter.guaranteed) };
  const source = emitSub(nodes, flags, scoped);
  emitter.helpers = scoped.helpers;
  const gained = scoped.guaranteed;
  if (keep) for (const group of gained) emitter.guaranteed.add(group);
  return { source, gained };
};

const emitSub = (nodes: Sub, flags: number, emitter: Emitter): string =>
  nodes.map((node) => emitNode(node, flags, emitter)).join('');

const emitNode = (node: Node, flags: number, emitter: Emitter): string => {
  switch (node.kind) {
    case 'literal': {
      const codes = ignoring(flags) ? caseClassOf(node.code, flags) : [node.code];
      const only = codes[0];
      if (codes.length === 1 && only !== undefined) {
        return rangesMatcher([[only, only]]);
      }
      return rangesMatcher(normalize(codes.map((code) => [code, code] as const)));
    }
    case 'any':
      return anyMatcher(flags);
    case 'in':
      return classSource(node.items, node.negate, flags);
    case 'at':
      return atMatcher(node.at, flags);
    case 'branch': {
      const emitted = node.branches.map((branch) => emitScoped(branch, flags, emitter, false));
      const first = emitted[0];
      if (first !== undefined) {
        for (const group of first.gained) {
          if (emitted.every((branch) => branch.gained.has(group))) emitter.guaranteed.add(group);
        }
      }
      return `(?:${emitted.map((branch) => branch.source).join('|')})`;
    }
    case 'group': {
      const inner = combineFlags(flags, node.addFlags, node.delFlags);
      const body = emitSub(node.body, inner, emitter);
      if (node.group === null) return `(?:${body})`;
      emitter.guaranteed.add(node.group);
      return `(?<g${node.group}>${body})`;
    }
    case 'atomic': {
      emitter.helpers += 1;
      const name = `a${emitter.helpers}`;
      return `(?=(?<${name}>${emitSub(node.body, flags, emitter)}))\\k<${name}>`;
    }
    case 'repeat': {
      const inner = emitScoped(node.body, flags, emitter, node.min >= 1);
      const body = `(?:${inner.source})${quantifier(node.min, node.max)}`;
      if (node.mode === 'lazy') return `${body}?`;
      if (node.mode === 'greedy') return body;
      emitter.helpers += 1;
      const name = `a${emitter.helpers}`;
      return `(?=(?<${name}>${body}))\\k<${name}>`;
    }
    case 'ref':
      if (!emitter.guaranteed.has(node.group)) {
        throw new ParseSignal(
          failure(
            'unsupported',
            `group ${node.group} may not have taken part where \\${node.group} refers to it`
          )
        );
      }
      return `\\k<g${node.group}>`;
    case 'assert': {
      const open =
        node.direction === 1 ? (node.negate ? '(?!' : '(?=') : node.negate ? '(?<!' : '(?<=';
      const inner = emitScoped(node.body, flags, emitter, !node.negate);
      return `${open}${inner.source})`;
    }
    case 'failure':
      return '(?!)';
    case 'conditional':
      throw new ParseSignal(
        failure(
          'unsupported',
          'a conditional group reference has no equivalent in this engine'
        )
      );
  }
};

// ---------------------------------------------------------------------------
// The entry point
// ---------------------------------------------------------------------------

/**
 * `re.compile(pattern, flags)` — the same accept/reject decision, the same
 * message when it rejects, and a `RegExp` with `re`'s semantics when it does not.
 *
 * `flags` is the caller's `re` flag word; `_show_grep` passes `re.IGNORECASE`.
 */
export const compilePythonRegex = (
  pattern: string,
  flags: number = 0
): Either.Either<RegExp, PyRegexFailure> => {
  try {
    const source = new Tokenizer(pattern);
    const state = new ParserState();
    state.flags = flags;
    const parsed = parseSub(source, state, (flags & PY_VERBOSE) !== 0, 0);

    // `fix_flags` for a `str` pattern: UNICODE unless ASCII was asked for.
    if ((state.flags & PY_LOCALE) !== 0) {
      return Either.left(failure('uncaught', 'cannot use LOCALE flag with a str pattern'));
    }
    if ((state.flags & PY_ASCII) === 0) state.flags |= PY_UNICODE;
    else if ((state.flags & PY_UNICODE) !== 0) {
      return Either.left(failure('uncaught', 'ASCII and UNICODE flags are incompatible'));
    }

    if (source.next !== null) throw source.error('unbalanced parenthesis');

    for (const [group, position] of state.grouprefpos) {
      if (group >= state.groups) {
        return Either.left(
          failure(
            'error',
            formatPatternError(`invalid group reference ${group}`, [...pattern], position)
          )
        );
      }
    }

    const emitter: Emitter = { helpers: 0, guaranteed: new Set<number>() };
    const emitted = emitSub(parsed, state.flags, emitter);
    const prefilter = charsetPrefilter(parsed, state.flags, state);
    return Either.right(new RegExp(CODE_POINT_START + prefilter + emitted, ''));
  } catch (caught) {
    if (caught instanceof ParseSignal) return Either.left(caught.failure);
    // Everything that is not a `ParseSignal` comes from the two host calls this
    // function makes — `new RegExp` on the emitted source, and the recursive
    // descent through a deeply nested pattern.  Both fail on *size*, not on
    // syntax: a class emitted as explicit code point ranges is ~10 KB and one
    // `\b` is ~50 KB, so `\w` a hundred times over — 200 characters, the most
    // `_show_grep` accepts — emits past JavaScriptCore's ~1 MB regex ceiling
    // and `new RegExp` raises `SyntaxError: regular expression too large`.
    // CPython compiles all of those, so this is a divergence either way; making
    // it a value keeps it a printed refusal with a runnable remedy instead of a
    // stack trace out of the defect channel (NOTES.md §19.3, residue 4).
    return Either.left(
      failure('unsupported', 'the emitted pattern is too large for this engine')
    );
  }
};
