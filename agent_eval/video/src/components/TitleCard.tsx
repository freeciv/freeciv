import { interpolate, spring, useCurrentFrame, useVideoConfig } from 'remotion'
import type { Film, PlayerTrack } from '../dataset/film'
import { controllerDisplayName, nationDisplayName } from '../faction-label'
import { formatDuration } from '../format'
import { HarnessLogo, ProviderLogo, controllerMarks } from '../logos'
import { NationFlag } from '../nation-flag'
import { SHELL } from '../theme'

interface TitleCardProps {
  readonly film: Film
  readonly durationInFrames: number
}

/**
 * A side of the matchup.
 *
 * `scale` is the whole argument of this component. In a single-player match
 * the opponent is Freeciv's own AI, and giving it equal billing states that
 * the built-in AI is half the story -- it is not. The story is which model
 * played, and how far it got. So the agent is set at full size and the CPU is
 * demoted to a supporting line. In an agent-vs-agent match both sides are the
 * story and both are set large.
 */
function Contender({
  track, scale, align,
}: {
  readonly track: PlayerTrack
  readonly scale: 'hero' | 'support'
  readonly align: 'left' | 'right'
}) {
  const hero = scale === 'hero'
  const rightAligned = align === 'right'
  // Both return null for anything unregistered, so the native seat -- which
  // has no harness and no vendor -- simply renders no marks rather than a gap.
  const { harness, provider } = controllerMarks(track.player.controllerLabel)
  const marks = harness !== null || provider !== null
  return (
    <div
      className={`flex min-w-0 items-stretch ${hero ? 'gap-[22px]' : 'gap-[16px]'} ${
        rightAligned ? 'flex-row-reverse' : ''
      }`}
    >
      {/* The rail is on the outer edge of each side, so the pair reads as two
          columns squared off against the frame rather than as two rows. */}
      <span
        className={`shrink-0 ${hero ? 'w-[6px]' : 'w-[4px]'}`}
        style={{ background: track.renderColor }}
      />
      <div
        className={`flex min-w-0 flex-1 flex-col gap-[10px] ${
          rightAligned ? 'items-end text-right' : 'items-start'
        }`}
      >
        {/* Who built it and who trained it, above the name. Muted, because
            these identify the contender rather than announce it. */}
        {marks && (
          <div
            className={`flex items-center gap-[24px] ${rightAligned ? 'flex-row-reverse' : ''}`}
            style={{ color: SHELL.ink }}
          >
            <HarnessLogo harness={harness} size={hero ? 68 : 46} />
            <ProviderLogo provider={provider} size={hero ? 68 : 46} />
          </div>
        )}
        <span
          className={`font-display font-normal text-ink [overflow-wrap:anywhere] ${
            hero
              // Side by side each name gets under half the frame, and model
              // names run long ("claude-code-claude-opus-5"), so this is sized
              // to let the longest real name set on two lines. Smaller than
              // this and short names strand themselves at the outer edges of a
              // 1920 frame with a void between them.
              ? 'text-[78px] leading-[0.96] tracking-[-0.032em]'
              : 'text-[44px] leading-[1.04] tracking-[-0.022em]'
          }`}
        >
          {controllerDisplayName(track.player)}
        </span>
        {/*
         * The civilization is a subordinate line, not half the headline. It is
         * kept because it is the only thing tying a name to the colour you are
         * about to watch move across the map -- but nobody outside the game
         * reads "English" as the interesting half of "GPT-5.6: English".
         */}
        <div
          className={`flex items-center gap-[18px] ${rightAligned ? 'flex-row-reverse' : ''}`}
        >
          {/* The raw ruleset nation, not the display label: the flag lookup is
              an exact match on Freeciv's own nation name, so a decorated
              string ("Spanish (CPU: Hard)") would silently resolve to null. */}
          <NationFlag nation={track.player.nation} size={hero ? 92 : 64} />
          <span
            className={`font-mono ${hero ? 'text-[26px]' : 'text-[19px]'}`}
            style={{ color: track.renderColor }}
          >
            {nationDisplayName(track.player)}
          </span>
        </div>
      </div>
    </div>
  )
}

/** One reading in the bottom band. */
function Fact({ value }: { readonly value: string }) {
  return (
    <span className="font-mono text-[15px] font-medium tracking-[0.14em] uppercase text-muted">
      {value}
    </span>
  )
}

