import { useMemo } from 'react'
import { interpolate } from 'remotion'
import type { Film } from '../dataset/film'
import type { GameEvent } from '../dataset/schema'
import { eventKindLabel, planCaptions, weightTier, type Caption } from '../event-log'
import { SHELL, mixColors, withAlpha } from '../theme'

interface EventCaptionProps {
  readonly film: Film
  /** Frames elapsed inside the match sequence. */
  readonly frame: number
  readonly framesPerTurn: number
  readonly fps: number
  readonly width: number
}

const HOLD_SECONDS = 2.6
/** A guaranteed beat earns more time on screen than a routine one. */
const LANDMARK_HOLD_SECONDS = 4.2
const FADE_FRAMES = 9

/** Faction colour for whoever the event names, using the film's render plan. */
function actorColor(film: Film, event: GameEvent): string {
  for (const actor of event.actors) {
    const track = film.tracks.find(
      (candidate) => candidate.player.seatId === actor
        || candidate.player.name === actor,
    )
    if (track) return track.renderColor
  }
  // Nobody the film tracks owns this beat, so it gets chrome rather than a
  // colour that would read as a faction it does not belong to.
  return SHELL.muted
}

/**
 * A burst of one kind inside a window reads as a count, not a list, so a busy
 * stretch of the war never turns into a wall of near-identical lines.
 */
function captionText(caption: Caption): string {
  const { event, sameKindInWindow } = caption
  // A guaranteed beat always keeps its own words. Collapsing "captured the
  // capital London" into "2 cities captured" throws away the moment the
  // caption exists to mark.
  if (caption.mustShow) return event.summary
  if (sameKindInWindow > 1 && event.kind === 'city_captured') {
    return `${sameKindInWindow} cities captured`
  }
  if (sameKindInWindow > 1 && event.kind === 'city_founded') {
    return `${sameKindInWindow} cities founded`
  }
  return event.summary
}

export function EventCaption({
  film, frame, framesPerTurn, fps, width,
}: EventCaptionProps) {
  const plan = useMemo(
    () => planCaptions(film.events.events, {
      turnsPerSecond: fps / Math.max(1, framesPerTurn),
      holdSeconds: HOLD_SECONDS,
      landmarkHoldSeconds: LANDMARK_HOLD_SECONDS,
    }),
    [film.events.events, fps, framesPerTurn],
  )
  const firstTurn = film.meta.firstTurn

  // The caption whose display turn the timeline has most recently passed. The
  // plan already caps each hold so at most one is live at a time.
  const active = useMemo(() => {
    let found: { caption: Caption; startFrame: number; holdFrames: number } | null = null
    for (const caption of plan.captions) {
      const startFrame = (caption.displayTurn - firstTurn) * framesPerTurn
      const holdFrames = Math.round(caption.holdTurns * framesPerTurn)
      if (startFrame > frame) break
      if (frame < startFrame + holdFrames) found = { caption, startFrame, holdFrames }
    }
    return found
  }, [firstTurn, frame, framesPerTurn, plan.captions])

  if (!active) return null

  const { holdFrames } = active
  const local = frame - active.startFrame
  const fade = Math.min(FADE_FRAMES, Math.floor(holdFrames / 3))
  const opacity = interpolate(
    local,
    [0, fade, holdFrames - fade, holdFrames],
    [0, 1, 1, 0],
    { extrapolateLeft: 'clamp', extrapolateRight: 'clamp' },
  )
  const rise = interpolate(local, [0, FADE_FRAMES], [10, 0], {
    extrapolateLeft: 'clamp', extrapolateRight: 'clamp',
  })
  const { event, alsoInWindow, mustShow } = active.caption
  const color = actorColor(film, event)
  const tier = weightTier(event.weight)

  return (
    /*
     * A lower third seated in the board's own corner, not a card hovering over
     * it. Sitting flush means the frame's edges hold it in place and it needs
     * no drop shadow to separate; the faction glow that used to do that work is
     * gone, and rank now reads from rail width, type size and padding.
     */
    <div
      className={`pointer-events-none absolute bottom-0 left-0 flex items-stretch ${
        mustShow ? 'gap-[20px] pr-[28px]' : 'gap-[16px] pr-[22px]'
      }`}
      style={{
        // Opaque: the board underneath is busy, and a translucent card turns
        // the summary into grey mush exactly when it matters most.
        background: mustShow ? mixColors(SHELL.panel, color, 0.045) : SHELL.panel,
        borderTop: `1px solid ${withAlpha(color, mustShow ? 0.5 : tier === 'major' ? 0.34 : 0.2)}`,
        borderRight: `1px solid ${withAlpha(color, mustShow ? 0.5 : tier === 'major' ? 0.34 : 0.2)}`,
        maxWidth: width - 40,
        opacity,
        transform: `translateY(${rise}px)`,
      }}
    >
      {/* Rail width is the rank: a landmark owns more of the edge. */}
      <span
        className={`shrink-0 self-stretch ${mustShow ? 'w-[9px]' : 'w-[4px]'}`}
        style={{ background: color }}
      />
      <div
        className={`flex min-w-0 flex-col ${
          mustShow ? 'gap-[9px] py-[20px]' : 'gap-[6px] py-[14px]'
        }`}
      >
        <div className="flex items-center gap-[14px]">
          <span
            className="font-mono text-[10px] font-medium tracking-[0.16em] uppercase"
            style={{ color }}
          >
            {eventKindLabel(event.kind)}
          </span>
          <span className="label">turn {event.turn}</span>
          {alsoInWindow > 0 && (
            <span className="label">+{alsoInWindow} more</span>
          )}
        </div>
        <span
          className={`truncate font-display text-ink ${
            mustShow
              ? 'text-[33px] leading-[1.1] tracking-[-0.02em]'
              : tier === 'major'
                ? 'text-[23px] tracking-[-0.015em]'
                : 'text-[20px] tracking-[-0.01em]'
          }`}
        >
          {captionText(active.caption)}
        </span>
      </div>
    </div>
  )
}
