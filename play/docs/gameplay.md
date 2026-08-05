# Classic strategic-v1 gameplay

This page applies only when join reports `strategic-v1`. If join reports
`full-control-v2`, use [the full-control-v2 loop](full-control-v2.md) instead;
do not use `next`, `act`, or this trait-only strategy for that session.

## Goal and control boundary

The objective in each private observation is authoritative. The default is to
maximize final Freeciv civilization score.

You do not directly move units, choose city production, select research, or
conduct diplomacy. Your civilization remains a hard Classic Freeciv AI. Once
per turn, you set four AI-priority modifiers; Freeciv then performs legal game
actions using those priorities.

## What you may observe

Use only the private response from `just next` and your own earlier private
observations. It includes your civilization's turn/year, score, gold, city and
unit counts, research bulbs and target, culture, government, and current trait
values. It intentionally excludes opponents, terrain, individual cities and
units, diplomacy details, and the spectator map.

Never inspect parent directories, live saves, replay/map/video endpoints,
scorelogs, server logs, decision traces, or another player's credentials.

## Action

Every action has exactly this structure:

```json
{
  "type": "set_traits",
  "traits": {
    "aggressive": 0,
    "builder": 20,
    "expansionist": 30,
    "trader": 10
  }
}
```

Each target is an integer from `-49` to `50`. Classic uses base trait values
of 50 in this setup, so modifier `-49` yields effective value 1, `0` yields
50, and `50` yields 100.

- `aggressive`: willingness to declare war.
- `builder`: preference for city improvements.
- `expansionist`: preference for settlers and new cities.
- `trader`: preference for trade routes and related value.

They are tendencies, not guaranteed commands. Adapt to your own score and
economy trends.

## Loop

1. Join bound this workspace to your seat, so no command names one. Begin
   with `LAST_TURN=0` and run `just next --after_turn LAST_TURN`.
2. If state is `waiting`, repeat with the same `LAST_TURN`.
3. If state is terminal, stop.
4. Read the objective, observation, deadline, action schema, turn, and
   top-level observation ID.
5. Choose all four modifiers and call `just act` exactly once.
6. Set `LAST_TURN` to the observed turn only when `act` returns
   `accepted: true`, then continue. If it errors or is not accepted, keep
   `LAST_TURN` unchanged and call `next` again; the server redelivers any
   observation for which this seat has no action.

Run one loop only. Missing the shared action deadline makes the evaluation
invalid even though Freeciv may continue. In `default` mode the deadline is
180 seconds per turn; `blitz` is 60 seconds. In `infinite` mode there is no
agent deadline and `deadline_at` is `null`; the game still waits for every
agent action or owner cancellation. Never print session or invite files.
One workspace plays one seat: join binds it, `just use` prints it, and
`just use GAME_ID` rebinds it. Two seats in one workspace is unsupported —
copy the workspace per seat.

The assigned harness/model must read each observation and choose the action
directly. Do not write, launch, or delegate to an automated bot solely to beat
the clock.

## Terminal states

- `completed`: Freeciv ended normally and required actions arrived.
- `invalid`: a result exists, but an evaluation condition such as a timeout
  failed.
- `failed`: an operational/game failure prevented valid completion.
- `cancelled`: the owner stopped the match.

Stop immediately for any terminal state.

## How this Freeciv match can end

This setup leaves Classic Freeciv's default spaceship and allied victories
enabled. Conquest is also an inherent victory: a surviving civilization or
team wins after all non-barbarian opponents are defeated or surrendered.

- **Spacerace:** construct, launch, and successfully deliver a spaceship.
- **Conquest/team victory:** eliminate or force the surrender of every rival.
- **Allied victory:** all surviving normal civilizations are mutually allied.
- **Turn limit:** if no earlier victory ends the game, this benchmark stops at
  its configured horizon (5000 turns by default) and records final scores.

Classic also defines cultural-domination and world-peace victories, but this
benchmark does not enable them. The evaluation objective remains final
civilization score; Freeciv's game-ending victory and the benchmark's score
ranking/validity are related but separate results.
