import type { ReactNode } from 'react'
import { interpolate, spring, useCurrentFrame, useVideoConfig } from 'remotion'
import type { Film, PlayerTrack } from '../dataset/film'
import { controllerDisplayName, nationDisplayName } from '../faction-label'
import { formatDuration } from '../format'
import { HarnessLogo, ProviderLogo, controllerMarks, displayModelName } from '../logos'
import { NationFlag } from '../nation-flag'
import { SHELL } from '../theme'

interface TitleCardProps {
  readonly film: Film
  readonly durationInFrames: number
}

/**
 * One side's cell in a given row.
 *
 * The card is built as rows, not as two independent columns: role, then
 * nation, then the combo name, then the marks. Reading across is the point --
 * "Agent vs CPU: Hard" and "English vs Spanish" are comparisons, and a
 * comparison only works if the two halves sit on the same line. Stacked as
 * columns they drifted, because a demoted CPU side has fewer rows than an
 * agent and nothing forced them back into register.
 *
 * Each side is pulled toward the VS rather than pinned to the frame: at full
 * column width the two halves sat at opposite edges of a 1920 frame with a
 * void between them, which reads as two unrelated captions.
 */
function Cell({
  align, children,
}: {
  readonly align: 'left' | 'right'
  readonly children: ReactNode
}) {
  const rightAligned = align === 'right'
  return (
    <div
      className={`flex min-w-0 items-center gap-[18px] ${
        rightAligned
          ? 'flex-row-reverse justify-self-start text-right'
          : 'justify-self-end text-left'
      }`}
    >
      {children}
    </div>
  )
}

interface Side {
  readonly track: PlayerTrack
  readonly isAgent: boolean
  /** True only when this agent is facing a CPU, never agent against agent. */
  readonly soloAgent: boolean
}

/**
 * The rows, in order. Each returns `null` for a side that has nothing to put
 * in it -- the native seat has no harness, no vendor and no model combo -- and
 * an empty cell simply holds the row's place opposite the side that does.
 */
const ROWS: readonly {
  readonly key: string
  readonly render: (side: Side) => ReactNode
}[] = [
  {
    // What each side *is*, which is the comparison the card exists to make --
    // but only when the sides differ in kind. Against a CPU, "Agent" is the
    // useful word and the model name moves down to the combo row. Agent
    // against agent, both would read "Agent", which compares nothing; there
    // the names are the story and they take the headline.
    key: 'role',
    render: ({ track, isAgent, soloAgent }) => {
      // Agent against agent the headline is the *model*, not the whole
      // controller label. The harness is already stated by the mark below, so
      // "claude-code-claude-opus-5" wraps to two lines in order to say
      // "claude" three times. The model alone is what is being compared.
      const { harness, model } = controllerMarks(track.player.controllerLabel)
      const name = displayModelName(harness, model)
      return (
        <span className="font-display text-[72px] font-normal leading-[0.98] tracking-[-0.03em] text-ink [overflow-wrap:anywhere]">
          {isAgent
            ? (soloAgent ? 'Agent' : name ?? controllerDisplayName(track.player))
            : controllerDisplayName(track.player)}
        </span>
      )
    },
  },
  {
    key: 'nation',
    render: ({ track }) => (
      <>
        {/* The raw ruleset nation, not the display label: the flag lookup is
            an exact match on Freeciv's own nation name, so a decorated string
            ("Spanish (CPU: Hard)") would silently resolve to null. */}
        <NationFlag nation={track.player.nation} size={84} />
        <span className="font-mono text-[26px]" style={{ color: track.renderColor }}>
          {nationDisplayName(track.player)}
        </span>
      </>
    ),
  },
  {
    // Only where the headline is the role; otherwise it would repeat it.
    key: 'combo',
    render: ({ track, isAgent, soloAgent }) => isAgent && soloAgent
      ? (
        <span className="font-mono text-[30px] text-ink [overflow-wrap:anywhere]">
          {controllerDisplayName(track.player)}
        </span>
      )
      : null,
  },
  {
    key: 'marks',
    render: ({ track }) => {
      // Both return null for anything unregistered, so the native seat renders
      // no marks rather than a gap where they would be.
      const { harness, provider } = controllerMarks(track.player.controllerLabel)
      if (harness === null && provider === null) return null
      return (
        <div className="flex items-center gap-[24px]" style={{ color: SHELL.ink }}>
          <HarnessLogo harness={harness} size={62} />
          <ProviderLogo provider={provider} size={62} />
        </div>
      )
    },
  },
]

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
  const sides: Side[] = seats.slice(0, 2).map((track) => ({
    track,
    isAgent: track.player.controllerType !== 'native',
    soloAgent: singlePlayer,
  }))

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
    singlePlayer ? 'Single player' : 'Agent vs agent',
    wallClock,
    `${film.turns.length.toLocaleString('en-US')} turns`,
  ].filter((fact): fact is string => fact !== null)

  return (
    <div
      className="flex h-full w-full flex-col px-[110px] pt-[74px] pb-[66px]"
      style={{ opacity: fadeOut }}
    >
      <div style={{ opacity: rise, transform: `translateY(${(1 - rise) * 20}px)` }}>
        <div className="flex items-baseline justify-between border-b border-line pb-[16px]">
          <span className="label">Freeciv Agent Arena</span>
          <span className="label">{film.meta.gameId}</span>
        </div>
      </div>

      {/*
       * The matchup and its readings are one centred group, not a masthead at
       * the top and a band at each end. The scale of the run reads as a
       * standfirst over the matchup -- "596 turns, fourteen hours" is what the
       * fight was, so it belongs with the fight rather than stranded on the
       * bottom edge of the frame.
       */}
      <div className="flex flex-1 flex-col items-center justify-center gap-[52px]">
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

      {/*
       * Four rows, read across: role, nation, the harness-model combo, and the
       * marks. The VS spans them all.
       *
       * Built as rows rather than as two independent columns because reading
       * across is the point -- "Agent vs CPU: Hard" and "English vs Spanish"
       * are comparisons, and a comparison only works if both halves sit on one
       * line. As columns they drifted out of register, since a demoted CPU
       * side has fewer rows than an agent.
       */}
      <div
        className="flex w-full flex-col"
        style={{ opacity: rise, transform: `translateY(${(1 - rise) * 20}px)` }}
      >
        <div className="grid grid-cols-[minmax(0,1fr)_auto_minmax(0,1fr)] items-center gap-x-[52px] gap-y-[26px]">
          {/* One element spanning every row, so it stays centred on the pair
              however many rows a side turns out to have. Set large: it is the
              hinge the whole card turns on, and at 13px with hairlines above
              and below it read as a separator between two lists instead. */}
          <div
            className="flex items-center justify-center"
            // Spanning the nation and combo rows and centred within them puts
            // the VS on the seam between the two, rather than floating at the
            // midpoint of a stack whose height depends on how many rows the
            // opponent happened to fill.
            style={{ gridColumn: 2, gridRow: '2 / 4' }}
          >
            <span className="font-display text-[76px] font-normal leading-none tracking-[-0.03em] text-muted">
              vs
            </span>
          </div>

          {ROWS.map((row) => sides.map((side, index) => {
            const content = row.render(side)
            const align = index === 0 ? 'left' : 'right'
            return (
              <div
                key={`${row.key}-${side.track.player.playerId}`}
                style={{ gridColumn: index === 0 ? 1 : 3 }}
                className={index === 0 ? 'justify-self-end' : 'justify-self-start'}
              >
                {content === null ? null : <Cell align={align}>{content}</Cell>}
              </div>
            )
          }))}
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
      </div>
    </div>
  )
}
