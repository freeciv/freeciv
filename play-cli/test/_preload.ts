/**
 * Test-runner default: parity mode.
 *
 * The golden assertions pin CPython's exact output, which spells every command
 * mention `just <verb>`.  `writeMirror` (and, in spawned-CLI tests, the stdout
 * patch) consult PLAY_PROG per call, so the runner pins parity here once;
 * tests that exercise the `./play` cutover set the variable explicitly around
 * their calls and restore it.
 */
process.env['PLAY_PROG'] ??= 'just';
