# `freeciv-agent` private IPC

`freeciv-agent` is a headless Freeciv client sidecar. It uses the ordinary
Freeciv client core and player connection; the IPC described here is private
to the local supervisor and is not a public API. The public control API remains
JSON over the authenticated supervisor service.

Launch the client with the normal Freeciv connection options, then place the
sidecar-only arguments after `--`:

```text
freeciv-agent --autoconnect --name SIDECAR_USER --server HOST --port PORT -- \
  --ipc-fd INHERITED_FD --player EXACT_FREECIV_PLAYER
```

The `--` delimiter is mandatory. The only accepted common options are one each
of `--name`, `--server`, `--port`, `--autoconnect`, and optionally `--debug`;
unknown, duplicated, or dangerous common client options are rejected before
client startup. `--player` is a bounded ASCII identifier using letters,
digits, `.`, `_`, and `-`.

The supervisor must pass an already-connected, full-duplex AF_UNIX
`SOCK_STREAM` descriptor with `pass_fds` or an equivalent facility. The client
validates the socket before startup and then sets `O_NONBLOCK` and
`FD_CLOEXEC`. Credentials, tokens, and invitations must never be placed on the
command line or sent through this protocol.

The sidecar loads the selected Freeciv tileset before connecting, using a
bounded metadata-only sprite backend that never decodes pixels. It disables
option saving and always uses hackless mode, including release builds, so it
cannot request local server privilege.

## Framing

Each message is one unsigned 32-bit big-endian byte length followed by that
many payload bytes. Payloads are 1 through 8192 bytes. They must be valid UTF-8
and may not contain NUL, CR, LF, DEL, or ASCII controls other than TAB. There
is no JSON parser in the native client.

The client first sends `HELLO<TAB>1<TAB>freeciv-agent`. The supervisor replies
with `HELLO<TAB>1`; the client acknowledges `HELLO<TAB>OK<TAB>1`. After that,
the accepted supervisor commands are:

- `PING<TAB>ASCII_TOKEN`
- `STATUS`
- `TAKE`
- `SHUTDOWN`

`TAKE` always refers to the exact `--player` value. The client waits until that
player exists in its ordinary, fog-correct client state, then sends the normal
Freeciv chat command `/take "PLAYER"`. It never issues a server/editor packet.
The immediate receipt is `TAKE<TAB>QUEUED`; successful submission emits
`TAKE<TAB>COMMAND_SENT`.
`READY<TAB>PLAYER` is emitted only after `client_player()` is exactly that
player and Freeciv reports the player as human-controlled. A queued takeover
that has not completed within 15 seconds emits one observable terminal result:
`TAKE_FAILED<TAB>NOT_CONNECTED`, `PLAYER_NOT_FOUND`, `NOT_ACQUIRED`, or
`SEND_FAILED`.

This first scaffold intentionally exposes no observations or gameplay actions.
Those will be added as versioned commands with validation and correlated
receipts; the server remains the legality authority.

## Current verification boundary

The native target is covered by a bounded process test over a local
`socketpair`. It exercises real common-client and tileset startup, framing,
HELLO, PING, STATUS, a queued TAKE receipt, SHUTDOWN, peer EOF, clean process
exit, and rejection of missing delimiters, unsafe player names, unsupported or
duplicated common options, and non-socket IPC descriptors. Connection,
player-list arrival, `/take` delivery, READY, alternate rulesets/maps, and
gameplay control still require an isolated real-server integration test before
they are claimed operational.
