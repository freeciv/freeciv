/**
 * The command name the guidance text tells the agent to run.
 *
 * The Python client and its justfile veneer told agents `just wait`,
 * `just legal --actor_id u3 --all` — 225 hardcoded mentions, reproduced
 * verbatim by this port for byte parity.  Workspaces provisioned after the
 * justfile cutover carry no justfile: the same guidance must say
 * `./play wait`.  One environment variable chooses the spelling:
 *
 *   PLAY_PROG=just     parity mode — output is byte-identical to CPython
 *                      (the diff oracle and the golden tests run here)
 *   unset / anything   the value (default `./play`) replaces `just` in every
 *                      command mention at the two output boundaries
 *
 * The rewrite happens at exactly two places — the process stdout/stderr
 * streams ({@link installProgRewrite}, from `bin.ts` only) and the mirror-file
 * writer (`writeMirror` in `src/services/mirror/store.ts`).  Wire payloads,
 * canonical bodies and persisted receipts are never rewritten: a remedy string
 * the server sent is display text by the time it reaches either boundary, and
 * nothing revision-bound flows through them.
 *
 * Only `just <verb>` for the twenty registered verbs is rewritten.  Prose
 * ("just one call"), the word "justfile", and `just --list` survive untouched
 * — the last deliberately: it names a command that no longer exists, and the
 * only string containing it died with the justfile.
 */

/** Every subcommand the CLI registers — the verbs a `just` mention can name. */
const VERBS = [
  'prompt',
  'join',
  'use',
  'next',
  'act',
  'health',
  'turn',
  'start',
  'do',
  'show',
  'state',
  'legal',
  'batch',
  'receipt',
  'retry',
  'wait',
  'monitor',
  'result',
  'help',
  'rules',
] as const;

const MENTION_RE = new RegExp(`\\bjust (${VERBS.join('|')})\\b`, 'g');

export const PROG_ENV = 'PLAY_PROG';

/** The spelling in effect right now — read per call so tests can toggle it. */
export const resolveProg = (): string => {
  const raw = process.env[PROG_ENV]?.trim();
  return raw !== undefined && raw !== '' ? raw : './play';
};

/**
 * Rewrite every `just <verb>` mention to `<prog> <verb>`; identity in parity
 * mode (`prog === 'just'`).
 */
/**
 * The one sentence that names both spellings: true while the justfile lived,
 * backwards after the cutover.  Rewritten wholesale rather than per-verb.
 */
const BOTH_SPELLINGS = 'Every `just X` is also `./play X`';

export const rewriteProgMentions = (text: string, prog = resolveProg()): string =>
  prog === 'just'
    ? text
    : text
        .replace(BOTH_SPELLINGS, `Every command is \`${prog} X\``)
        .replace(MENTION_RE, (_match, verb: string) => `${prog} ${verb}`);

type StreamWrite = typeof process.stdout.write;

const patchStream = (stream: NodeJS.WriteStream): void => {
  const original: StreamWrite = stream.write.bind(stream);
  const patched: StreamWrite = (
    chunk: Uint8Array | string,
    encodingOrCallback?: BufferEncoding | ((error?: Error | null) => void),
    callback?: (error?: Error | null) => void
  ): boolean => {
    const rewritten =
      typeof chunk === 'string'
        ? rewriteProgMentions(chunk)
        : Buffer.from(rewriteProgMentions(Buffer.from(chunk).toString('utf8')), 'utf8');
    // The two overloads of `write` disagree on the second parameter; forward
    // exactly what arrived so neither loses its callback.
    return typeof encodingOrCallback === 'function'
      ? original(rewritten, encodingOrCallback)
      : original(rewritten, encodingOrCallback, callback);
  };
  stream.write = patched;
};

type ConsoleMethod = (...args: ReadonlyArray<unknown>) => void;

const patchConsoleMethod = (name: 'log' | 'error' | 'warn' | 'info'): void => {
  const original: ConsoleMethod = console[name].bind(console);
  console[name] = (...args: ReadonlyArray<unknown>): void =>
    original(...args.map((arg) => (typeof arg === 'string' ? rewriteProgMentions(arg) : arg)));
};

/**
 * Patch process stdout/stderr AND the console methods so every write passes
 * through {@link rewriteProgMentions}.  Both are needed: Bun's `console.log`
 * takes a native fast path that never touches `process.stdout.write`.  Called
 * once from `bin.ts`, before anything can print — in-process tests never see
 * it, so golden assertions stay in parity mode unless a test opts in via the
 * env var.
 */
export const installProgRewrite = (): void => {
  patchStream(process.stdout);
  patchStream(process.stderr);
  patchConsoleMethod('log');
  patchConsoleMethod('error');
  patchConsoleMethod('warn');
  patchConsoleMethod('info');
};
