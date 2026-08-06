# Offline match films

Turns a finished (or interrupted) arena run into an MP4 — the synthetic world
board animating turn by turn beside a live standings panel — entirely from what
is already on disk. No live server, no network, no replay gateway.

```bash
just video game_a8_dSs1WtX5NoDPHACckOKc4
just video game_a8_dSs1WtX5NoDPHACckOKc4 /tmp/epic-loss.mp4
```

The first run installs Remotion's pinned dependencies and downloads a headless
Chrome into `node_modules/.remotion`. Everything after that is offline.

## The two stages

**1. Export (Python, stdlib only).** `python -m agent_eval.video_export GAME_ID`
walks `.agent-eval/runs/GAME_ID/` read-only and writes a render dataset to
`.agent-eval/video-exports/GAME_ID/`:

| File | What it holds |
| --- | --- |
| `meta.json` | Game identity, protocol, ruleset, seeds, seat/controller table with faction colours, board dimensions, terrain catalog, interpolation stats. A few tens of kilobytes. |
| `frames.json` | One entry per replay turn: territory rows, cities, unit stacks, city names, and per-player stats (score, cities, units, gold, techs, government, current research and bulbs). Terrain and infrastructure appear only on the turns they change. Roughly 6 MB for a 596-turn match. |
| `cache/` | The save reader's derived parse cache. Safe to delete. |

Board snapshots come from `agent_eval.save_replay.board_from_autosave` through
`replay_gateway._default_board_loader` — the exact code path the replay gateway
serves `/v1/games/<id>/board.json` from. There is no second save parser.

Research shows accumulated bulbs but no completion percentage: these archives
record `research.cost` as zero on every turn, so there is no denominator to
divide by and none is invented.

Scores come from `replay.jsonl`, which records every turn. Autosaves may not.
A turn with no readable save is exported with `interpolated: true` and no board
payload; the renderer holds the previous board and the ticker says which turn it
is showing. `meta.json` reports `board_density` (the fraction of turns backed by
a real save) and lists the interpolated turns.

**2. Render (Remotion + TypeScript).** One composition, `GameFilm`, sized from
the dataset by `calculateMetadata`. The `just video` recipe copies `meta.json`
and `frames.json` into `public/exports/<game_id>/` so the renderer reads them
through `staticFile` and never touches a run directory.

## What you see

- **Title card** — matchup, controllers per seat, protocol, ruleset, world size,
  seed, wall clock, final score.
- **Match** — the board, one turn every 4 frames at 30 fps (about 7.5 turns per
  second), with the turn/year ticker, a territory-share bar, animated score
  counters, each seat's current research and accumulated bulbs, and live
  score, cities and technology charts. Match identity (game id and wall-clock
  duration) sits in the header strip rather than in a panel of its own.
- **End screen** — a seven-second hold on the final board full-bleed, the final
  standings, score/cities/technology history charts, and the outcome.

The board is painted to a 2D canvas at twice the layout resolution, using the
viewer's own geometry and palette (`agent_eval/viewer/src/board-geometry.ts`,
`index.css`): the same hex lattice, terrain colours, faction tints, ownership
boundaries, capital rings and unit chevrons. The one deliberate difference is
orientation — the film rotates the lattice by 30° so the board's long axis lies
horizontal and fills a landscape frame, instead of sitting as a diamond in the
middle of it. Tile adjacency and shape are untouched.

## Faction colours

Freeciv assigns nation colours with no knowledge of the map beneath them, and
the agent seat's default blue (`#0067A5`) sits on top of the ocean palette — its
territory and its city and unit markers disappear into the water.

`src/faction-color.ts` substitutes any faction colour that lands too close to
the terrain palette, measured as Euclidean distance in OKLab so "close" means
what it looks like. Two clearances, calibrated against the real seat colours:

| Colour | Nearest terrain | Distance | Result |
| --- | --- | --- | --- |
| `#0067A5` agent blue | Ocean | 0.065 | substituted |
| `#F38400` default orange | Desert | 0.093 | kept |
| `#FA8072` barbarian salmon | Desert | 0.119 | kept by terrain, may move for faction clearance |

A terrain clearance of 0.085 separates exactly the failing case from the working
ones. Faction-to-faction clearance is 0.15, because the default orange and the
barbarian salmon are only 0.087 apart from each other — not enough to tell two
empires apart on a busy board.

