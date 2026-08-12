/**
 * The justfile cutover: command mentions follow PLAY_PROG.
 *
 * Parity mode (`PLAY_PROG=just`, the test-runner default from _preload.ts) is
 * what every golden test and the diff oracle pin; these tests cover the other
 * spelling — the one provisioned workspaces actually see.
 */
import { describe, expect, test } from 'bun:test';
import * as fs from 'node:fs';
import * as path from 'node:path';
import { Effect } from 'effect';
import { PROG_ENV, resolveProg, rewriteProgMentions } from 'src/services/prog-prefix';
import { writeMirror } from 'src/services/mirror/store';
import { scratchWorkspace } from 'test/_fixtures';

const withProg = <A>(value: string | undefined, body: () => A): A => {
  const previous = process.env[PROG_ENV];
  if (value === undefined) {
    delete process.env[PROG_ENV];
  } else {
    process.env[PROG_ENV] = value;
  }
  try {
    return body();
  } finally {
    if (previous === undefined) {
      delete process.env[PROG_ENV];
    } else {
      process.env[PROG_ENV] = previous;
    }
  }
};

describe('resolveProg', () => {
  test('defaults to ./play when unset', () => {
    withProg(undefined, () => expect(resolveProg()).toBe('./play'));
  });
  test('defaults to ./play when blank', () => {
    withProg('  ', () => expect(resolveProg()).toBe('./play'));
  });
  test('honours an explicit spelling', () => {
    withProg('just', () => expect(resolveProg()).toBe('just'));
  });
});

describe('rewriteProgMentions', () => {
  test('parity mode is the identity', () => {
    const line = 'next: just wait — or add --await --brief';
    expect(rewriteProgMentions(line, 'just')).toBe(line);
  });

  test('rewrites every registered verb', () => {
    expect(rewriteProgMentions('run just wait then just turn --end', './play')).toBe(
      'run ./play wait then ./play turn --end'
    );
    expect(rewriteProgMentions('just legal --actor_id u3 --all', './play')).toBe(
      './play legal --actor_id u3 --all'
    );
    expect(
      rewriteProgMentions('just do "u1 found_city London; u2 route 32,73" --end', './play')
    ).toBe('./play do "u1 found_city London; u2 route 32,73" --end');
  });

  test('multiple mentions in one text all move', () => {
    const briefing = [
      'ERRORS carry their own remedy.',
      'just start                                get into the game',
      'just turn                                 one briefing, one revision',
      'A failed wait command is a harness error; run just health first.',
    ].join('\n');
    const rewritten = rewriteProgMentions(briefing, './play');
    expect(rewritten).not.toContain('just start');
    expect(rewritten).toContain('./play start');
    expect(rewritten).toContain('./play turn');
    expect(rewritten).toContain('./play health');
  });

  test('prose, justfile, and unknown verbs survive', () => {
    const prose = 'it is just one call; adjust waiting; the justfile is gone; just because';
    expect(rewriteProgMentions(prose, './play')).toBe(prose);
    // `--list` is not a registered verb: the mention dies with the justfile.
    expect(rewriteProgMentions('just --list', './play')).toBe('just --list');
  });

  test('reads PLAY_PROG per call', () => {
    withProg('./play', () => {
      expect(rewriteProgMentions('next: just wait')).toBe('next: ./play wait');
    });
    withProg('just', () => {
      expect(rewriteProgMentions('next: just wait')).toBe('next: just wait');
    });
  });
});

describe('writeMirror', () => {
  const mirrorThrough = (prog: string): string => {
    const scratch = scratchWorkspace();
    try {
      return withProg(prog, () => {
        Effect.runSync(
          Effect.provide(
            writeMirror(scratch.workspace.stateRoot, ['header.txt'], 'NOT YOUR TURN — next: just wait'),
            scratch.layer
          )
        );
        return fs.readFileSync(path.join(scratch.workspace.stateRoot, 'header.txt'), 'utf8');
      });
    } finally {
      scratch.cleanup();
    }
  };

  test('spells guidance per PLAY_PROG', () => {
    expect(mirrorThrough('./play')).toBe('NOT YOUR TURN — next: ./play wait\n');
    expect(mirrorThrough('just')).toBe('NOT YOUR TURN — next: just wait\n');
  });
});

describe('the spawned CLI', () => {
  const spawnHelp = (env: Record<string, string | undefined>): string => {
    const result = Bun.spawnSync({
      cmd: ['bun', 'run', path.join(import.meta.dir, '..', 'src', 'bin.ts'), 'help'],
      env: { ...process.env, ...env },
      stdout: 'pipe',
      stderr: 'pipe',
    });
    return new TextDecoder().decode(result.stdout);
  };

  test('defaults to ./play spelling', () => {
    const out = spawnHelp({ PLAY_PROG: undefined });
    expect(out).toContain('./play join');
    expect(out).not.toMatch(/\bjust (join|turn|wait|do)\b/);
  });

  test('PLAY_PROG=just restores parity', () => {
    const out = spawnHelp({ PLAY_PROG: 'just' });
    expect(out).toContain('just join');
  });
});
