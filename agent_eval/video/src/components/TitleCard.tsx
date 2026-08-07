import { interpolate, spring, useCurrentFrame, useVideoConfig } from 'remotion'
import type { Film, PlayerTrack } from '../dataset/film'
import { controllerDisplayName, factionDisplayLabel, nationDisplayName } from '../faction-label'
import { formatDuration } from '../format'

interface TitleCardProps {
  readonly film: Film
  readonly durationInFrames: number
}

/** One cell of the collapsed metadata grid: a chrome label over its reading. */
function MetaCell({ label, value }: { readonly label: string; readonly value: string }) {
  return (
    <div className="flex flex-col gap-[7px] bg-page px-[22px] py-[16px]">
      <span className="label">{label}</span>
      <span className="truncate font-mono text-[14px] text-ink">{value}</span>
    </div>
  )
}

/** "pi-gpt-5.6-sol: Babylonian", the civilization tinted with its faction colour. */
function Contender({ track }: { readonly track: PlayerTrack }) {
  return (
    <div className="flex items-stretch gap-[26px]">
      <span className="w-[6px] shrink-0" style={{ background: track.renderColor }} />
      <div className="flex min-w-0 flex-1 flex-col gap-[14px]">
        <span className="font-display text-[92px] leading-[0.94] font-normal tracking-[-0.035em] text-ink">
          {controllerDisplayName(track.player)}
          <span className="text-muted">: </span>
          <span style={{ color: track.renderColor }}>{nationDisplayName(track.player)}</span>
        </span>
        <span className="label">seat {track.player.seatId ?? '—'}</span>
      </div>
    </div>
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
  const winner = [...seats].sort((left, right) => right.finalScore - left.finalScore)[0]
  const wallClock = film.meta.startedAt !== null && film.meta.finishedAt !== null
    ? formatDuration(film.meta.finishedAt - film.meta.startedAt)
    : 'unknown'

  return (
    /*
     * Three bands anchored to the frame -- masthead, card, dossier -- rather
     * than one small block floating in the middle of 1080 lines of black. The
     * empty space between them is measured, which is what makes it read as
     * composition instead of as a gap.
     */
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

      {/* Each side owns a full-width band, so the card reads as ruled sheet
          rather than as a short line of type adrift in the middle of the frame. */}
      <div
        className="flex flex-col"
        style={{ opacity: rise, transform: `translateY(${(1 - rise) * 20}px)` }}
      >
        {seats.map((track, index) => (
          <div className="flex flex-col" key={track.player.playerId}>
            {index > 0 && (
              <div className="flex items-center gap-[22px] py-[46px]">
                <span className="h-px w-[46px] bg-line" />
                <span className="font-mono text-[12px] font-medium tracking-[0.42em] text-muted">
                  VS
                </span>
                <span className="h-px flex-1 bg-line" />
              </div>
            )}
            <Contender track={track} />
          </div>
        ))}
        <span className="mt-[46px] h-px bg-line" />
      </div>

      {/* One tinted parent, 1px gaps: the rules between readings are shared,
          not a gutter between floating cards. */}
      <div
        className="hair-grid grid grid-cols-3 border border-line"
        style={{ opacity: rise, transform: `translateY(${(1 - rise) * 20}px)` }}
      >
        <MetaCell label="Game" value={film.meta.gameId} />
        <MetaCell
          label="World"
          value={`${film.meta.width} x ${film.meta.height} ${film.meta.topology} · seed ${film.meta.seeds[0] ?? 'n/a'}`}
        />
        <MetaCell
          label="Match"
          value={`${film.turns.length} turns · board on ${(film.meta.boardDensity * 100).toFixed(0)}% of turns`}
        />
        <MetaCell
          label="Protocol"
          value={`${film.meta.controlProtocol} · ruleset ${film.meta.ruleset}`}
        />
        <MetaCell label="Outcome" value={`${film.meta.state} after ${wallClock} of wall clock`} />
        <MetaCell
          label="Final"
          value={winner
            ? `${factionDisplayLabel(winner.player)} — ${winner.finalScore.toLocaleString('en-US')} points`
            : '—'}
        />
      </div>
    </div>
  )
}
