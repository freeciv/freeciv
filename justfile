set shell := ["bash", "-uc"]
set quiet

export AGENT_EVAL_SERVICE_URL := env_var_or_default("AGENT_EVAL_SERVICE_URL", "https://freeciv-api.localhost")
export AGENT_EVAL_ADMIN_TOKEN := env_var_or_default("AGENT_EVAL_ADMIN_TOKEN", "freeciv-local-dev")
export AGENT_EVAL_STATE_DIR := env_var_or_default("AGENT_EVAL_STATE_DIR", ".agent-eval")

# Show the short local workflow.
default:
    @echo "Freeciv agent quick start"
    @echo
    @echo "  just prompt"
    @echo "  just start"
    @echo "  just single            # default: 180s/turn"
    @echo "  just single 2 blitz    # 60s/turn"
    @echo "  just single 2 infinite # no agent deadline"
    @echo "  just invite GAME_ID"
    @echo "  just join --game_id GAME_ID"
    @echo "  just watch GAME_ID"
    @echo "  just replay [GAME_ID]"
    @echo
    @echo "For model-vs-model: just multi 2 infinite, then join each place."
    @echo "Reference bot: just bot GAME_ID CONTROLLER_NAME"
    @echo "Run 'just --list' for every recipe."

# Build the headless Freeciv server and same-checkout full-control-v2 client.
build:
    #!/usr/bin/env bash
    set -euo pipefail
    if [[ ! -d build-agent ]]; then
      meson setup build-agent -Dclients=[] -Dtools=[] -Dfcmp=[]
    fi
    meson compile -C build-agent freeciv-server
    agent_meson_args=(
      -Dserver=disabled
      -Dclients=agent
      -Dtools=[]
      -Dfcmp=[]
      -Daudio=none
      -Dnls=false
      -Daimodules=[]
      -Dbuildtype=release
      -Ddebug=false
    )
    if [[ ! -f build-control-v2/build.ninja ]]; then
      meson setup build-control-v2 "${agent_meson_args[@]}"
    fi
    meson compile -C build-control-v2 freeciv-agent
    if [[ ! -x build-control-v2/freeciv-agent ]]; then
      echo "error: full-control-v2 client build did not produce build-control-v2/freeciv-agent" >&2
      exit 2
    fi

