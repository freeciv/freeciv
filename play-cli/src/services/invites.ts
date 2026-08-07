/**
 * The invitation loader — the security-critical half of `join`.
 *
 * Ports `_invite` (client.py:6010-6126).
 *
 * Four properties are the whole point, and every one of them is a refusal:
 *
 * 1. **`.invites` is a real directory inside the workspace.** A symlinked
 *    `.invites` would let anything on the box hand this client a service URL
 *    and a bearer token, so the directory is `lstat`ed and its resolution is
 *    proved to stay inside the workspace root.
 * 2. **An invite file resolves inside `.invites/`.** `--invite ../../elsewhere`
 *    and a symlinked invite both land outside and are refused by path, before
 *    a single byte is read.  "Resolves" means CPython's `Path.resolve()`:
 *    symlinks first, `..` afterwards, never the other way round — see the walk
 *    below, and NOTES §12.7 for what the other order silently accepts.
 * 3. **Mode 0600, `schema_version == 1`, matching `game_id`, an untrimmed-equal
 *    non-empty `join_token`, a string `service_url`.** A credential file that
 *    any other user can read is not a credential.
 * 4. **A CLI/environment token is a complete credential override.** It skips
 *    only the *implicit default* file, never an explicitly configured one, so
 *    a stale local invitation cannot block documented recovery or redirect the
 *    request to its old service URL.
 *
 * Every failure names the exact command that repairs it — `just invite
 * {game_id}` — because the agent reading the refusal cannot run it and has to
 * quote it to the game owner verbatim.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { Effect } from 'effect';
import { PlayerError, playerError } from 'src/errors';
import { serviceUrl } from 'src/services/http';
import { expandUser, type WorkspacePaths } from 'src/services/private-fs';
import { isJsonObject, type JsonObject } from 'src/schema/primitives';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/** The three `join` arguments the loader reads. */
export interface InviteRequest {
  readonly gameId: string;
  /** `--invite`; `PLAY_INVITE` when empty. */
  readonly invite: string;
  /** `--join-token`; `AGENT_EVAL_JOIN_TOKEN` when empty. */
  readonly joinToken: string;
}

export interface Invitation {
  /** The bearer this join POSTs with. Never printed, never logged. */
  readonly token: string;
  /** The one supervisor origin every later request is allowed to reach. */
  readonly base: string;
}

interface LoadedInvite {
  readonly invite: JsonObject;
  readonly token: string;
}

// ---------------------------------------------------------------------------
// The remediation sentences (client.py:6010-6126, verbatim)
// ---------------------------------------------------------------------------

/** "Ask the game owner to run `just invite X` from the repository root, then retry once." */
const remedy = (gameId: string): string =>
  `Ask the game owner to run \`just invite ${gameId}\` from the ` +
  'repository root, then retry once.';

export const INVITE_ROOT_NOT_REAL = '.invites must be a real directory inside play/';
export const INVITE_ESCAPES = 'invite files must stay inside .invites/';

const directoryUnavailable = (gameId: string): string =>
  `the invitation directory is unavailable. ${remedy(gameId)}`;

const configuredMissing = (gameId: string): string =>
  `the configured invitation for ${gameId} does not exist. ${remedy(gameId)}`;

const notPrivate = (gameId: string): string =>
  `the invitation for ${gameId} is not mode 0600. ${remedy(gameId)}`;

const unreadable = (gameId: string): string =>
  `the invitation for ${gameId} is unreadable. ${remedy(gameId)}`;

const unsupportedSchema = (gameId: string): string =>
  `the invitation for ${gameId} has an unsupported schema. ${remedy(gameId)}`;

const differentGame = (gameId: string): string =>
  `the invitation belongs to a different game. ${remedy(gameId)}`;

const invalidToken = (gameId: string): string =>
  `the invitation for ${gameId} has an invalid join token. ${remedy(gameId)}`;

const invalidServiceUrl = (gameId: string): string =>
  `the invitation for ${gameId} has an invalid service URL. ${remedy(gameId)}`;

const noInvitation = (gameId: string): string =>
  `no join invitation for ${gameId}. ${remedy(gameId)}`;

