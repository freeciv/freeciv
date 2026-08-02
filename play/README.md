# Freeciv player workspace

This directory is the complete harness-facing surface for playing an assigned
Freeciv match. It contains a standalone standard-library HTTP client, a small
Justfile, private session storage, and static gameplay documentation. It does
not import or require the Freeciv or `agent_eval` source tree.

## Player flow

From this directory:

```sh
just prompt --game_id GAME_ID --name codex-gpt-5.6-sol
just join --game_id GAME_ID --name codex-gpt-5.6-sol
just next --session SESSION_FILE --after_turn 0
```

After each observation, submit the documented `set_traits` action with
`just act --session SESSION_FILE`, and update `LAST_TURN` only after the
response says `accepted: true`. Use the exact session path printed by join for
every command; `.sessions/current` is not safe when harnesses share this
workspace. Continue until the API reports a terminal state. See
[commands](docs/commands.md) and [gameplay](docs/gameplay.md).

## Owner setup and isolation

Joining requires only a game-scoped join token—not the owner or admin token.
The root `just single` and `just multi` recipes automatically stage this
mode-`0600` player invitation:

```text
.invites/GAME_ID.json
```

```json
{
  "schema_version": 1,
  "game_id": "GAME_ID",
  "service_url": "http://127.0.0.1:8765",
  "join_token": "GAME_SCOPED_JOIN_TOKEN"
}
```

If it is missing or stale, the owner can rebuild it from the repository root
without displaying the token:

```sh
just invite GAME_ID
```

This owner command refuses if the original lobby is no longer open; staging an
invitation cannot revive a failed or terminal game.

The join response states whether the game uses `default` timing (180 seconds
per agent turn), `blitz` (60 seconds), or `infinite` (no agent deadline).
The assigned harness/model must inspect each observation and choose its action
directly; do not launch or delegate to an automated bot solely to beat the
clock.

The invite and generated `.sessions/` are ignored by Git. Never copy
`owner_token` or `admin_token` into this workspace.

The directory and `AGENTS.md` boundary is a policy boundary, not an
operating-system permission boundary. The current invitation bearer is also
shared by every still-open seat in a game; it is game-scoped, not seat-scoped
or single-use. A process that can ignore workspace rules or read another
process's files is therefore outside this threat model.

Strong adversarial isolation requires a separate OS/container workspace for
each harness, with this directory as its only mounted/readable workspace and
credential delivery controlled by the orchestrator. That separation is
necessary but not sufficient for hostile multiplayer while every seat shares
one bearer. Do not claim that normal directory permissions or the current
join protocol provide hostile same-user or multi-tenant isolation. Seat-scoped
invitations would require a coordinated supervisor, credential, and
join-protocol change and are not part of the current surface.
The client otherwise continues to work in a sandbox because it communicates
with the game only over HTTP.

Only player-safe commands are present. Live public standings, game creation,
cancellation, native watching, replay, frames, video, saves, and server
administration remain in the owner workspace outside this directory.
