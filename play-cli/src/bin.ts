#!/usr/bin/env bun
/**
 * `play` — the player-only Freeciv session client.
 *
 * The bootstrap does one thing: hand `cli-main` to `BunRuntime.runMain` with a
 * teardown that preserves the exit-code contract (0 / 2 / 75 / 66).  Everything
 * else — parsing, the Layer stack, the single error → exit-code mapping — lives
 * in `src/cli-main.ts`.
 */
import { installProgRewrite } from 'src/services/prog-prefix';
import { runMain } from 'src/cli-main';

installProgRewrite();
runMain();