// ---------------------------------------------------------------------------
// Filesystem helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// `str.strip()` — CPython's whitespace class, not JavaScript's
// ---------------------------------------------------------------------------

/**
 * CPython's `str.strip()`.
 *
 * `String.prototype.trim()` is NOT this function, and the two disagree in both
 * directions on characters a hostile or rotten invitation can carry:
 *
 * - Python strips `\x1c`-`\x1f` (the four ASCII separators) and `\x85` (NEL);
 *   JavaScript does not.  `"tok\x1f"` is therefore *untrimmed-equal* to CPython
 *   and refused, while `.trim()` leaves it alone and hands the raw bytes to the
 *   `Authorization` header.
 * - JavaScript strips `﻿` (ZWNBSP); Python does not.  `"tok﻿"` is a
 *   perfectly good CPython token that `.trim()` would call untrimmed.
 *
 * That is not cosmetic on this surface: the same helper decides whether
 * `AGENT_EVAL_JOIN_TOKEN="\x1f"` is a credential override.  CPython says it is
 * blank, so the default invitation is still read and its `service_url` still
 * decides the origin; `.trim()` says it is a token, skips the invitation
 * entirely, and joins the *wrong origin* with a bogus bearer.  Verified against
 * `python3` for all 29 code points Python calls whitespace.
 */
const PY_SPACE_CLASS =
  '\\t\\n\\v\\f\\r\\x1c-\\x1f \\x85\\xa0\\u1680\\u2000-\\u200a\\u2028\\u2029\\u202f\\u205f\\u3000';

const PY_LSTRIP_RE = new RegExp(`^[${PY_SPACE_CLASS}]+`, 'u');
const PY_RSTRIP_RE = new RegExp(`[${PY_SPACE_CLASS}]+$`, 'u');

/** CPython's `str.strip()`. Every `.strip()` in `_invite` goes through it. */
export const pyStrip = (text: string): string =>
  text.replace(PY_LSTRIP_RE, '').replace(PY_RSTRIP_RE, '');

// ---------------------------------------------------------------------------

const isInside = (parent: string, child: string): boolean =>
  child === parent || child.startsWith(parent + path.sep);

const lstatOrNull = (target: string): fs.Stats | null => {
  try {
    return fs.lstatSync(target);
  } catch {
    return null;
  }
};

/** `Path.is_file()` / `Path.stat()` — follows symlinks, on an already-resolved path. */
const statOrNull = (target: string): fs.Stats | null => {
  try {
    return fs.statSync(target);
  } catch {
    return null;
  }
};

const readlinkOrNull = (target: string): string | null => {
  try {
    return fs.readlinkSync(target);
  } catch {
    return null;
  }
};

// ---------------------------------------------------------------------------
// `Path.resolve()` — symlinks first, `..` afterwards
// ---------------------------------------------------------------------------

// CPython's `Path.resolve()` is `os.path.realpath(strict=False)`: it walks the
// path ONE COMPONENT AT A TIME, expanding each symlink as it meets it, so a
// `..` pops a component off the *already resolved* prefix.  With
// `.invites/d -> /elsewhere`, `.invites/d/../x.json` resolves to `/x.json` —
// outside `.invites/` — and `_invite` refuses it.
//
// `path.resolve()` and `path.join()` do the opposite: they collapse `..`
// LEXICALLY, BEFORE any symlink is read, so the same argument becomes
// `.invites/x.json` and the escape refusal never fires for any path whose `..`
// crosses a symlinked component.  That is not cosmetic here — it decides which
// file the client accepts a bearer token from — so `posixpath._joinrealpath` is
// ported rather than approximated (NOTES §12.7).
//
// POSIX only, like every other path in this client.

const SEP = '/';
const TRAILING_SEPARATORS = /\/+$/;

/** `posixpath.split` — the head keeps its separators only when it is all separators. */
const splitPath = (target: string): readonly [string, string] => {
  const cut = target.lastIndexOf(SEP);
  if (cut === -1) return ['', target];
  const head = target.slice(0, cut + 1);
  const stripped = head.replace(TRAILING_SEPARATORS, '');
  return [stripped === '' ? head : stripped, target.slice(cut + 1)];
};

