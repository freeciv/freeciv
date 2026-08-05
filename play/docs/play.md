# Play (full-control-v2)

Run these in order. Every `just X` is also `./play X`; none takes a session
argument once step 1 binds this workspace to your seat.

```sh
1. just join                             # FIRST. nothing works before it
2. just start                            # lobby: configure + ready
3. just turn                             # the briefing
4. just do "u1 found_city London; c1 build Warriors" --end --await --brief
```

Repeat step 4 until terminal: it orders every actor, ends the phase, blocks,
then prints the next briefing — a whole turn in **one call**. Order every
actor in that one line, not one call each. With nothing left to order, step 4
is `just turn --end --await --brief`. Step 2 is this workspace's lobby
command, not the repository stack's; every flag is an override
(`--nation English --leader Ada`).

## Reading

The briefing (`just turn`, or the tail of `--brief`) is revision and turn,
economy, research, civ score, units, cities, a `needs decision:` line, one row
per actor still needing orders with its best options, and the `just do` line
that orders them all. `just turn --decisions` is that list on demand.

`just state --section chat` is the typed event feed (tech, growth, huts); the
briefing counts what is new since it looked.

`just show` reads the local mirror. **It never opens a socket.**

```sh
just show                     # header card, what changed, file list
just show units               # one file; bare just show lists them all
just show map --yields        # terrain grid with f/s/t yields
just show u1                  # that entity's rows and its option catalog
just show --grep found_city   # literal search, file:line; --regex too
```

A file older than the newest revision leads with a `stale:` line.

## Acting

An order is `<alias> <verb> [args]` (`u1 found_city London`, `phase end`) or a
bare action alias (`a7 London`). The verb is one a page advertised: an
action's kind, its tail, or its operation. Six words are sugar, each fixed to
one capability:

```sh
just do "u2 route 40,60 41,61"    # unit.order/set_route, goto
just do "u2 patrol 40,60 41,61"   # same capability, patrol
just do "c1 build Warriors; c1 queue Granary Settlers; c1 rally 33,70"
just do "research goal Currency"  # research.set_goal
```

Resolution is local. An order for an actor whose menu this seat never read
fetches that menu (`fetched u1 options (rev9)`) and retries; anything still
unresolved refuses the whole batch before any request, naming the
`just legal` that enumerates it. Orders run in order, one receipt each, then a
tail naming what still needs orders as the `just do` line that gives them. A
refusal stops the batch (`--continue-on-error` continues) and leaves the phase
open: `phase NOT ended`, the turn is still yours.

## Aliases

`a1..aN` name one enumerated action and die with its revision; `u1 c1 p1 r1`
name a unit, city, player, or relation for the whole game; `T(31,72)` names a
tile you have seen. They work anywhere an ID is taken and expand locally, so
the wire carries the server's ID. Quote `T(x,y)` in a shell. Acting bumps
the revision: a stale `aN` is re-bound when the action is unchanged
(`a3 rebound at rev14`), refused when it is gone; `--no-refresh` refuses
instead.

## Going deeper

The fast paths are sugar over:

```sh
just legal --actor_id u1 --all      # one actor's menu
just legal --kind unit.order --all  # one class; operation works too
just state --section SECTION --actor_id c1
just batch --action_id a3 --arguments '{"name":"London"}'
just receipt --batch_id ID | just retry --batch_id ID
just health | just wait | just use
```

## Standing rules

- **Errors carry their own remedy.** Every refusal names the command that
  fixes it. Run it; do not guess an ID.
- **`--json` is always there.** The text is a projection of that payload.

Choose every action yourself: no bot, no delegation to the game's AI. Keep
playing until the game is terminal.

Static rules: `just rules`. Harness reference: `docs/commands.md`,
`docs/full-control-v2.md` — not needed to play.
