/**
 * Geometry shared between the match stage and the end screen.
 *
 * These live here rather than in either component because the handoff between
 * the two depends on them agreeing. The end screen opens on the match's board
 * width and animates to its own, so the two acts read as one camera move
 * instead of a dissolve -- and if the numbers drifted apart, the board would
 * jump on the cut instead.
 */

/** The board's width on the match stage. */
export const MATCH_BOARD_WIDTH = 1316

/** The board's width on the end screen. */
export const OUTRO_BOARD_WIDTH = 1254

/**
 * How far the board has to travel down-screen at the handoff.
 *
 * The match seats the board under the top bar and the ticker; the end screen
 * centres it in a taller frame. Measured, not derived: the board's top edge is
 * at y=221 on the match's last frame and y=80 on the end screen's first, so
 * the end screen starts it 141px lower and eases the offset out. Without this
 * the width animates smoothly and the board still jumps.
 */
export const BOARD_HANDOFF_RISE = 141

/** Frames the end screen spends settling from the match layout into its own. */
export const OUTRO_SETTLE_FRAMES = 26