# Build the same-revision SDL2 client used by the owner-only live viewer.
build-viewer:
    #!/usr/bin/env bash
    set -euo pipefail
    if [[ "$(uname -s)" == "Darwin" ]]; then
      if ! command -v brew >/dev/null 2>&1; then
        echo "Homebrew is required to install SDL2_image and SDL2_ttf for the native viewer." >&2
        exit 2
      fi
      missing=()
      for formula in pkg-config sdl2 sdl2_image sdl2_ttf icu4c zstd; do
        if ! brew list --versions "$formula" >/dev/null 2>&1; then
          missing+=("$formula")
        fi
      done
      if ((${#missing[@]})); then
        brew install "${missing[@]}"
      fi
      # Homebrew keeps these mandatory Freeciv libraries keg-only.
      icu_pkg="$(brew --prefix icu4c)/lib/pkgconfig"
      zstd_pkg="$(brew --prefix zstd)/lib/pkgconfig"
      export PKG_CONFIG_PATH="$icu_pkg:$zstd_pkg:${PKG_CONFIG_PATH:-}"
    fi
    meson_args=(
      -Dserver=disabled
      -Dclients=sdl2
      -Dtools=[]
      -Dfcmp=[]
      -Daudio=none
      -Dnls=false
    )
    if [[ ! -f build-viewer/build.ninja ]]; then
      meson setup build-viewer "${meson_args[@]}"
    fi
    compile_args=()
    if command -v ninja >/dev/null 2>&1 \
       && { ninja --help 2>&1 || true; } | grep -q -- '--quiet'; then
      compile_args+=(--ninja-args=--quiet)
    fi
    meson compile -C build-viewer "${compile_args[@]}" freeciv-sdl2

# Install the browser replay viewer's pinned development dependencies.
replay-install:
    #!/usr/bin/env bash
    set -euo pipefail
    if [[ ! -d agent_eval/viewer/node_modules ]]; then
      npm --prefix agent_eval/viewer ci
    fi

# Typecheck and build the committed browser replay viewer served by `just start`.
replay-build: replay-install
    npm --prefix agent_eval/viewer run build

# Run only Vite on its legacy raw port (advanced UI development).
replay-dev: replay-install
    npm --prefix agent_eval/viewer run dev -- --host 127.0.0.1 --port 5173 --strictPort

# Run browser replay typechecks, unit tests, and the production build.
replay-check: replay-install
    npm --prefix agent_eval/viewer run check

# Start the complete local stack. Ctrl-C stops only children from this invocation.
start: build replay-build
    #!/usr/bin/env bash
    set -euo pipefail
    mkdir -p "$AGENT_EVAL_STATE_DIR"
    exec python3 -B -m agent_eval.local_stack start \
      --repo-root "$PWD" \
      --state-dir "$AGENT_EVAL_STATE_DIR" \
      --agent-binary "${AGENT_EVAL_AGENT_BINARY:-$PWD/build-control-v2/freeciv-agent}"

# Start only the raw Python supervisor on a fixed loopback port (advanced).
start-supervisor port="8765": build
    #!/usr/bin/env bash
    set -euo pipefail
    mkdir -p "$AGENT_EVAL_STATE_DIR"
    exec python3 -B -m agent_eval supervisor \
      --host 127.0.0.1 \
      --port "{{ port }}" \
      --runs-root "$AGENT_EVAL_STATE_DIR/runs" \
      --agent-binary "${AGENT_EVAL_AGENT_BINARY:-$PWD/build-control-v2/freeciv-agent}"

# Create the normal one-agent-vs-native-AI game.
[arg("max_turns", long="max-turns")]
single mode_or_places="default" places_or_turns="" turns="" max_turns="":
    #!/usr/bin/env bash
    set -euo pipefail
    first="{{ mode_or_places }}"
    second="{{ places_or_turns }}"
    third="{{ turns }}"
    case "$first" in
      default|blitz|infinite)
        timing_mode="$first"
        resolved_places="$second"
        [[ -n "$resolved_places" ]] || resolved_places=2
        positional_turns="$third"
        [[ -n "$positional_turns" ]] || positional_turns=5000
        ;;
      ''|*[!0-9]*)
        echo "error: timing mode must be default, blitz, or infinite" >&2
        exit 2
        ;;
      *)
        resolved_places="$first"
        case "$second" in
          default|blitz|infinite)
            timing_mode="$second"
            positional_turns="$third"
            [[ -n "$positional_turns" ]] || positional_turns=5000
            ;;
          '')
            timing_mode=default
            positional_turns=5000
            if [[ -n "$third" ]]; then
              echo "error: a turn limit cannot follow an empty timing argument" >&2
              exit 2
            fi
            ;;
          *[!0-9]*)
            echo "error: after places, use default, blitz, infinite, or a numeric turn limit" >&2
            exit 2
            ;;
          *)
            timing_mode=default
            positional_turns="$second"
            if [[ -n "$third" ]]; then
              echo "error: put the timing mode before the final turn limit" >&2
              exit 2
            fi
            ;;
        esac
        ;;
    esac
    turn_limit="{{ max_turns }}"
    [[ -n "$turn_limit" ]] || turn_limit="$positional_turns"
    mkdir -p "$AGENT_EVAL_STATE_DIR"
    python3 -B -m agent_eval game create \
      --service-url "$AGENT_EVAL_SERVICE_URL" \
      --mode single \
      --places "$resolved_places" \
      --turns "$turn_limit" \
      --timing-mode "$timing_mode" \
      --lobby-timeout-s 0 \
      --credentials "$AGENT_EVAL_STATE_DIR/games/{game_id}/owner.json" \
      --player-invite "play/.invites/{game_id}.json"

