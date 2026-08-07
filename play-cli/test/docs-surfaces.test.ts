/**
 * `help` and `rules` — the two doc surfaces the justfile carried.
 *
 * The recipes were `@cat docs/play.md` and `@cat docs/gameplay.md`
 * (justfile:396-403), so the contract is not "prints something like the card",
 * it is "prints the file".  Two things therefore get asserted here:
 *
 * 1. the embedded constants are byte-identical to `play/docs/*.md`, so an edit
 *    under `play/docs/` fails this suite instead of drifting silently;
 * 2. running the command emits those bytes and exactly those bytes, trailing
 *    newline included — `cat` adds nothing and neither may this.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { describe, expect, test } from 'bun:test';
import { Command } from '@effect/cli';
import { BunContext } from '@effect/platform-bun';
import { Effect } from 'effect';
import { helpCommand } from 'src/commands/help.cmd';
import { rulesCommand } from 'src/commands/rules.cmd';
import { GAMEPLAY_RULES } from 'src/docs/gameplay-rules';
import { PLAY_CARD } from 'src/docs/play-card';

/** The Python tree this port is a copy of, from `play-cli/test/`. */
const docPath = (name: string): string =>
  path.resolve(import.meta.dir, '..', '..', 'play', 'docs', name);

/**
 * Run one subcommand and collect stdout as raw bytes.
 *
 * `Console.log` appends exactly one newline to its joined arguments, so
 * re-adding it here is what makes the comparison a byte comparison rather than
 * a line comparison — the trailing newline is the part `cat` would emit and a
 * line-wise assertion would never notice missing.
 */
const runForBytes = async <Name extends string, A>(
  command: Command.Command<Name, never, never, A>,
  argv: ReadonlyArray<string>
): Promise<string> => {
  const root = Command.make('play', {}, () => Effect.void).pipe(
    Command.withSubcommands([command])
  );
  let out = '';
  const original = console.log;
  console.log = (...parts: ReadonlyArray<unknown>) => {
    out += `${parts.join(' ')}\n`;
  };
  try {
    await Effect.runPromise(
      Command.run(root, { name: 'play', version: '0.1.0' })(['bun', 'play', ...argv]).pipe(
        Effect.provide(BunContext.layer),
        Effect.orDie
      )
    );
  } finally {
    console.log = original;
  }
  return out;
};

describe('the embedded documents', () => {
  test('the play card is play/docs/play.md, byte for byte', () => {
    expect(`${PLAY_CARD}\n`).toBe(fs.readFileSync(docPath('play.md'), 'utf8'));
  });

  test('the rules are play/docs/gameplay.md, byte for byte', () => {
    expect(`${GAMEPLAY_RULES}\n`).toBe(fs.readFileSync(docPath('gameplay.md'), 'utf8'));
  });

  test('the constants carry no trailing newline of their own', () => {
    // The printer supplies it. Carrying it in the constant *and* printing it
    // would put a blank line at the end of every `just help`.
    expect(PLAY_CARD.endsWith('\n')).toBe(false);
    expect(GAMEPLAY_RULES.endsWith('\n')).toBe(false);
  });

  test('the em dash survives the copy, so the bytes are UTF-8 not ASCII', () => {
    expect(PLAY_CARD).toContain('a whole turn in **one call**');
    expect(PLAY_CARD).toContain('—');
  });
});

describe('play help', () => {
  test('prints docs/play.md and nothing else', async () => {
    expect(await runForBytes(helpCommand, ['help'])).toBe(
      fs.readFileSync(docPath('play.md'), 'utf8')
    );
  });

  test('does not print the harness-author reference the recipe withheld', async () => {
    // justfile:396-397 — every doc character an agent reads is part of its
    // per-turn budget, so `commands.md` stays out of the agent-facing card.
    const printed = await runForBytes(helpCommand, ['help']);
    expect(printed).not.toContain('# Commands');
    expect(printed.length).toBeLessThan(6000);
  });
});

describe('play rules', () => {
  test('prints docs/gameplay.md and nothing else', async () => {
    expect(await runForBytes(rulesCommand, ['rules'])).toBe(
      fs.readFileSync(docPath('gameplay.md'), 'utf8')
    );
  });

  test('the two surfaces are different documents', async () => {
    const card = await runForBytes(helpCommand, ['help']);
    const rules = await runForBytes(rulesCommand, ['rules']);
    expect(card).not.toBe(rules);
  });
});
