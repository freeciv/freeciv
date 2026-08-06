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
```

`just join` is always the first command: it creates the seat every other
command reads, so they all refuse until it has run. A workspace materialized
with a `.playconfig.json` needs no arguments here, and the refusal says so.

Join prints the negotiated control protocol and its exact loop, and binds this
workspace to the seat it joined: one workspace plays one seat, so no later
command takes a session argument. `just use` prints the bound seat and
`just use GAME_ID` rebinds it. For `strategic-v1`, use `next`/`act` against
that bound seat.

For `full-control-v2`, join also prints the protocol card, and `just help`
prints the same play card as a file (`docs/play.md`): `just start` for the
lobby, `just turn` for the briefing, `just do "u1 found_city London; c1 build
Warriors" --end --await --brief` for a whole turn in one call (orders, phase
end, the block, the next briefing), and `just show` to read the local mirror
with no network call. Everything the wire offers stays
reachable through `just state`, `just legal`, `just batch`, `just receipt`,
`just retry` and `just wait`, and every command takes `--json` for the full
wire payload. Every `just X` is also `./play X` — the same CLI without the
recipe layer.

The agent-facing surface is `just help` and `just rules`.
[commands](docs/commands.md) and
[full-control-v2 gameplay](docs/full-control-v2.md) are the harness-author
reference — complete, but not part of a playing agent's per-turn budget;
[strategic gameplay](docs/gameplay.md) covers the `strategic-v1` loop.
Custom harnesses should also use the versioned
[HTTP contract](docs/custom-harness-v2.md) and
[OpenAPI 3.1 document](docs/full-control-v2.openapi.json).

V2 health includes only the caller's latest durable phase-end attribution. It
lets a harness learn that its phase was auto-ended after a timeout without
granting access to the public spectator feed or another player's event.

## Owner setup and isolation

Joining requires only a game-scoped join token—not the owner or admin token.
The root `just single` and `just multi` recipes (full-control-v2 by
default; pass `v1` for strategic-v1) automatically stage this
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
per agent turn on `strategic-v1`, 10 minutes on `full-control-v2`), `blitz`
(60 seconds, `strategic-v1` only), or `infinite` (no agent deadline).
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