# Create an all-agent game; places is also the maximum agent count.
[arg("max_turns", long="max-turns")]
multi mode_or_places="default" places_or_turns="" turns="" max_turns="":
    #!/usr/bin/env bash
    set -euo pipefail
    first="{{ mode_or_places }}"
    second="{{ places_or_turns }}"
    third="{{ turns }}"
    case "$first" in
      default|blitz|infinite)
        timing_mode="$first"
        resolved_places="$second"
        [[ -n "$resolved_places" ]] || resolved_places=2
        positional_turns="$third"
        [[ -n "$positional_turns" ]] || positional_turns=5000
        ;;
      ''|*[!0-9]*)
        echo "error: timing mode must be default, blitz, or infinite" >&2
        exit 2
        ;;
      *)
        resolved_places="$first"
        case "$second" in
          default|blitz|infinite)
            timing_mode="$second"
            positional_turns="$third"
            [[ -n "$positional_turns" ]] || positional_turns=5000
            ;;
          '')
            timing_mode=default
            positional_turns=5000
            if [[ -n "$third" ]]; then
              echo "error: a turn limit cannot follow an empty timing argument" >&2
              exit 2
            fi
            ;;
          *[!0-9]*)
            echo "error: after places, use default, blitz, infinite, or a numeric turn limit" >&2
            exit 2
            ;;
          *)
            timing_mode=default
            positional_turns="$second"
            if [[ -n "$third" ]]; then
              echo "error: put the timing mode before the final turn limit" >&2
              exit 2
            fi
            ;;
        esac
        ;;
    esac
    turn_limit="{{ max_turns }}"
    [[ -n "$turn_limit" ]] || turn_limit="$positional_turns"
    mkdir -p "$AGENT_EVAL_STATE_DIR"
    python3 -B -m agent_eval game create \
      --service-url "$AGENT_EVAL_SERVICE_URL" \
      --mode multiplayer \
      --places "$resolved_places" \
      --turns "$turn_limit" \
      --timing-mode "$timing_mode" \
      --lobby-timeout-s 0 \
      --credentials "$AGENT_EVAL_STATE_DIR/games/{game_id}/owner.json" \
      --player-invite "play/.invites/{game_id}.json"

# Rebuild one player-only invitation from its owner credentials.
invite game_id:
    #!/usr/bin/env bash
    set -euo pipefail
    python3 -B -m agent_eval game stage-invite "{{ game_id }}" \
      --credentials "$AGENT_EVAL_STATE_DIR/games/{{ game_id }}/owner.json" \
      --output "play/.invites/{{ game_id }}.json" \
      --require-open-lobby

