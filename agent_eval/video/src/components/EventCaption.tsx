import { useMemo } from 'react'
import { interpolate } from 'remotion'
import type { Film } from '../dataset/film'
import type { GameEvent } from '../dataset/schema'
import { eventKindLabel, planCaptions, weightTier, type Caption } from '../event-log'
import { SHELL, withAlpha } from '../theme'

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
  return SHELL.cyan
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
    <div
      className={`pointer-events-none absolute bottom-[16px] left-[16px] flex items-stretch rounded ${
        mustShow ? 'gap-[18px] px-[22px] py-[18px]' : 'gap-[14px] px-[18px] py-[13px]'
      }`}
      style={{
        // Opaque: the board underneath is busy, and a translucent card turns
        // the summary into grey mush exactly when it matters most.
        background: SHELL.panel,
        border: `1px solid ${withAlpha(color, mustShow ? 1 : tier === 'major' ? 0.85 : 0.45)}`,
        boxShadow: mustShow
          ? `0 14px 48px rgba(0,0,0,.7), 0 0 0 1px ${withAlpha(color, 0.35)}, 0 0 34px ${withAlpha(color, 0.22)}`
          : `0 12px 40px rgba(0,0,0,.62)`,
        maxWidth: width - 32,
        opacity,
        transform: `translateY(${rise}px)`,
      }}
    >
      {/* A landmark's accent runs the full height of the card. */}
      <span
        className={`shrink-0 rounded-sm ${mustShow ? 'w-[6px] self-stretch' : 'h-[34px] w-[4px] self-center'}`}
        style={{ background: color }}
      />
      <div className={`flex min-w-0 flex-col ${mustShow ? 'gap-[6px]' : 'gap-[4px]'}`}>
        <div className="flex items-center gap-[10px]">
          <span
            className="font-mono text-[10px] tracking-[1.8px] uppercase"
            style={{ color }}
          >
            {eventKindLabel(event.kind)}
          </span>
          <span className="font-mono text-[10px] tracking-[1.2px] text-muted">
            TURN {event.turn}
          </span>
          {alsoInWindow > 0 && (
            <span className="font-mono text-[10px] tracking-[1.2px] text-muted">
              +{alsoInWindow} more
            </span>
          )}
        </div>
        <span
          className={`truncate font-sans text-ink ${
            mustShow ? 'text-[27px] leading-tight' : tier === 'major' ? 'text-[21px]' : 'text-[18px]'
          }`}
        >
          {captionText(active.caption)}
        </span>
      </div>
    </div>
  )
}
