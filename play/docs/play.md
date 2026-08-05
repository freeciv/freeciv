# Play (full-control-v2)

The whole loop is four commands. `--session` is optional while this workspace
holds one joined seat.

```sh
just start --nation English --leader Ada --female   # lobby: configure + ready
just turn                                           # the briefing
just do "u1 found_city London; u2 move 32,73"       # 1..8 orders
just turn --end --await                             # end phase, block, header
```

## Reading

`just turn` starts every decision: revision and turn, economy and research,
your units and cities with their options, the terrain around them, and a
closing `needs decision:` line. Read it, act, end the phase.

`just show` reads the local mirror the client rewrites from every response.
**It never opens a socket**, so it costs no request budget and no game clock.

```sh
just show                     # header card, what changed, file list
just show units               # one projection: header overview units cities
                              # map delta nations styles governments
just show u1                  # that entity's rows and its option catalog
just show --grep found_city   # search every mirror file, file:line
```

`--grep` is literal text; add `--regex` to read it as a regular expression.

## Acting

An order is `<alias> <verb> [args]` — `u1 found_city London`, `c1 build
Warriors`, `research set_goal Currency`, `phase end` — or a bare action alias
(`a7 London`). The verb must be one a page advertised: an action's kind
(`unit.found_city`), the tail of that kind (`found_city`), or its operation.
Nothing is invented. Resolution is local, against the catalog cached for the
newest revision this seat knows; if any order does not select exactly one
cached capability the whole batch is refused before a single request, and each
line names the `just legal` command that enumerates it.

Orders run sequentially and print one receipt line each. Execution stops at
the first order that is not accepted; `--continue-on-error` keeps going.

## Aliases

| Alias | Names | Lifetime |
|---|---|---|
| `a1 a2 …` | one enumerated action | the revision it was enumerated at |
| `u1 c1 p1 r1` | a unit, city, player, relation | the whole game |
| `T(31,72)` | a tile this seat has seen | the whole game |

Aliases work anywhere the CLI takes an opaque ID; the client expands them
locally, so the wire still carries only the server's own ID. **Action aliases
die on the next revision** — every action you take bumps it — and a stale one
is refused with the exact `just legal` command that re-issues the numbers.
Quote `T(x,y)` in a shell.

## Going deeper

The fast paths are sugar over the same capabilities, never a ceiling.
Everything the wire offers stays reachable:

```sh
just legal --actor_id u1 --all        # one actor's menu
just legal --kind unit.order --all    # one class of action
just state --section city_build_choices --actor_id c1
just batch --action_id a3 --arguments '{"name":"London"}'
just receipt --batch_id BATCH_ID      # or: just retry --batch_id BATCH_ID
just health
just wait
```

## Two standing rules

- **Errors carry their own remedy.** Every refusal names the exact command
  that fixes it. Run what it names; do not guess a replacement ID, and do not
  go re-read documentation.
- **`--json` is always there.** These commands print compact text and the full
  wire payload with `--json`. The text is a projection, never a different
  capability, and a payload that does not match the contract is a rendering
  error naming `--json` — never a blank.

You must choose every action yourself: no bot, no delegation to the game's AI.
Keep playing until the game is terminal.

Static gameplay rules: `just rules`. Full reference for harness authors:
`docs/commands.md` and `docs/full-control-v2.md` — you do not need them to
play.