# Print the prompt to paste into a fresh Codex, Claude, Pi, or other harness.
[arg("game_id", long)]
[arg("name", long)]
[arg("place", long)]
prompt game_id="" name="" place="":
    #!/usr/bin/env bash
    set -euo pipefail
    game_id="{{ game_id }}"
    [[ -n "$game_id" ]] || game_id=GAME_ID
    controller_name="{{ name }}"
    if [[ -z "$controller_name" || "$controller_name" == "Agent" || "$controller_name" == "HARNESS-MODEL" || "$controller_name" != *-* ]]; then
      controller_name=HARNESS-MODEL
    fi
    repo_root="$(pwd -P)"
    player_workspace="$repo_root/play"
    join_command=(just join --game_id "$game_id" --name "$controller_name")
    if [[ -n "{{ place }}" ]]; then
      join_command+=(--place "{{ place }}")
    fi
    timing_line="Timing: the exact mode and per-agent deadline are reported when you join."
    if [[ "$game_id" != GAME_ID ]]; then
      reported_timing="$(python3 -B -c '
    import json, math, sys, urllib.parse, urllib.request
    url = sys.argv[1].rstrip("/") + "/v1/games/" + urllib.parse.quote(sys.argv[2], safe="")
    with urllib.request.urlopen(url, timeout=3) as response:
        value = json.load(response)
    mode = value.get("timing_mode")
    timeout = value.get("action_timeout_s")
    if mode == "infinite" and timeout is None:
        print("Timing: infinite; there is no agent action deadline.")
    elif isinstance(mode, str) and isinstance(timeout, (int, float)) and not isinstance(timeout, bool) and math.isfinite(timeout):
        print(f"Timing: {mode}; each agent has {timeout:g} seconds per turn.")
    ' "$AGENT_EVAL_SERVICE_URL" "$game_id" 2>/dev/null || true)"
      [[ -n "$reported_timing" ]] && timing_line="$reported_timing"
    fi
    printf '%s\n' \
      "You are an autonomous Freeciv player using this repository's session API." \
      "" \
      "Your assigned game ID is: $game_id" \
      "" \
      "Before joining, identify yourself with a public harness-model label." \
      "Replace HARNESS-MODEL below with your real identity, for example:" \
      "codex-gpt-5.6-sol, pi-gpt-5.6-sol, or claude-code-claude-opus." \
      "Do not join with a generic label such as Agent." \
      "Games created by just single or just multi already have a private player invitation staged." \
      "$timing_line" \
      "You—the assigned harness/model—must inspect observations and choose actions directly." \
      "Do not write, launch, or delegate to an automated bot solely to beat the clock." \
      "" \
      "Enter the player-only workspace first:"
    printf '  cd %q\n\n' "$player_workspace"
    printf '%s\n' "Then run its player-only join command:"
    join_line="$(printf '%q ' "${join_command[@]}")"
    printf '  %s\n\n' "${join_line% }"
    cat <<EOF
    Do not run the repository-root owner join recipe. The command above is the
    player-only recipe inside $player_workspace. It consumes the staged
    mode-0600 invitation without putting its bearer token in command arguments,
    joins your assigned seat, saves a private session file, and returns the
    complete in-game prompt. Read and follow that returned prompt.

    Continue playing autonomously until the game reports completed, invalid,
    failed, or cancelled. Do not create a different game, invent a game ID,
    expose session credentials, or use omniscient spectator data as gameplay
    perception. If the game ID above is still GAME_ID, ask the user for it
    before running the join command. If joining reports that the supervisor is
    unreachable or the game ID is stale, stop and tell the user immediately;
    do not retry the same command in a loop.
    EOF

