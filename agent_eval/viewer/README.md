# Freeciv replay viewer

This directory is a self-contained React, TypeScript, Vite, and Tailwind v4
application. The supervisor serves the committed `dist/` output, so production
does not require Node.js.

```sh
npm install
npm run check
npm run dev
```

Vite emits route-relative `../viewer/` asset URLs. A shell at
`PREFIX/watch/GAME_ID` therefore loads assets from `PREFIX/viewer/` without
injecting the game ID or deployment prefix. The application derives both from
the watch path, then incrementally reads:

- `GET /v1/games/{id}/watch.json`
- `GET /v1/games/{id}/replay.json?after_turn=N&limit=250`

## Backend contract

The typed contract is in `src/types.ts`. The important backend guarantees are:

- frame rows carry parsed `turn` and `map_players` from PPM headers;
- map players include `player_id`, `player_name`, and exact `player_color`;
- configured map players may include place/controller identity, while unmatched
  entries remain dynamic factions and never enter the scored leaderboard;
- replay snapshots contain every current Freeciv player, but configured players
  are enriched with place, controller, model, and fixed color;
- player telemetry includes score, cities, citizen population, units, gold,
  culture, government, nation, known/gained/lost technologies, and current
  research bulbs/cost;
- the technology catalog includes numeric prerequisites and dependency depth;
- replay pagination advances monotonically and returns sanitized public fields;
- replay capture warnings are generic/public-safe and never affect benchmark
  validity or the gameplay action barrier.

`src/mock.ts` is a representative contract fixture, including a dynamic Pirate
faction (`Blackbeard`, `#FF1493`) that appears on the map but not in scored
comparisons.