One pair is **pinned** rather than derived: `#0067A5` (the agent seat's blue)
always becomes `#A78BFA` (periwinkle violet). The replay viewer applies the same
remap, and the two surfaces have to agree, so this is a contract and not a
preference — a pinned colour is never displaced by a clearance check. Periwinkle
clears its nearest terrain (tundra) by 0.181 and sits at least 0.23 from every
other faction colour in use.

Anything else that needs moving draws from an ordered list (mint, magenta,
violet, cream, amber), skipping any that would collide with a colour already in
play, so the mapping stays deterministic per seat. **This is presentation
only** — it happens at render time, and neither the exported dataset nor the
replay viewer's data is touched.

## Naming the contenders

Every side is credited as **"<harness or model>: <Civilization>"**, with the
civilization read from the game's own data (`replay.jsonl` `players[].nation`)
rather than hardcoded. The a8 match therefore reads:

> pi-gpt-5.6-sol: English **vs** In-game Deity AI: Spanish

The native opponent is always "In-game Deity AI" — never its wire name
("Freeciv Classic AI") and never its raw player name ("NativePlace2"). The rule
lives in `src/faction-label.ts`, a port of the viewer's `faction-label.ts`, so a
film and a viewer tab credit a match identically.

## Styling

Compositions are styled with Tailwind v4 through `@remotion/tailwind-v4`, wired
up by `Config.overrideWebpackConfig(enableTailwind)` in `remotion.config.ts`.
The arena palette lives as theme tokens in `src/tailwind.css`, so components use
`bg-panel`, `text-muted`, `border-line` rather than style objects.

Two things stay outside Tailwind on purpose:

- **The board.** It is drawn into a `<canvas>`; there is nothing for a utility
  class to style.
- **Data-driven colour and size.** Faction colours and bar widths come from the
  dataset at runtime, so they remain inline `style` props. Tailwind cannot emit
  a class for a hex code it has never seen.

`src/theme.ts` still holds the palette as plain strings because the canvas
painter needs them that way; it and `src/tailwind.css` must be kept in sync.

## Tuning a render

Pass composition props to change the pacing or fidelity:

```bash
cd agent_eval/video
npx remotion render src/index.ts GameFilm out.mp4 \
  --props '{"gameId":"game_...","framesPerTurn":6,"showCityLabels":false}'
```

| Prop | Default | Effect |
| --- | --- | --- |
| `gameId` | the a8 match | Which exported dataset to read. |
| `framesPerTurn` | `4` | Higher is slower and longer. |
| `titleFrames` / `outroFrames` | `120` / `210` | Card lengths, in frames at 30 fps. |
| `superSample` | `2` | Canvas backing-store multiplier. |
| `showCityLabels` | `true` | Capital name labels on the board. |

For 2160p output add `--scale 2`. It costs roughly four times the render time
and is not the default.

Preview interactively with `just video-studio` (after exporting at least one
game), and typecheck with `just video-check`.

## Layout

```
src/
  index.ts              registerRoot and the Tailwind stylesheet import
  Root.tsx              the GameFilm composition and its default props
  GameFilm.tsx          title / match / standings sequences and the play stage
  tailwind.css          Tailwind entry plus the arena palette as theme tokens
  theme.ts              palette and terrain colours as strings, for the canvas
  faction-label.ts      "harness-model: Civilization" naming, ported from the viewer
  faction-color.ts      OKLab substitution for faction colours that hit terrain
  format.ts             year and count formatting
  dataset/
    schema.ts           narrowing parsers for meta.json and frames.json
    film.ts             resolves frames into per-turn board states and tracks
    geometry.ts         ISO|HEX tile positions, fit transform, hex paths
    load.ts             staticFile fetch, per-page cache, delayRender
  board/draw.ts         the canvas painter
  components/           BoardCanvas, Ticker, ScorePanel, MetricChart, TitleCard, Outro
public/exports/<game>/  dataset copied here by `just video`
```

Everything crossing the JSON boundary is parsed from `unknown` by hand in
`dataset/schema.ts`. There is no `any` and no unchecked cast; a malformed export
fails at load with a specific message instead of rendering ninety seconds of
blank panels.
