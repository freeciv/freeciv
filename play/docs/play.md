# Play (full-control-v2)

Run these in order. Every `just X` is also `./play X`, and none takes a
session argument once step 1 binds this workspace to your seat.

```sh
1. just join                             # FIRST. nothing works before it
2. just start                            # lobby: configure + ready
3. just turn                             # the briefing
4. just do "u1 found_city London"        # 1..8 orders
5. just turn --end --await               # end phase, block, next header
```

Repeat 3-5 until terminal. Step 2 is this workspace's lobby command, not the
repository stack's `just start`; it takes no arguments, every flag being an
override (`--nation English --leader Ada`).

## Reading

`just turn` starts every decision: revision and turn, economy, research, your
civ score, units, cities, a `needs decision:` line, then one row per actor
still needing orders with its best options as aliases. `just turn --decisions`
is the same loop on demand, and fetches what the briefing did not.

`just state --section chat` is the typed event feed — tech learned, city
growth, huts. The briefing counts what is new since it looked.

`just show` reads the local mirror. **It never opens a socket.**

```sh
just show                     # header card, what changed, file list
just show units               # one file; bare just show lists them all
just show map --yields        # terrain grid with f/s/t yields
just show u1                  # that entity's rows and its option catalog
just show --grep found_city   # literal search, file:line; --regex too
```

Anything older than the newest revision leads with a `stale:` line; its
aliases are re-verified by meaning when used.

## Acting

An order is `<alias> <verb> [args]` — `u1 found_city London`, `phase end` — or
a bare action alias (`a7 London`). The verb is one a page advertised: an
action's kind (`unit.found_city`), its tail (`found_city`), or its operation.
Six words are the only sugar, each fixed to one capability:

```sh
just do "u2 route 40,60 41,61"    # unit.order/set_route, goto
just do "u2 patrol 40,60 41,61"   # same capability, patrol
just do "c1 build Warriors; c1 queue Granary Settlers; c1 rally 33,70"
just do "research goal Currency"  # research.set_goal
```

Resolution is local. An order for an actor whose menu this seat never read
fetches that menu (`fetched u1 options (rev9)`) and retries; anything still
unresolved refuses the whole batch before any request, naming the
`just legal` that enumerates it. Orders then run in order, one receipt
line each, then a `next:` line naming the next actor to decide. Execution
stops at the first refusal; `--continue-on-error` continues.

## Aliases

`a1..aN` name one enumerated action and die with its revision; `u1 c1 p1 r1`
name a unit, city, player, or relation for the whole game; `T(31,72)` names a
tile you have seen. They work anywhere an ID is taken and expand locally, so
the wire carries the server's own ID. Quote `T(x,y)` in a shell. Acting bumps
the revision: a stale `aN` is re-bound where the action is unchanged
(`a3 rebound at rev14`) and refused when it is gone. `--no-refresh` gets the
bare refusal.

## Going deeper

The fast paths are sugar over these:

```sh
just legal --actor_id u1 --all      # one actor's menu
just legal --kind unit.order --all  # one class; operation works too
just legal --full                   # global catalog, ungrouped
just state --section city_build_choices --actor_id c1
just batch --action_id a3 --arguments '{"name":"London"}'
just receipt --batch_id ID          # or: just retry --batch_id ID
just health | just wait | just use
```

## Standing rules

- **Errors carry their own remedy.** Every refusal names the command that
  fixes it. Run it; do not guess an ID.
- **`--json` is always there.** The text is a projection of that payload.

Choose every action yourself: no bot, no delegation to the game's AI. Keep
playing until the game is terminal.

Static rules: `just rules`. Harness-author reference: `docs/commands.md`,
`docs/full-control-v2.md` — you do not need them to play.