# Without an ID, print the bootstrap prompt. With an ID, join and print play instructions.
[arg("game_id", long)]
[arg("name", long)]
[arg("place", long)]
[no-exit-message]
join game_id="" name="Agent" place="":
    #!/usr/bin/env bash
    set -euo pipefail
    if [[ -z "{{ game_id }}" ]]; then
      exec just prompt --name "{{ name }}" --place "{{ place }}"
    fi
    controller_name="{{ name }}"
    if [[ -z "$controller_name" || "$controller_name" == "Agent" || "$controller_name" == "HARNESS-MODEL" || "$controller_name" != *-* ]]; then
      cat >&2 <<EOF
    A non-generic public harness-model identity is required to join.

    Retry with your truthful harness and model, for example:
      just join --game_id {{ game_id }} --name codex-gpt-5.6-sol
      just join --game_id {{ game_id }} --name pi-gpt-5.6-sol
      just join --game_id {{ game_id }} --name claude-code-claude-opus
    EOF
      exit 2
    fi
    game_state_dir="$AGENT_EVAL_STATE_DIR/games/{{ game_id }}"
    mkdir -p "$game_state_dir"
    session_key="$(python3 -B -c \
      'import sys; from agent_eval.client import controller_session_key; print(controller_session_key(sys.argv[1]))' \
      "$controller_name")"
    args=(
      python3 -B -m agent_eval game join "{{ game_id }}"
      --service-url "$AGENT_EVAL_SERVICE_URL"
      --credentials "$game_state_dir/owner.json"
      --controller-label "$controller_name"
      --session "$game_state_dir/${session_key}.json"
    )
    if [[ -n "{{ place }}" ]]; then
      args+=(--place "{{ place }}")
    fi
    if ! curl --silent --show-error --fail --max-time 3 \
      "$AGENT_EVAL_SERVICE_URL/health" >/dev/null 2>&1; then
      cat >&2 <<EOF
    Cannot reach the Freeciv game supervisor at:
      $AGENT_EVAL_SERVICE_URL

    Game {{ game_id }} cannot be joined while its original supervisor is down.
    Game IDs belong to the supervisor process that created them; restarting a
    new supervisor does not recover an interrupted live game.

    If this is a local match, ask the game owner to:
      1. run 'just start' in a terminal and leave it running;
      2. create a new match with 'just single' or 'just multi';
      3. give you the new game ID.

    If the game is hosted elsewhere, set AGENT_EVAL_SERVICE_URL to that server.
    Stop here and report this problem to the user. Do not retry this stale ID.
    EOF
      exit 2
    fi
    join_error="$game_state_dir/${session_key}-join-error.log"
    if ! "${args[@]}" >"$game_state_dir/${session_key}-join.json" 2>"$join_error"; then
      if grep -qi 'game not found' "$join_error"; then
        cat >&2 <<EOF
    The Freeciv supervisor is running, but it does not know game {{ game_id }}.

    The game ID is wrong, expired, or belongs to a different supervisor. Ask
    the game owner for the current game ID and service URL. Do not keep retrying
    this ID and do not create a replacement game unless the user asks you to.
    EOF
      else
        cat "$join_error" >&2
        cat >&2 <<EOF

    The supervisor rejected the join request for game {{ game_id }}. Check that
    the lobby is still open, the requested place is available, and this
    checkout has the matching invitation credentials. Stop and ask the user
    rather than retrying blindly.
    EOF
      fi
      exit 2
    fi
    session_path="$(cd "$game_state_dir" && pwd)/${session_key}.json"
    timing_line="$(python3 -B -c '
    import json, math, sys
    value = json.load(open(sys.argv[1], encoding="utf-8"))
    mode = value.get("timing_mode") or "unknown"
    timeout = value.get("action_timeout_s")
    if mode == "infinite" and timeout is None:
        print("Timing mode: infinite (no agent action deadline)")
    elif isinstance(timeout, (int, float)) and not isinstance(timeout, bool) and math.isfinite(timeout):
        print(f"Timing mode: {mode} ({timeout:g} seconds per agent turn)")
    else:
        print(f"Timing mode: {mode} (deadline unavailable)")
    ' "$game_state_dir/${session_key}-join.json")"
    repo_root="$(pwd)"
    control_protocol="$(python3 -B -c '
    import json, sys
    value = json.load(open(sys.argv[1], encoding="utf-8"))
    print(value.get("control_protocol") or "strategic-v1")
    ' "$session_path")"
    if [[ "$control_protocol" == "full-control-v2" ]]; then
      cat <<EOF
    Joined full-control-v2 for game {{ game_id }} as $controller_name.

    Private session file: $session_path
    Protocol contract: $repo_root/docs/full-control-v2.md

    The headless Freeciv client sidecar and v2 state/action routes are not
    available yet. This lobby fails safely before Freeciv starts and never
    falls back to the strategic-v1 trait API. Do not run the strategic
    next/act loop for this session.
    EOF
      exit 0
    fi
    cat <<EOF
    You are now playing Freeciv through the strategic-v1 session API.

    Game ID: {{ game_id }}
    Controller: $controller_name
    $timing_line
    Private session file: $session_path
    Agent contract and action rules: $repo_root/agent_eval/README.md
    Classic Freeciv ruleset notes: $repo_root/data/classic/README.classic

    Read the agent contract before playing. The observation returned by the
    session API is your authoritative game state, and its objective is your
    goal for this match. This is strategic-v1, not primitive unit control:
    Freeciv's hard Classic AI executes legal city, unit, diplomacy, and combat
    actions. Once per turn you choose the target integer modifiers in [-49, 50]
    for aggressive, builder, expansionist, and trader.

    You—the assigned harness/model—must inspect each observation and choose
    its action directly. Do not write, launch, or delegate to an automated bot
    solely to beat the clock.

    Repeat until state is completed, invalid, failed, or cancelled:

    1. Long-poll for your next turn. Start with LAST_TURN=0:

       python3 -B -m agent_eval agent next --session "$session_path" --after-turn LAST_TURN --wait-s 120

    2. Read objective, observation, deadline_at, and action_schema. Choose all
       four integer trait targets in the documented range. OBSERVATION_ID is
       the nonempty top-level observation_id field returned by step 1; do not
       call act if it is absent.

    3. Submit the action exactly once; exact retries are safe:

       python3 -B -m agent_eval agent act --session "$session_path" --turn TURN --observation-id=OBSERVATION_ID --action '{"type":"set_traits","traits":{"aggressive":0,"builder":20,"expansionist":30,"trader":10}}'

    4. Advance LAST_TURN to TURN only after act returns accepted=true. If act
       fails or returns anything else, do not claim submission and do not
       advance: call next again with this same explicit session path and the
       unchanged LAST_TURN so the server can redeliver an unsubmitted turn.
       Never use a shared current-session pointer. Never print or share the
       session token. Run exactly one active observe/act loop for this session;
       do not resume a second loop concurrently. Do not use the omniscient
       watch endpoints as game perception.
    EOF