/** `posixpath.join`, two arguments, no normalization. */
const joinPath = (base: string, name: string): string =>
  name.startsWith(SEP)
    ? name
    : base === '' || base.endsWith(SEP)
      ? base + name
      : base + SEP + name;

interface Resolution {
  readonly path: string;
  /** False once a symlink loop was hit — the remainder is appended unresolved. */
  readonly ok: boolean;
}

const joinRealpath = (
  start: string,
  rest: string,
  seen: Map<string, string | null>
): Resolution => {
  let resolved = start;
  let remaining = rest;
  if (remaining.startsWith(SEP)) {
    remaining = remaining.slice(1);
    resolved = SEP;
  }
  while (remaining !== '') {
    const cut = remaining.indexOf(SEP);
    const name = cut === -1 ? remaining : remaining.slice(0, cut);
    remaining = cut === -1 ? '' : remaining.slice(cut + 1);
    if (name === '' || name === '.') continue;
    if (name === '..') {
      // The parent of what has been resolved SO FAR, which is the whole point.
      if (resolved === '') {
        resolved = '..';
      } else {
        const [head, tail] = splitPath(resolved);
        resolved = tail === '..' ? joinPath(joinPath(head, '..'), '..') : head;
      }
      continue;
    }
    const candidate = joinPath(resolved, name);
    const metadata = lstatOrNull(candidate);
    if (metadata === null || !metadata.isSymbolicLink()) {
      resolved = candidate;
      continue;
    }
    const cached = seen.get(candidate);
    if (cached !== undefined) {
      if (cached !== null) {
        resolved = cached;
        continue;
      }
      // Seen but unresolved: a symlink loop.  Non-strict resolution keeps the
      // rest of the path as written rather than raising ELOOP.
      return { path: joinPath(candidate, remaining), ok: false };
    }
    seen.set(candidate, null);
    const target = readlinkOrNull(candidate);
    if (target === null) {
      resolved = candidate;
      continue;
    }
    const inner = joinRealpath(resolved, target, seen);
    resolved = inner.path;
    if (!inner.ok) return { path: joinPath(resolved, remaining), ok: false };
    seen.set(candidate, resolved);
  }
  return { path: resolved, ok: true };
};

/**
 * `Path(target).expanduser().resolve()` — `realpath` then `abspath`, exactly as
 * CPython composes them.  Verified against CPython over a symlink zoo (a
 * directory link, a relative link, a link to `..`, a dangling link, a two-hop
 * loop, `.`/`//`/trailing-slash noise): 25 of 25 paths agree.
 */
const pathResolve = (target: string): string =>
  path.resolve(joinRealpath('', expandUser(target), new Map()).path);

/** `pathlib`'s `/` — joining, *without* `path.join`'s lexical normalization. */
const rawJoin = (base: string, name: string): string =>
  path.isAbsolute(name) ? name : joinPath(base, name);

// ---------------------------------------------------------------------------
// The default-file / configured-file read
// ---------------------------------------------------------------------------

