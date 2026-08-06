import { interpolate, spring, useCurrentFrame, useVideoConfig } from 'remotion'
import type { Film, PlayerTrack } from '../dataset/film'
import { controllerDisplayName, factionDisplayLabel, nationDisplayName } from '../faction-label'
import { formatDuration } from '../format'
import { withAlpha } from '../theme'

interface TitleCardProps {
  readonly film: Film
  readonly durationInFrames: number
}

function MetaRow({ label, value }: { readonly label: string; readonly value: string }) {
  return (
    <div className="flex gap-[18px]">
      <span className="min-w-[168px] font-mono text-[12px] tracking-[2px] text-muted uppercase">
        {label}
      </span>
      <span className="font-mono text-[14px] text-ink">{value}</span>
    </div>
  )
}

/** "pi-gpt-5.6-sol: English", the civilization tinted with its faction colour. */
function Contender({ track }: { readonly track: PlayerTrack }) {
  return (
    <div className="flex items-center gap-[14px]">
      <span
        className="h-[46px] w-[6px] shrink-0 rounded-sm"
        style={{ background: track.renderColor }}
      />
      <span className="font-serif text-[54px] leading-tight text-ink">
        {controllerDisplayName(track.player)}
        <span className="text-muted">: </span>
        <span style={{ color: track.renderColor }}>{nationDisplayName(track.player)}</span>
      </span>
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
    <div className="flex h-full w-full items-center justify-center" style={{ opacity: fadeOut }}>
      <div
        className="flex w-[1280px] flex-col gap-[28px]"
        style={{ opacity: rise, transform: `translateY(${(1 - rise) * 22}px)` }}
      >
        <span className="font-mono text-[13px] tracking-[6px] text-cyan">
          FREECIV AGENT ARENA
        </span>
        <div className="flex flex-col gap-[8px]">
          {seats.map((track, index) => (
            <div className="flex flex-col gap-[8px]" key={track.player.playerId}>
              {index > 0 && (
                <span className="pl-[22px] font-mono text-[17px] font-bold tracking-[5px] text-cyan">
                  VS
                </span>
              )}
              <Contender track={track} />
            </div>
          ))}
        </div>
        <div className="flex gap-[18px] pt-[4px]">
          {seats.map((track) => (
            <div
              className="flex flex-1 flex-col gap-[6px] rounded bg-panel px-[20px] py-[16px]"
              key={track.player.playerId}
              style={{ border: `1px solid ${withAlpha(track.renderColor, 0.5)}` }}
            >
              <span
                className="font-mono text-[13px] font-bold tracking-[2px]"
                style={{ color: track.renderColor }}
              >
                {nationDisplayName(track.player).toUpperCase()}
              </span>
              <span className="font-sans text-[18px] text-ink">
                {controllerDisplayName(track.player)}
              </span>
              <span className="font-mono text-[12px] text-muted">
                seat {track.player.seatId ?? '—'}
              </span>
            </div>
          ))}
        </div>
        <div className="flex flex-col gap-[9px] border-t border-line pt-[20px]">
          <MetaRow label="Game" value={film.meta.gameId} />
          <MetaRow
            label="Protocol"
            value={`${film.meta.controlProtocol} · ruleset ${film.meta.ruleset}`}
          />
          <MetaRow
            label="World"
            value={`${film.meta.width} x ${film.meta.height} ${film.meta.topology} · seed ${film.meta.seeds[0] ?? 'n/a'}`}
          />
          <MetaRow
            label="Match"
            value={`${film.turns.length} turns · board snapshots on ${(film.meta.boardDensity * 100).toFixed(0)}% of turns`}
          />
          <MetaRow label="Outcome" value={`${film.meta.state} after ${wallClock} of wall clock`} />
          {winner && (
            <MetaRow
              label="Final"
              value={`${factionDisplayLabel(winner.player)} — ${winner.finalScore.toLocaleString('en-US')} points`}
            />
          )}
        </div>
      </div>
    </div>
  )
}
