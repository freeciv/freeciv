/**
 * The invitation loader (`_invite`, client.py:6010-6126).
 *
 * This is the port's only credential-reading surface, so the tests are written
 * as a rejection matrix: one row per way a hostile or rotten invitation can
 * reach the loader, each asserting the *exact* stderr sentence, because those
 * sentences are the agent's only route back to a working game — it cannot run
 * `just invite` itself and has to quote the remedy to the game owner verbatim.
 *
 * Ports `test_invite_is_game_scoped_and_token_is_not_returned_publicly`,
 * `test_missing_or_broken_invite_names_owner_recovery_command`,
 * `test_invite_root_symlink_cannot_escape_player_workspace`,
 * `test_explicit_token_ignores_bad_implicit_invite_and_uses_env_url` and the
 * invite half of `test_session_and_invite_paths_cannot_escape_workspace`.
 */
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import {
  INVITE_ESCAPES,
  INVITE_ROOT_NOT_REAL,
  loadInvitation,
  type InviteRequest,
  type Invitation,
} from 'src/services/invites';
import type { WorkspacePaths } from 'src/services/private-fs';

const GAME_ID = 'game_Hsit9YEuBjKdJPPouFoGVYlk';
const OTHER_ID = 'game_9OtherGameIdentifier000';

const roots: string[] = [];

afterEach(() => {
  while (roots.length > 0) {
    const root = roots.pop();
    if (root !== undefined) fs.rmSync(root, { recursive: true, force: true });
  }
});

interface Bench {
  readonly root: string;
  readonly workspace: WorkspacePaths;
  readonly invites: string;
  readonly write: (name: string, body: unknown, mode?: number) => string;
}

/** A workspace with a real `.invites/` directory, unless `invites` is false. */
const bench = (options: { readonly invites?: boolean } = {}): Bench => {
  const root = fs.realpathSync.native(fs.mkdtempSync(path.join(os.tmpdir(), 'play-cli-u02-')));
  roots.push(root);
  const invites = path.join(root, '.invites');
  if (options.invites !== false) fs.mkdirSync(invites, { mode: 0o700 });
  return {
    root,
    workspace: { root, stateRoot: path.join(root, '.sessions') },
    invites,
    write: (name, body, mode = 0o600) => {
      const target = path.join(invites, name);
      fs.writeFileSync(
        target,
        typeof body === 'string' ? body : JSON.stringify(body),
        'utf8'
      );
      fs.chmodSync(target, mode);
      return target;
    },
  };
};

const validInvite = (overrides: Record<string, unknown> = {}): Record<string, unknown> => ({
  schema_version: 1,
  game_id: GAME_ID,
  service_url: 'http://127.0.0.1:8765',
  join_token: 'join-secret',
  ...overrides,
});

const request = (overrides: Partial<InviteRequest> = {}): InviteRequest => ({
  gameId: GAME_ID,
  invite: '',
  joinToken: '',
  ...overrides,
});

const load = (
  workspace: WorkspacePaths,
  overrides: Partial<InviteRequest> = {},
  environment: Record<string, string | undefined> = {}
): Either.Either<Invitation, { readonly message: string }> =>
  Effect.runSync(Effect.either(loadInvitation(workspace, request(overrides), environment)));

