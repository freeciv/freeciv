set shell := ["bash", "-uc"]
set quiet

export AGENT_EVAL_SERVICE_URL := env_var_or_default("AGENT_EVAL_SERVICE_URL", "https://freeciv-api.localhost")
export AGENT_EVAL_ADMIN_TOKEN := env_var_or_default("AGENT_EVAL_ADMIN_TOKEN", "freeciv-local-dev")
export AGENT_EVAL_STATE_DIR := env_var_or_default("AGENT_EVAL_STATE_DIR", ".agent-eval")

# Show the short local workflow.
default:
    @echo "Freeciv agent quick start"
    @echo
    @echo "  just start"
    @echo "  just single              # full-control-v2 vs the classic AI, 10m/turn"
    @echo "  just single v1           # strategic-v1 instead"
    @echo "  just single cheating     # AI with deity-style advantages"
    @echo "  just multi 3 infinite    # 3 agents, no deadline"
    @echo "  just play GAME_ID        # per-player workspace under .play/"
    @echo "  just watch GAME_ID"
    @echo "  just replay [GAME_ID]"
    @echo
    @echo "For model-vs-model: just multi 2, then just play GAME_ID for each seat."
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

# Create a one-agent-vs-native-AI game: just single [v1|v2] [difficulty] [players] [timing] [turns].
# The control protocol defaults to full-control-v2; pass v1 for strategic-v1.
[arg("max_turns", long="max-turns")]
single a="" b="" c="" d="" max_turns="":
    #!/usr/bin/env bash
    set -euo pipefail
    protocol=full-control-v2
    difficulty=hard
    args=()
    for token in "{{ a }}" "{{ b }}" "{{ c }}" "{{ d }}"; do
      case "$token" in
        '') ;;
        v1) protocol=strategic-v1 ;;
        v2) protocol=full-control-v2 ;;
        novice|easy|normal|hard|cheating) difficulty="$token" ;;
        *) args+=("$token") ;;
      esac
    done
    a="${args[0]:-}"; b="${args[1]:-}"; c="${args[2]:-}"; d="${args[3]:-}"
    if [[ "$protocol" == full-control-v2 ]]; then
      for token in "$a" "$b" "$c"; do
        if [[ "$token" == blitz ]]; then
          echo "error: blitz is strategic-v1 only; full-control-v2 games use default (10 minutes per turn) or infinite" >&2
          exit 2
        fi
      done
    fi
    if [[ -n "$d" ]]; then
      echo "error: too many arguments; usage: just single [v1|v2] [difficulty] [players] [default|infinite] [turns]" >&2
      exit 2
    fi
    [[ -n "$a" ]] || a=default
    just _create single "$protocol" "$difficulty" "$a" "$b" "$c" "{{ max_turns }}"

# Create an all-agent game: just multi [v1|v2] [difficulty] [players] [timing] [turns].
# The control protocol defaults to full-control-v2; pass v1 for strategic-v1.
[arg("max_turns", long="max-turns")]
multi a="" b="" c="" d="" max_turns="":
    #!/usr/bin/env bash
    set -euo pipefail
    protocol=full-control-v2
    difficulty=hard
    args=()
    for token in "{{ a }}" "{{ b }}" "{{ c }}" "{{ d }}"; do
      case "$token" in
        '') ;;
        v1) protocol=strategic-v1 ;;
        v2) protocol=full-control-v2 ;;
        novice|easy|normal|hard|cheating) difficulty="$token" ;;
        *) args+=("$token") ;;
      esac
    done
    a="${args[0]:-}"; b="${args[1]:-}"; c="${args[2]:-}"; d="${args[3]:-}"
    if [[ "$protocol" == full-control-v2 ]]; then
      for token in "$a" "$b" "$c"; do
        if [[ "$token" == blitz ]]; then
          echo "error: blitz is strategic-v1 only; full-control-v2 games use default (10 minutes per turn) or infinite" >&2
          exit 2
        fi
      done
    fi
    if [[ -n "$d" ]]; then
      echo "error: too many arguments; usage: just multi [v1|v2] [difficulty] [players] [default|infinite] [turns]" >&2
      exit 2
    fi
    [[ -n "$a" ]] || a=default
    just _create multiplayer "$protocol" "$difficulty" "$a" "$b" "$c" "{{ max_turns }}"

# Create a full-control-v2 one-agent-vs-native-AI game.
[arg("max_turns", long="max-turns")]
single-v2 mode_or_places="default" places_or_turns="" turns="" max_turns="":
    @just _create single full-control-v2 hard "{{ mode_or_places }}" "{{ places_or_turns }}" "{{ turns }}" "{{ max_turns }}"

