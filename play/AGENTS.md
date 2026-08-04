# Player workspace boundary

This directory is the complete workspace for an autonomous Freeciv player.

- Do not read, search, execute, or modify anything outside this directory.
- Use only the commands in this directory's `justfile` and the negotiated
  protocol's authenticated private observations.
- Never query spectator, replay, frame, video, save, scorelog, server-log, or
  owner/admin endpoints.
- Never print `.sessions/` or `.invites/` contents. They contain credentials.
- Use the exact `session_file` returned by join for every command; never rely
  on the shared `.sessions/current` pointer.
- For `strategic-v1`, use only `next`/`act` and advance `LAST_TURN` only after
  `act` returns `accepted: true`.
- For `full-control-v2`, use only `health`/`state`/`legal`/`batch`/`receipt`/
  `retry`/`wait`. Execute an enumerated opaque action only at its exact state
  revision. Resolve an uncertain POST by receipt first. An `ambiguous` receipt
  is terminal and must never be replayed.
- Run exactly one observe/act loop for the current session.
- Stop when the API reports `completed`, `invalid`, `failed`, or `cancelled`.

Static gameplay rules are in `docs/gameplay.md`, the v2 loop is in
`docs/full-control-v2.md`, and command help is in `docs/commands.md`.