const refusal = <A>(either: Either.Either<A, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

const accepted = <A, E>(either: Either.Either<A, E>): A => {
  expect(Either.isRight(either)).toBe(true);
  if (Either.isLeft(either)) throw new Error('expected an accepted invitation');
  return either.right;
};

/** The one remediation sentence every refusal ends with. */
const REMEDY =
  `Ask the game owner to run \`just invite ${GAME_ID}\` from the ` +
  'repository root, then retry once.';

// ---------------------------------------------------------------------------

describe('a well-formed invitation', () => {
  test('the default file is read by game ID and carries token and origin', () => {
    const fixture = bench();
    fixture.write(`${GAME_ID}.json`, validInvite());
    expect(accepted(load(fixture.workspace))).toEqual({
      token: 'join-secret',
      base: 'http://127.0.0.1:8765',
    });
  });

  test('an explicitly configured file wins over the default one', () => {
    const fixture = bench();
    fixture.write(`${GAME_ID}.json`, validInvite({ join_token: 'default-secret' }));
    const configured = fixture.write(
      'explicit.json',
      validInvite({ join_token: 'explicit-secret', service_url: 'https://elsewhere.test' })
    );
    expect(accepted(load(fixture.workspace, { invite: configured }))).toEqual({
      token: 'explicit-secret',
      base: 'https://elsewhere.test',
    });
  });

  test('PLAY_INVITE configures the same path as --invite', () => {
    const fixture = bench();
    const configured = fixture.write('env.json', validInvite({ join_token: 'env-file-secret' }));
    expect(
      accepted(load(fixture.workspace, {}, { PLAY_INVITE: `  ${configured}  ` })).token
    ).toBe('env-file-secret');
  });

  test('a relative --invite is read against the workspace root', () => {
    const fixture = bench();
    fixture.write('relative.json', validInvite({ join_token: 'relative-secret' }));
    expect(accepted(load(fixture.workspace, { invite: '.invites/relative.json' })).token).toBe(
      'relative-secret'
    );
  });
});

// ---------------------------------------------------------------------------

describe('the credential override', () => {
  test('an environment token skips the default file and takes the env origin', () => {
    // The default invitation is unparseable JSON: a stale local file must not
    // block documented recovery, nor redirect the join to its old origin.
    const fixture = bench();
    fixture.write(`${GAME_ID}.json`, '{');
    expect(
      accepted(
        load(
          fixture.workspace,
          {},
          {
            AGENT_EVAL_JOIN_TOKEN: 'explicit-secret',
            AGENT_EVAL_SERVICE_URL: 'http://127.0.0.1:9999',
          }
        )
      )
    ).toEqual({ token: 'explicit-secret', base: 'http://127.0.0.1:9999' });
  });

  test('--join-token skips a `.invites` that is not even a directory', () => {
    const fixture = bench({ invites: false });
    fs.writeFileSync(path.join(fixture.root, '.invites'), 'not a directory', 'utf8');
    expect(
      accepted(load(fixture.workspace, { joinToken: 'flag-secret' })).token
    ).toBe('flag-secret');
  });

  test('it does NOT skip an explicitly configured invitation', () => {
    // The override is about the *implicit default*: an operator who named a
    // file still gets that file's origin, and its refusals.
    const fixture = bench();
    const configured = fixture.write('configured.json', validInvite({ join_token: ' padded ' }));
    const loaded = accepted(
      load(
        fixture.workspace,
        { invite: configured },
        { AGENT_EVAL_JOIN_TOKEN: 'explicit-secret' }
      )
    );
    // A rotten stored token is tolerated only because it is never used …
    expect(loaded.token).toBe('explicit-secret');
    // … while the configured file's service URL still decides the origin.
    expect(loaded.base).toBe('http://127.0.0.1:8765');
  });

  test('an explicitly configured file that does not exist is still a refusal', () => {
    const fixture = bench();
    expect(
      refusal(
        load(
          fixture.workspace,
          { invite: path.join(fixture.invites, 'absent.json') },
          { AGENT_EVAL_JOIN_TOKEN: 'explicit-secret' }
        )
      )
    ).toBe(`the configured invitation for ${GAME_ID} does not exist. ${REMEDY}`);
  });
});

// ---------------------------------------------------------------------------

describe('the rejection matrix', () => {
  interface Row {
    readonly name: string;
    /** Prepare the workspace and return the `--invite` value to pass. */
    readonly stage: (fixture: Bench) => Partial<InviteRequest>;
    readonly message: string;
    readonly invites?: boolean;
  }

  const rows: ReadonlyArray<Row> = [
    {
      name: 'a symlinked .invites cannot escape the player workspace',
      invites: false,
      stage: (fixture) => {
        const outside = path.join(fixture.root, '..', path.basename(fixture.root) + '-outside');
        fs.mkdirSync(outside, { recursive: true });
        roots.push(outside);
        fs.symlinkSync(outside, path.join(fixture.root, '.invites'), 'dir');
        return {};
      },
      message: INVITE_ROOT_NOT_REAL,
    },
    {
      name: 'a missing .invites names the owner recovery command',
      invites: false,
      stage: () => ({}),
      message: `the invitation directory is unavailable. ${REMEDY}`,
    },
    {
      name: 'an absolute path outside .invites is refused by path',
      stage: (fixture) => {
        const outside = path.join(fixture.root, 'outside-invite.json');
        fs.writeFileSync(outside, JSON.stringify(validInvite()), 'utf8');
        fs.chmodSync(outside, 0o600);
        return { invite: outside };
      },
      message: INVITE_ESCAPES,
    },
    {
      name: 'a traversal out of .invites is refused by path',
      stage: (fixture) => {
        const outside = path.join(fixture.root, 'escaped.json');
        fs.writeFileSync(outside, JSON.stringify(validInvite()), 'utf8');
        fs.chmodSync(outside, 0o600);
        return { invite: '.invites/../escaped.json' };
      },
      message: INVITE_ESCAPES,
    },
    {
      name: 'a symlinked invite file resolving outside .invites is refused',
      stage: (fixture) => {
        const outside = path.join(fixture.root, 'linked.json');
        fs.writeFileSync(outside, JSON.stringify(validInvite()), 'utf8');
        fs.chmodSync(outside, 0o600);
        const link = path.join(fixture.invites, 'link.json');
        fs.symlinkSync(outside, link);
        return { invite: link };
      },
      message: INVITE_ESCAPES,
    },
    {
      name: 'mode 0644 is not a credential',
      stage: (fixture) => ({ invite: fixture.write('loose.json', validInvite(), 0o644) }),
      message: `the invitation for ${GAME_ID} is not mode 0600. ${REMEDY}`,
    },
    {
      name: 'unparseable JSON is unreadable, not a stack trace',
      stage: (fixture) => ({ invite: fixture.write('broken.json', '{') }),
      message: `the invitation for ${GAME_ID} is unreadable. ${REMEDY}`,
    },
    {
      name: 'a JSON array is unreadable too',
      stage: (fixture) => ({ invite: fixture.write('array.json', [1, 2, 3]) }),
      message: `the invitation for ${GAME_ID} is unreadable. ${REMEDY}`,
    },
    {
      name: 'schema_version 2 is an unsupported schema',
      stage: (fixture) => ({
        invite: fixture.write('v2.json', validInvite({ schema_version: 2 })),
      }),
      message: `the invitation for ${GAME_ID} has an unsupported schema. ${REMEDY}`,
    },
    {
      name: 'a missing schema_version is an unsupported schema',
      stage: (fixture) => {
        const body = validInvite();
        delete body['schema_version'];
        return { invite: fixture.write('noschema.json', body) };
      },
      message: `the invitation for ${GAME_ID} has an unsupported schema. ${REMEDY}`,
    },
    {
      name: "another game's invitation is refused by game ID",
      stage: (fixture) => ({
        invite: fixture.write('other.json', validInvite({ game_id: OTHER_ID })),
      }),
      message: `the invitation belongs to a different game. ${REMEDY}`,
    },
    {
      name: 'an invitation with no game_id at all belongs to a different game',
      stage: (fixture) => {
        const body = validInvite();
        delete body['game_id'];
        return { invite: fixture.write('nogame.json', body) };
      },
      message: `the invitation belongs to a different game. ${REMEDY}`,
    },
    {
      name: 'a blank join token is invalid',
      stage: (fixture) => ({
        invite: fixture.write('blank.json', validInvite({ join_token: '   ' })),
      }),
      message: `the invitation for ${GAME_ID} has an invalid join token. ${REMEDY}`,
    },
    {
      name: 'an untrimmed join token is invalid',
      stage: (fixture) => ({
        invite: fixture.write('padded.json', validInvite({ join_token: ' secret ' })),
      }),
      message: `the invitation for ${GAME_ID} has an invalid join token. ${REMEDY}`,
    },
    {
      name: 'a non-string join token is invalid',
      stage: (fixture) => ({
        invite: fixture.write('numeric.json', validInvite({ join_token: 12345 })),
      }),
      message: `the invitation for ${GAME_ID} has an invalid join token. ${REMEDY}`,
    },
    {
      name: 'a non-string service URL is invalid',
      stage: (fixture) => ({
        invite: fixture.write('nullurl.json', validInvite({ service_url: null })),
      }),
      message: `the invitation for ${GAME_ID} has an invalid service URL. ${REMEDY}`,
    },
    {
      name: 'a service URL carrying credentials is invalid',
      stage: (fixture) => ({
        invite: fixture.write(
          'creds.json',
          validInvite({ service_url: 'http://user:pass@127.0.0.1:8765' })
        ),
      }),
      message: `the invitation for ${GAME_ID} has an invalid service URL. ${REMEDY}`,
    },
    {
      name: 'no invitation at all names the owner recovery command',
      stage: () => ({}),
      message: `no join invitation for ${GAME_ID}. ${REMEDY}`,
    },
  ];

  for (const row of rows) {
    test(row.name, () => {
      const fixture = bench(row.invites === false ? { invites: false } : {});
      const overrides = row.stage(fixture);
      expect(refusal(load(fixture.workspace, overrides))).toBe(row.message);
    });
  }

  test('every refusal names `just invite {game_id}` verbatim', () => {
    for (const row of rows) {
      const fixture = bench(row.invites === false ? { invites: false } : {});
      const overrides = row.stage(fixture);
      const message = refusal(load(fixture.workspace, overrides));
      const remediable = message !== INVITE_ROOT_NOT_REAL && message !== INVITE_ESCAPES;
      expect(remediable ? message.includes(`just invite ${GAME_ID}`) : true).toBe(true);
    }
  });
});

// ---------------------------------------------------------------------------

/**
 * `Path.resolve()` expands each symlink as it walks, so a `..` pops a component
 * off the *resolved* prefix.  Normalizing `..` lexically first — which is what
 * `path.resolve` / `path.join` do — silently re-points every one of these paths
 * at a different file, and the escape refusal never fires.  Each expectation
 * below was taken from CPython (`Path(p).resolve()`), not from the port.
 */
describe('containment across a symlinked component', () => {
  /** A real invitation the loader must never read, planted where the escape lands. */
  const plant = (target: string, token: string): void => {
    fs.writeFileSync(target, JSON.stringify(validInvite({ join_token: token })), 'utf8');
    fs.chmodSync(target, 0o600);
  };

  test('`..` through a directory symlink escapes .invites/ and is refused', () => {
    // `.invites/d` -> `<root>/outside`, so `.invites/d/../x.json` is
    // `<root>/x.json`.  Lexically it is `.invites/x.json`, a file that exists
    // and holds a different token — accepting it is a credential swap.
    const fixture = bench();
    fs.mkdirSync(path.join(fixture.root, 'outside'));
    fs.symlinkSync(path.join(fixture.root, 'outside'), path.join(fixture.invites, 'd'), 'dir');
    plant(path.join(fixture.root, 'x.json'), 'escaped-secret');
    fixture.write('x.json', validInvite({ join_token: 'inside-secret' }));
    expect(refusal(load(fixture.workspace, { invite: '.invites/d/../x.json' }))).toBe(
      INVITE_ESCAPES
    );
  });

  test('it is refused by path, not by the absence of the lexical twin', () => {
    // The same traversal with no `.invites/x.json` at all must still be the
    // escape refusal — "does not exist" would mean the port had looked inside.
    const fixture = bench();
    fs.mkdirSync(path.join(fixture.root, 'outside'));
    fs.symlinkSync(path.join(fixture.root, 'outside'), path.join(fixture.invites, 'd'), 'dir');
    plant(path.join(fixture.root, 'x.json'), 'escaped-secret');
    expect(refusal(load(fixture.workspace, { invite: '.invites/d/../x.json' }))).toBe(
      INVITE_ESCAPES
    );
  });

  test('a `..` that lands back inside .invites/ is accepted', () => {
    // `.invites/deep` -> `.invites/a/b`, so `.invites/deep/../../keep.json` is
    // `.invites/keep.json`.  Lexical normalization makes it `<root>/keep.json`
    // and refuses a perfectly good invitation — the divergence in the other
    // direction, and just as wrong.
    const fixture = bench();
    fs.mkdirSync(path.join(fixture.invites, 'a', 'b'), { recursive: true });
    fs.symlinkSync(path.join(fixture.invites, 'a', 'b'), path.join(fixture.invites, 'deep'), 'dir');
    fixture.write('keep.json', validInvite({ join_token: 'kept-secret' }));
    expect(
      accepted(load(fixture.workspace, { invite: '.invites/deep/../../keep.json' })).token
    ).toBe('kept-secret');
  });

  test('a relative symlink target is resolved against its own directory', () => {
    const fixture = bench();
    fs.mkdirSync(path.join(fixture.invites, 'a', 'b'), { recursive: true });
    fs.symlinkSync('a/b', path.join(fixture.invites, 'rel'), 'dir');
    fixture.write('keep.json', validInvite({ join_token: 'relative-target-secret' }));
    expect(
      accepted(load(fixture.workspace, { invite: '.invites/rel/../../keep.json' })).token
    ).toBe('relative-target-secret');
  });

  test('a symlink to the workspace root is still an escape', () => {
    const fixture = bench();
    fs.symlinkSync('..', path.join(fixture.invites, 'up'), 'dir');
    plant(path.join(fixture.root, 'escaped.json'), 'escaped-secret');
    expect(refusal(load(fixture.workspace, { invite: '.invites/up/escaped.json' }))).toBe(
      INVITE_ESCAPES
    );
  });

  test('a symlink loop terminates as a missing invitation, not a hang', () => {
    // Non-strict resolution keeps the unresolved remainder rather than raising
    // ELOOP, so the path stays inside `.invites/` and fails the `is_file` test.
    const fixture = bench();
    fs.symlinkSync('ping', path.join(fixture.invites, 'pong'));
    fs.symlinkSync('pong', path.join(fixture.invites, 'ping'));
    expect(refusal(load(fixture.workspace, { invite: '.invites/ping' }))).toBe(
      `the configured invitation for ${GAME_ID} does not exist. ${REMEDY}`
    );
  });

  test('a symlinked .invites is refused before any of this matters', () => {
    // The root check runs first, so a hostile `.invites` never gets to argue
    // about traversal at all.
    const fixture = bench({ invites: false });
    const outside = path.join(fixture.root, 'real-invites');
    fs.mkdirSync(outside);
    fs.symlinkSync(outside, path.join(fixture.root, '.invites'), 'dir');
    plant(path.join(outside, `${GAME_ID}.json`), 'linked-secret');
    expect(refusal(load(fixture.workspace))).toBe(INVITE_ROOT_NOT_REAL);
  });
});

// ---------------------------------------------------------------------------

/**
 * `str.strip()` is not `String.prototype.trim()`, and this is the loader that
 * cares.  Python strips `\x1c`-`\x1f` and `\x85` and JavaScript does not;
 * JavaScript strips `﻿` and Python does not.  Every expectation below was
 * taken from `client._invite` with a patched `ROOT`, not from the port.
 */
describe("CPython's whitespace class decides who is a credential", () => {
  const SEPARATORS: ReadonlyArray<readonly [string, string]> = [
    ['', 'FS'],
    ['', 'GS'],
    ['', 'RS'],
    ['', 'US'],
    ['', 'NEL'],
  ];

  for (const [character, label] of SEPARATORS) {
    test(`a stored join token ending in ${label} is untrimmed-equal and refused`, () => {
      // `.trim()` leaves these alone, so the port used to ACCEPT the file and
      // send `join-secret\x1f` as the bearer — a credential that is not the one
      // CPython would ever send.
      const fixture = bench();
      fixture.write(`${GAME_ID}.json`, validInvite({ join_token: `join-secret${character}` }));
      expect(refusal(load(fixture.workspace))).toBe(
        `the invitation for ${GAME_ID} has an invalid join token. ${REMEDY}`
      );
    });

    test(`AGENT_EVAL_JOIN_TOKEN of a lone ${label} is blank, not an override`, () => {
      // The worst shape of the divergence: treating it as a token skips the
      // default invitation entirely, sends an empty bearer, and joins the
      // invitation's *replaced* origin rather than its declared one.
      const fixture = bench();
      fixture.write(`${GAME_ID}.json`, validInvite({ service_url: 'http://127.0.0.1:7777' }));
      expect(
        accepted(load(fixture.workspace, {}, { AGENT_EVAL_JOIN_TOKEN: character }))
      ).toEqual({ token: 'join-secret', base: 'http://127.0.0.1:7777' });
    });

    test(`a --invite path padded with ${label} still loads`, () => {
      const fixture = bench();
      fixture.write('padded-path.json', validInvite({ join_token: 'path-secret' }));
      expect(
        accepted(
          load(fixture.workspace, {
            invite: `${character}.invites/padded-path.json${character}`,
          })
        ).token
      ).toBe('path-secret');
    });

    test(`a PLAY_INVITE path padded with ${label} still loads`, () => {
      const fixture = bench();
      fixture.write('padded-path.json', validInvite({ join_token: 'path-secret' }));
      expect(
        accepted(
          load(
            fixture.workspace,
            {},
            { PLAY_INVITE: `${character}.invites/padded-path.json${character}` }
          )
        ).token
      ).toBe('path-secret');
    });
  }

  test('a stored join token ending in ZWNBSP is a perfectly good token', () => {
    // The divergence in the other direction: `.trim()` strips `﻿`, so the
    // port used to refuse a file CPython accepts.
    const fixture = bench();
    fixture.write(`${GAME_ID}.json`, validInvite({ join_token: 'join-secret﻿' }));
    expect(accepted(load(fixture.workspace)).token).toBe('join-secret﻿');
  });

  test('AGENT_EVAL_JOIN_TOKEN of a lone ZWNBSP IS an override', () => {
    const fixture = bench();
    fixture.write(`${GAME_ID}.json`, validInvite({ service_url: 'http://127.0.0.1:7777' }));
    expect(accepted(load(fixture.workspace, {}, { AGENT_EVAL_JOIN_TOKEN: '﻿' }))).toEqual({
      token: '﻿',
      base: 'http://127.0.0.1:8765',
    });
  });

  test('a --invite path padded with ZWNBSP is a different path, and escapes', () => {
    const fixture = bench();
    fixture.write('padded-path.json', validInvite({ join_token: 'path-secret' }));
    expect(
      refusal(load(fixture.workspace, { invite: '﻿.invites/padded-path.json﻿' }))
    ).toBe(INVITE_ESCAPES);
  });
});

// ---------------------------------------------------------------------------

/**
 * `Path.read_text(encoding="utf-8")` is strict.  Node's `'utf8'` reader is not:
 * it substitutes U+FFFD for an invalid byte, which would turn a malformed
 * credential file into a *different, well-formed* credential.
 */
describe('the file is decoded strictly, or it is unreadable', () => {
  const UNREADABLE = `the invitation for ${GAME_ID} is unreadable. ${REMEDY}`;

  const writeBytes = (fixture: Bench, name: string, bytes: Uint8Array): string => {
    const target = path.join(fixture.invites, name);
    fs.writeFileSync(target, bytes);
    fs.chmodSync(target, 0o600);
    return target;
  };

  test('an invalid UTF-8 byte inside join_token is unreadable, not U+FFFD', () => {
    const fixture = bench();
    const bytes = Buffer.concat([
      Buffer.from(
        `{"schema_version":1,"game_id":"${GAME_ID}",` +
          '"service_url":"http://127.0.0.1:8765","join_token":"to',
        'utf8'
      ),
      Buffer.from([0xff]),
      Buffer.from('k"}', 'utf8'),
    ]);
    expect(refusal(load(fixture.workspace, { invite: writeBytes(fixture, 'bad.json', bytes) }))).toBe(
      UNREADABLE
    );
  });

  test('a truncated multi-byte sequence is unreadable', () => {
    const fixture = bench();
    const bytes = Buffer.concat([
      Buffer.from(JSON.stringify(validInvite()).slice(0, -1), 'utf8'),
      Buffer.from([0xe2, 0x82]),
      Buffer.from('}', 'utf8'),
    ]);
    expect(
      refusal(load(fixture.workspace, { invite: writeBytes(fixture, 'trunc.json', bytes) }))
    ).toBe(UNREADABLE);
  });

  test('a leading BOM is kept, so the JSON is unreadable exactly as CPython says', () => {
    // `TextDecoder` strips the BOM by default; the utf-8 codec does not, and
    // `json.loads("﻿{…}")` raises.  Stripping it would ACCEPT a file
    // CPython refuses.
    const fixture = bench();
    const bytes = Buffer.concat([
      Buffer.from([0xef, 0xbb, 0xbf]),
      Buffer.from(JSON.stringify(validInvite()), 'utf8'),
    ]);
    expect(refusal(load(fixture.workspace, { invite: writeBytes(fixture, 'bom.json', bytes) }))).toBe(
      UNREADABLE
    );
  });

  test('a well-formed non-ASCII token survives the strict decode', () => {
    const fixture = bench();
    fixture.write(`${GAME_ID}.json`, validInvite({ join_token: 'jöin-sécret-✓' }));
    expect(accepted(load(fixture.workspace)).token).toBe('jöin-sécret-✓');
  });
});

// ---------------------------------------------------------------------------

describe('the returned origin', () => {
  test('a default invitation without a usable URL falls back to the environment', () => {
    // `service_url` is validated as a string but an empty one is `None` to
    // `service_url()`, which then reads AGENT_EVAL_SERVICE_URL.
    const fixture = bench();
    fixture.write(`${GAME_ID}.json`, validInvite({ service_url: '' }));
    expect(
      accepted(load(fixture.workspace, {}, { AGENT_EVAL_SERVICE_URL: 'https://supervisor.test' }))
        .base
    ).toBe('https://supervisor.test');
  });

  test('the origin is normalized exactly once, trailing slash and case', () => {
    const fixture = bench();
    fixture.write(`${GAME_ID}.json`, validInvite({ service_url: 'HTTP://127.0.0.1:8765/' }));
    expect(accepted(load(fixture.workspace)).base).toBe('http://127.0.0.1:8765');
  });
});
