# Player workspace boundary

This directory is the complete workspace for an autonomous Freeciv player.

`just join` is always the first command. It creates the seat every other
command reads and writes, so `start`, `turn`, `do`, `show`, `state`, `legal`
and the rest all refuse until it has run. For `full-control-v2` the order is:

    1. just join                 binds this workspace to its seat
    2. just start                leave the lobby: configure + set ready
    3. just turn                 the briefing
    4. just do "u1 found_city London; u2 route 32,73" --end --await --brief

then repeat step 4 until the game is terminal. Step 4 is a whole turn in one
call: it orders every actor, ends the phase, blocks for the next phase and
prints its full briefing. Order every actor that needs orders in that one
line rather than one call each -- the briefing and every receipt end with the
`next N actors: just do "..."` line that writes it. With nothing left to
order, step 4 is `just turn --end --await --brief`. Step 2 is this
workspace's lobby command; it is unrelated to the repository stack's
`just start`.

- Do not read, search, execute, or modify anything outside this directory.
- Use only the commands in this directory's `justfile` and the negotiated
  protocol's authenticated private observations.
- Never query spectator, replay, frame, video, save, scorelog, server-log, or
  owner/admin endpoints.
- Never print `.sessions/` or `.invites/` contents. They contain credentials.
  The `state/` mirror directory is a sanctioned projection and is safe to read.
- One workspace plays one seat. Join binds this workspace to the seat it
  joined, so no command needs a session argument; `just use` prints the bound
  seat and `just use GAME_ID` rebinds it.
- For `strategic-v1`, use only `next`/`act`, and advance `LAST_TURN` only
  after `act` returns `accepted: true`.
- For `full-control-v2`, play with `start`, `turn`, `do … --end --await
  --brief` and `show`, and reach anything they do not cover with `state`, `legal`,
  `batch`, `receipt`, `retry`, `health` and `wait`.
- The `full-control-v2` invariants: execute an enumerated action only at its
  exact state revision, resolve an uncertain POST by receipt first, and never
  replay an `ambiguous` receipt — it is terminal.
- Choose every action yourself. Never launch a bot and never hand a unit to
  the game's own AI.
- Run exactly one observe/act loop for the current session.
- Stop when the API reports `completed`, `invalid`, `failed`, or `cancelled`.

Every `just X` here is also `./play X`; they are one CLI with one set of
flags and one set of errors.

For `full-control-v2`, `just help` prints the play card (`docs/play.md`) and
`just rules` prints the static gameplay rules — that is everything you need to
play. `docs/commands.md` and `docs/full-control-v2.md` are the harness-author
reference, not part of the per-turn budget. For `strategic-v1`, the loop is in
`docs/gameplay.md`.

## When a command fails

Every refusal names its own remedy: run exactly the command the error
prints, not a variant you invent. If the SAME error text comes back from
two different commands, it is not a game state you can play around — stop
cycling commands and report the exact error to the user. In particular,
any error naming an "unexpected" field or "unknown" kind means this
workspace's client is older than the server; that is never fixed by
retrying, only by re-materializing the workspace.