export function TitleCard({ film, durationInFrames }: TitleCardProps) {
  const frame = useCurrentFrame()
  const { fps } = useVideoConfig()
  const rise = spring({ frame, fps, config: { damping: 200 }, durationInFrames: 34 })
  const fadeOut = interpolate(
    frame, [durationInFrames - 16, durationInFrames], [1, 0],
    { extrapolateLeft: 'clamp', extrapolateRight: 'clamp' },
  )

  const seats = film.seatTracks
  const agents = seats.filter((track) => track.player.controllerType !== 'native')
  const singlePlayer = agents.length === 1 && seats.length > 1

  const wallClock = film.meta.startedAt !== null && film.meta.finishedAt !== null
    ? formatDuration(film.meta.finishedAt - film.meta.startedAt)
    : null

  /*
   * Scale, elapsed time, and who the opponent is. Deliberately NOT the
   * outcome: the card used to carry "cancelled after 14h" and "In-game Deity
   * AI -- 2,276 points", which answered the only question the film exists to
   * answer, in its first two seconds. The result belongs to the Outro. What is
   * left is the promise -- this ran for 596 turns and fourteen hours -- and
   * every reading that was internal vocabulary (game id, map seed, ISO|HEX,
   * the control-protocol version, the ruleset name, board density) is gone.
   */
  const facts = [
    `${film.turns.length.toLocaleString('en-US')} turns`,
    wallClock,
    singlePlayer ? 'Single player' : 'Agent vs agent',
  ].filter((fact): fact is string => fact !== null)

  return (
    <div
      className="flex h-full w-full flex-col justify-between px-[110px] pt-[74px] pb-[66px]"
      style={{ opacity: fadeOut }}
    >
      <div style={{ opacity: rise, transform: `translateY(${(1 - rise) * 20}px)` }}>
        <div className="flex items-baseline justify-between border-b border-line pb-[16px]">
          <span className="label">Freeciv Agent Arena</span>
          <span className="label">{film.meta.gameId}</span>
        </div>
      </div>

      {/*
       * Left against right, not stacked. A matchup is two sides facing each
       * other; stacking them made the card read as a list, and left the whole
       * right half of a 1920-wide frame empty. The rule and the VS between the
       * columns is the fight.
       */}
      <div
        className="flex flex-col"
        style={{ opacity: rise, transform: `translateY(${(1 - rise) * 20}px)` }}
      >
        <div className="grid grid-cols-[minmax(0,1fr)_auto_minmax(0,1fr)] items-center gap-[52px]">
          {seats.slice(0, 2).map((track, index) => {
            const isAgent = track.player.controllerType !== 'native'
            const side = (
              <Contender
                align={index === 0 ? 'left' : 'right'}
                key={track.player.playerId}
                scale={singlePlayer && !isAgent ? 'support' : 'hero'}
                track={track}
              />
            )
            if (index === 0) return side
            return [
              <div className="flex flex-col items-center gap-[14px]" key="versus">
                <span className="h-[60px] w-px bg-line" />
                <span className="font-mono text-[12px] font-medium tracking-[0.42em] text-muted">
                  VS
                </span>
                <span className="h-[60px] w-px bg-line" />
              </div>,
              side,
            ]
          })}
        </div>
        {/* Free-for-all games can seat more than two. They are named plainly
            below the pair rather than forced into a two-column composition. */}
        {seats.length > 2 && (
          <div className="mt-[34px] flex flex-wrap items-center gap-x-[22px] gap-y-[10px]">
            <span className="label">Also seated</span>
            {seats.slice(2).map((track) => (
              <span
                className="font-mono text-[15px]"
                key={track.player.playerId}
                style={{ color: track.renderColor }}
              >
                {controllerDisplayName(track.player)}
              </span>
            ))}
          </div>
        )}
        <span className="mt-[46px] h-px bg-line" />
      </div>

      {/* One line of readings, spaced by rules rather than boxed into a grid:
          three facts do not need six cells and a border around each. */}
      <div
        className="flex items-center gap-[26px]"
        style={{ opacity: rise, transform: `translateY(${(1 - rise) * 20}px)` }}
      >
        {facts.map((fact, index) => (
          <div className="flex items-center gap-[26px]" key={fact}>
            {index > 0 && <span className="h-[13px] w-px bg-line" />}
            <Fact value={fact} />
          </div>
        ))}
      </div>
    </div>
  )
}
