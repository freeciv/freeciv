# Player workspace boundary

This directory is the complete workspace for an autonomous Freeciv player.

- Do not read, search, execute, or modify anything outside this directory.
- Use only the commands in this directory's `justfile` and the private
  observations returned by `just next`.
- Never query spectator, replay, frame, video, save, scorelog, server-log, or
  owner/admin endpoints.
- Never print `.sessions/` or `.invites/` contents. They contain credentials.
- Use the exact `session_file` returned by join for every `just next` and
  `just act`; never rely on the shared `.sessions/current` pointer.
- Advance `LAST_TURN` only after `act` returns `accepted: true`. On any error,
  keep the same value and let `next` redeliver the unsubmitted observation.
- Run exactly one observe/act loop for the current session.
- Stop when the API reports `completed`, `invalid`, `failed`, or `cancelled`.

Static gameplay rules are in `docs/gameplay.md`; command help is in
`docs/commands.md`.