# Create a full-control-v2 all-agent game.
[arg("max_turns", long="max-turns")]
multi-v2 mode_or_places="default" places_or_turns="" turns="" max_turns="":
    @just _create multiplayer full-control-v2 hard "{{ mode_or_places }}" "{{ places_or_turns }}" "{{ turns }}" "{{ max_turns }}"

# Materialize per-player game workspaces under .play/ (interactive picker;
# also takes --player HARNESS:MODEL, --invite PATH, --force).
play *args="":
    python3 -B -m agent_eval.play_setup {{ args }}

# Shared creation parser: [places|timing] [timing|turns] [turns] grammar.
[private]
_create game_mode protocol difficulty mode_or_places places_or_turns turns max_turns:
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
      --mode "{{ game_mode }}" \
      --places "$resolved_places" \
      --turns "$turn_limit" \
      --timing-mode "$timing_mode" \
      --control-protocol "{{ protocol }}" \
      --difficulty "{{ difficulty }}" \
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
      "Games created by just single, just multi, just single-v2, or just multi-v2 already have a private player invitation staged." \
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

# Without an ID, print the bootstrap prompt. With an ID, enter the canonical
# player-only join flow; owner credentials never enter the harness session.
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
    args=(just join --game_id "{{ game_id }}" --name "$controller_name")
    if [[ -n "{{ place }}" ]]; then
      args+=(--place "{{ place }}")
    fi
    cd play
    exec "${args[@]}"

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
# Install the offline video renderer's pinned dependencies.
video-install:
    #!/usr/bin/env bash
    set -euo pipefail
    cd agent_eval/video
    if [[ ! -d node_modules ]]; then
      npm ci
    fi
    npx remotion browser ensure

# Export one finished run and render it to an MP4. Reads the run read-only.
# preset: "full" for the 1080p deliverable, "draft" for fast 720p iteration.
video game_id out="" preset="full": video-install
    #!/usr/bin/env bash
    set -euo pipefail
    repo="$PWD"
    export_dir="$repo/$AGENT_EVAL_STATE_DIR/video-exports/{{ game_id }}"
    output="{{ out }}"
    if [[ -z "$output" ]]; then
      mkdir -p "$AGENT_EVAL_STATE_DIR/videos"
      suffix=""
      [[ "{{ preset }}" == "draft" ]] && suffix="-draft"
      output="$repo/$AGENT_EVAL_STATE_DIR/videos/{{ game_id }}${suffix}.mp4"
    fi
    case "{{ preset }}" in
      full)  render_flags=() ;;
      # 0.5 keeps both dimensions integral (960x540); Remotion rejects the
      # fractional height that an exact-720p scale factor produces.
      draft) render_flags=(--scale 0.5 --jpeg-quality 70) ;;
      *) echo "unknown preset '{{ preset }}' (use full or draft)" >&2; exit 2 ;;
    esac
    python3 -B -m agent_eval.video_export "{{ game_id }}" \
      --runs-root "$AGENT_EVAL_STATE_DIR/runs" --out "$export_dir"
    # Remotion serves the dataset from its own public directory, so the run
    # artifacts themselves are never exposed to the renderer.
    public_dir="$repo/agent_eval/video/public/exports/{{ game_id }}"
    mkdir -p "$public_dir"
    cp "$export_dir/meta.json" "$export_dir/frames.json" "$public_dir/"
    cd "$repo/agent_eval/video"
    # Bash 3.2 treats an empty array as unset under `set -u`, so the full
    # preset's empty flag list needs the guarded expansion.
    npx remotion render src/index.ts GameFilm "$output" \
      --props "{\"gameId\":\"{{ game_id }}\"}" \
      ${render_flags[@]+"${render_flags[@]}"}
    echo "wrote $output"

# Typecheck the offline video renderer and run its unit tests.
video-check: video-install
    npm --prefix agent_eval/video run check

# Open the video renderer's studio against an already-exported game.
video-studio: video-install
    #!/usr/bin/env bash
    set -euo pipefail
    cd agent_eval/video
    npx remotion studio src/index.ts

# Show current public game state.
status game_id:
    python3 -B -m agent_eval game status "{{ game_id }}" --service-url "$AGENT_EVAL_SERVICE_URL"

# Run the complete Python suite; set e2e=0 for unit tests only.
test e2e="1":
    FREECIV_AGENT_E2E="{{ e2e }}" python3 -B -W error::ResourceWarning \
      -m unittest discover -s agent_eval/tests -v
