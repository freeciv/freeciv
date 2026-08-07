# play-cli: Effect + Bun + TypeScript port of the play client

The canonical spec for the migration. Every worker and reviewer reads this file first.

## Goal

Replace `play/client.py` (11,907 LoC) + `play/state_mirror.py` (1,757 LoC) + `play/justfile`
(403-line dispatch veneer) with a single TypeScript CLI named `play`, built on
**Bun + Effect 3.22 + @effect/cli 0.77 + @effect/platform-bun**, structured after
`~/composio/composio/ts/packages/cli` (read it for idiom: `src/bin.ts` bootstrap,
`src/cli-main.ts` Layer composition, `src/commands/*.cmd.ts`, `src/services/*` as
Effect services/Layers).

**Parity first, redesign later.** Phase 1 (this migration) is a byte-compatible port:
same command names, same flags (including `--wait_s`/`--wait-s` dual spellings), same
stdout text, same exit codes (0 / 2 refusal / 75 EX_TEMPFAIL / 66 EX_NOINPUT), same
`state/` mirror file formats. The Python client's observable behavior is the spec; where
`play/docs/commands.md` and the Python disagree, the Python wins and the divergence is
noted in `NOTES.md`.

## Non-goals (phase 1)

- No supervisor/server changes. The wire contract `play/docs/full-control-v2.openapi.json`
  is read-only truth.
- No daemon mode, no persistent-connection redesign, no output redesign. Those are
  phase 2, after the byte-diff oracle runs clean.
- Do not delete `play/justfile` or touch anything under `play/` or `agent_eval/`.
- Do not touch live game workspaces under `.play/` except read-only copies for diffing.

## The two behavioral upgrades allowed in phase 1

Both are invisible in stdout bytes:

1. **Concurrent catalog drains.** `do`'s per-actor `legal` drains and per-page fetches
   run with bounded concurrency (`Effect.forEach`, concurrency 4), but notes/receipts
   PRINT in the same deterministic order the Python prints them. This is the fix for the
   15s serial `do` that caused the gpt-5.6-sol timeout death loop.
2. **Streamed receipt ledger.** Each order's receipt is appended (atomic append) to
   `state/receipts.log` the moment it applies, so a SIGKILLed process leaves a readable
   record. Stdout unchanged.

## Package layout

```
play-cli/
  package.json          # name "play-cli", bin { play: "./src/bin.ts" }, bun-first
  tsconfig.json         # strict, noUncheckedIndexedAccess, verbatimModuleSyntax
  PLAN.md PORT_MAP.md NOTES.md
  src/
    bin.ts              # bootstrap → cli-main
    cli-main.ts         # Layer stack + root command
    commands/*.cmd.ts   # one file per subcommand
    schema/             # Effect Schema for the 41 OpenAPI schemas + wire types
    services/           # V2Client (HTTP), SessionStore, Mirror, Aliases, Receipts
    render/             # text rendering: tables, map, catalogs, briefings
  test/                 # bun test; golden fixtures ported from play/tests/
```

Package manager: `bun` (single package, not a workspace). Dependencies pinned to the
composio catalog versions: `effect ^3.22.1`, `@effect/cli ^0.77.0`,
`@effect/platform ^0.97.1`, `@effect/platform-bun ^0.91.2`. No other runtime deps
without a note in NOTES.md.

**Type checking: TypeScript v7.** Pin devDependency `typescript` to `^7` — v7 is the
native (Go) compiler, so plain `bunx tsc --noEmit` is the typecheck; no `tsgo` or
preview packages. If any instruction elsewhere mentions `tsgo`, read it as `bunx tsc`
with typescript@7.

## Command surface (all 18, from `play/client.py` argparse)

`prompt join use next act health turn start do show state legal batch receipt retry
wait monitor result`

plus the implicit `help`/`rules` text surfaces the justfile carried — fold them in as
subcommands emitting the same docs.

## Code style (hard requirements)

- Zero `any`, zero `@ts-ignore`, zero unchecked casts. `unknown` + Schema narrowing.
- Errors are values: tagged errors in the Effect error channel
  (`PlayerError`, `V2ResponseError`, ...) mapped to exit codes + stderr text in exactly
  one place (cli-main). Never `throw` for expected failures.
- `const` everywhere, functional pipelines, no module-level mutable state.
- Explicit return types on exported functions.
- Every port unit carries tests. Where `play/tests/test_client.py` asserts output text
  for the unit's behavior, port those assertions as golden-fixture tests.

## The oracle

1. **Ported tests** from `play/tests/test_client.py` (12,368 LoC) and
   `test_state_mirror.py` — the per-unit gate.
2. **Offline byte-diff**: copy a finished game workspace
   (`.play/game_Hsit9YEuBjKdJPPouFoGVYlk_pi_gpt-5.6-sol/`) to scratch, run read-only
   commands (`show`, `show map`, `prompt`, `help`, `rules`, `state --section ...` render
   paths that read the mirror) under both `python3 play/client.py` and `bun src/bin.ts`,
   and diff stdout bytes. The integrator runs this; divergence is a failing gate.
3. **Adversarial review**: reviewers read the Python original against the TS port with
   the explicit brief to find behavioral divergence, not style.

## Operational rules for migration agents (hard requirements)

- **Never run a command that triggers a permission approval.** The human is not watching;
  one blocked prompt stalls the entire run (a single `rm -rf` cost this migration 8 hours).
  Concretely: no `rm`/`rmdir` of anything, no `sudo`, no `git push`, no chained `cd` into
  paths outside the repo/scratchpad.
- **Never delete — allocate fresh instead.** Need a clean scratch area? `mktemp -d` under
  the scratchpad, or a new uniquely-named subdir. Never clear an old one to reuse its name.
- **Defer all cleanup to the end.** Append leftover scratch paths to the `## Cleanup`
  section of NOTES.md; a single end-of-run pass (with the human present) removes them.

## Known traps (from the Python and its history)

- Alias tables (`u1/c1/p1/r1/T(x,y)`, `a1..aN` action aliases dying with their revision)
  are stateful across the workspace's `.v2-state`; exact renumbering behavior matters.
- Dual flag spellings (`--wait_s` and `--wait-s`) both accepted, only one at a time.
- "stale: rendered at revN, now revM" re-verification lines must match.
- An ambiguous receipt is terminal and must never be replayed; `retry` vs `receipt`
  semantics are safety-critical — port `_order_receipt_ok` and the refresh-disposition
  logic with extra care and tests.
- Schema drift caused 3 incidents historically **because validators were closed**:
  decode wire JSON permissively (unknown fields pass through), never
  `Schema.Struct` with exact-field rejection on server responses.
- Paged cursors: `--cursor`, `--offset`, "16/43 more --cursor ..." footer lines.
- Exit-code contract on `wait` (0/75/66) and "an applied phase end never exits non-zero
  because of how the wait after it turned out".