# Run the model-free reference bot for one game-scoped controller session.
bot game_id name:
    #!/usr/bin/env bash
    set -euo pipefail
    session_key="$(python3 -B -c \
      'import sys; from agent_eval.client import controller_session_key; print(controller_session_key(sys.argv[1]))' \
      '{{ name }}')"
    exec python3 -B -m agent_eval bot \
      --session "$AGENT_EVAL_STATE_DIR/games/{{ game_id }}/${session_key}.json"

# Launch the real same-revision Freeciv GUI as an owner-only global observer.
watch game_id: build build-viewer
    #!/usr/bin/env bash
    set -euo pipefail
    game_state_dir="$AGENT_EVAL_STATE_DIR/games/{{ game_id }}"
    credentials="$game_state_dir/owner.json"
    mkdir -p "$game_state_dir"
    lease_file="$(mktemp "$game_state_dir/viewer-lease.XXXXXX")"
    cleanup() {
      if [[ -s "$lease_file" ]]; then
        python3 -B -m agent_eval game native-viewer-release "{{ game_id }}" \
          --service-url "$AGENT_EVAL_SERVICE_URL" \
          --credentials "$credentials" \
          --lease-file "$lease_file" >/dev/null 2>&1 || true
      fi
      rm -f -- "$lease_file"
    }
    trap cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    python3 -B -m agent_eval game native-viewer-run "{{ game_id }}" \
      --service-url "$AGENT_EVAL_SERVICE_URL" \
      --credentials "$credentials" \
      --lease-file "$lease_file" \
      --snapshot-server "$PWD/build-agent/freeciv-server" \
      --client "$PWD/build-viewer/freeciv-sdl2" \
      --data-path "$PWD/data" \
      --log-dir "$game_state_dir"

# Open the already-running Portless arena or one game. Never starts processes.
[no-exit-message]
replay game_id="":
    python3 -B -m agent_eval.local_stack replay "{{ game_id }}"
# Show current public game state.
status game_id:
    python3 -B -m agent_eval game status "{{ game_id }}" --service-url "$AGENT_EVAL_SERVICE_URL"

# Run the complete Python suite; set e2e=0 for unit tests only.
test e2e="1":
    FREECIV_AGENT_E2E="{{ e2e }}" python3 -B -W error::ResourceWarning \
      -m unittest discover -s agent_eval/tests -v
