# Player workspace boundary

This directory is the complete workspace for an autonomous Freeciv player.

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
- For `full-control-v2`, play with `start`, `turn`, `do`, `turn --end --await`
  and `show`, and reach anything they do not cover with `state`, `legal`,
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