const readInviteFile = (
  workspace: WorkspacePaths,
  gameId: string,
  configured: string,
  explicitToken: string
): Effect.Effect<LoadedInvite, PlayerError> =>
  Effect.suspend(() => {
    const root = pathResolve(workspace.root);
    const inviteRoot = rawJoin(workspace.root, '.invites');
    const rootMetadata = lstatOrNull(inviteRoot);
    if (rootMetadata === null) {
      return Effect.fail(playerError(directoryUnavailable(gameId)));
    }
    const resolvedRoot = pathResolve(inviteRoot);
    if (
      rootMetadata.isSymbolicLink() ||
      !rootMetadata.isDirectory() ||
      !isInside(root, resolvedRoot)
    ) {
      return Effect.fail(playerError(INVITE_ROOT_NOT_REAL));
    }

    const named =
      configured === '' ? rawJoin(inviteRoot, `${gameId}.json`) : expandUser(configured);
    const absolute = path.isAbsolute(named) ? named : rawJoin(workspace.root, named);
    const resolved = pathResolve(absolute);
    if (!isInside(resolvedRoot, resolved)) {
      return Effect.fail(playerError(INVITE_ESCAPES));
    }

    const metadata = statOrNull(resolved);
    if (metadata === null || !metadata.isFile()) {
      // An absent *default* invitation is not yet a failure: an explicit token
      // may still be a complete credential.  An absent *configured* one is.
      return configured === ''
        ? Effect.succeed<LoadedInvite>({ invite: {}, token: '' })
        : Effect.fail(playerError(configuredMissing(gameId)));
    }
    if ((metadata.mode & 0o777) !== 0o600) {
      return Effect.fail(playerError(notPrivate(gameId)));
    }

    const parsed = ((): unknown => {
      try {
        // `Path.read_text(encoding="utf-8")`, and it is STRICT.  Node's
        // `'utf8'` reader silently substitutes U+FFFD for an invalid byte, so
        // an invitation whose `join_token` is the bytes `to\xffk` would decode
        // to `"to�k"`, pass every check, and be sent as a bearer that is
        // not what is on disk.  CPython raises `UnicodeDecodeError`, which
        // `_load_object` turns into the "unreadable" refusal — a credential
        // loader has to fail closed on a malformed credential file.
        //
        // `ignoreBOM: true` means "do not strip a leading U+FEFF": the utf-8
        // codec keeps it, and `json.loads` then rejects it, exactly as
        // `JSON.parse` does here.
        const bytes = fs.readFileSync(resolved);
        const decoded = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true }).decode(bytes);
        return JSON.parse(decoded) as unknown;
      } catch {
        return undefined;
      }
    })();
    if (!isJsonObject(parsed)) {
      return Effect.fail(playerError(unreadable(gameId)));
    }
    if (parsed['schema_version'] !== 1) {
      return Effect.fail(playerError(unsupportedSchema(gameId)));
    }
    if (parsed['game_id'] !== gameId) {
      return Effect.fail(playerError(differentGame(gameId)));
    }
    const stored = parsed['join_token'];
    // With a CLI/environment token in hand the stored one is never used, so a
    // rotted token in an explicitly configured file is not a reason to refuse.
    if (
      explicitToken === '' &&
      (typeof stored !== 'string' || pyStrip(stored) === '' || stored !== pyStrip(stored))
    ) {
      return Effect.fail(playerError(invalidToken(gameId)));
    }
    if (typeof parsed['service_url'] !== 'string') {
      return Effect.fail(playerError(invalidServiceUrl(gameId)));
    }
    return Effect.succeed<LoadedInvite>({
      invite: parsed,
      token: typeof stored === 'string' ? stored : '',
    });
  });

// ---------------------------------------------------------------------------
// _invite
// ---------------------------------------------------------------------------

/** Resolve the bearer token and the supervisor origin this join will use. */
export const loadInvitation = (
  workspace: WorkspacePaths,
  request: InviteRequest,
  environment: Readonly<Record<string, string | undefined>> = process.env
): Effect.Effect<Invitation, PlayerError> =>
  Effect.gen(function* () {
    const gameId = request.gameId;
    const configured = pyStrip(request.invite) || pyStrip(environment['PLAY_INVITE'] ?? '');
    const explicitToken =
      pyStrip(request.joinToken) || pyStrip(environment['AGENT_EVAL_JOIN_TOKEN'] ?? '');
    // A CLI/environment token is a complete credential override.  Ignore only
    // the implicit default file in that case.
    const loadInvite = configured !== '' || explicitToken === '';
    const loaded: LoadedInvite = loadInvite
      ? yield* readInviteFile(workspace, gameId, configured, explicitToken)
      : { invite: {}, token: '' };

    const token = explicitToken !== '' ? explicitToken : loaded.token;
    if (token === '') {
      return yield* Effect.fail(playerError(noInvitation(gameId)));
    }

    const declared = loaded.invite['service_url'];
    const configuredUrl =
      loadInvite && typeof declared === 'string' && declared !== '' ? declared : undefined;
    const base = yield* Effect.catchAll(serviceUrl(configuredUrl, environment), () =>
      Effect.fail(playerError(invalidServiceUrl(gameId)))
    );
    return { token, base };
  });
