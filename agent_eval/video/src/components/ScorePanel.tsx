import type { Film, PlayerTrack, TurnState } from '../dataset/film'
import { sampleTrack } from '../dataset/film'
import { controllerDisplayName, nationDisplayName } from '../faction-label'
import { SHELL, withAlpha } from '../theme'
import { MetricChart, type ChartSeries } from './MetricChart'

/** Cities and technologies ride in the rail the match dossier used to occupy. */
const CHART_HEIGHT = 178

interface ScorePanelProps {
  readonly film: Film
  readonly turn: TurnState
  readonly turnIndex: number
  readonly progress: number
  readonly width: number
}

function Sparkline({
  track, turnIndex, width, height,
}: {
  readonly track: PlayerTrack
  readonly turnIndex: number
  readonly width: number
  readonly height: number
}) {
  const upTo = Math.min(turnIndex, track.scores.length - 1)
  const peak = Math.max(1, track.peakScore)
  const total = Math.max(1, track.scores.length - 1)
  const points: string[] = []
  for (let index = 0; index <= upTo; index += 1) {
    const x = (index / total) * width
    const y = height - ((track.scores[index] ?? 0) / peak) * (height - 3) - 1.5
    points.push(`${x.toFixed(2)},${y.toFixed(2)}`)
  }
  return (
    <svg className="block" height={height} width={width}>
      <line
        stroke={withAlpha(SHELL.line, 0.9)}
        strokeWidth={1}
        x1={0}
        x2={width}
        y1={height - 1}
        y2={height - 1}
      />
      {points.length > 1 && (
        <polyline
          fill="none"
          points={points.join(' ')}
          stroke={track.renderColor}
          strokeLinecap="round"
          strokeWidth={1.8}
        />
      )}
    </svg>
  )
}

function StatCell({ label, value }: { readonly label: string; readonly value: string }) {
  return (
    <div className="flex flex-col gap-[3px]">
      <span className="font-mono text-[10px] tracking-[1.4px] text-muted uppercase">
        {label}
      </span>
      <span className="font-mono text-[19px] font-semibold text-ink">{value}</span>
    </div>
  )
}

function PlayerCard({
  track, turn, turnIndex, progress, rank, leaderScore,
}: {
  readonly track: PlayerTrack
  readonly turn: TurnState
  readonly turnIndex: number
  readonly progress: number
  readonly rank: number
  readonly leaderScore: number
}) {
  const stat = turn.statsByPlayer.get(track.player.playerId)
  const score = sampleTrack(track.scores, turnIndex, progress)
  const cities = sampleTrack(track.cities, turnIndex, progress)
  const units = sampleTrack(track.units, turnIndex, progress)
  const alive = stat?.alive !== false
  const researching = stat?.researching ?? ''
  const bulbs = stat?.bulbs ?? 0
  const share = leaderScore > 0 ? Math.min(1, score / leaderScore) : 0

  return (
    <div
      className="relative flex flex-col gap-[10px] rounded bg-panel-2 px-[16px] pt-[14px] pb-[12px]"
      style={{
        border: `1px solid ${rank === 1 ? withAlpha(track.renderColor, 0.55) : SHELL.line}`,
        opacity: alive ? 1 : 0.55,
      }}
    >
      <span
        className="absolute top-0 bottom-0 left-0 w-[4px] rounded-l"
        style={{ background: track.renderColor }}
      />
      <div className="flex items-baseline gap-[8px]">
        <span className="font-mono text-[13px] font-bold tracking-[1.2px] text-ink">
          {controllerDisplayName(track.player)}
          <span className="text-muted">: </span>
          <span style={{ color: track.renderColor }}>
            {nationDisplayName(track.player)}
          </span>
        </span>
        <span className="ml-auto font-mono text-[10px] tracking-[1.2px] text-muted">
          #{rank}
        </span>
      </div>
      <div className="flex items-end gap-[12px]">
        <span className="font-mono text-[46px] leading-[0.95] font-bold tracking-[-1px] text-ink tabular-nums">
          {Math.round(score).toLocaleString('en-US')}
        </span>
        <span className="pb-[6px] font-mono text-[10px] tracking-[1.4px] text-muted">
          SCORE
        </span>
        <div className="ml-auto pb-[4px]">
          <Sparkline height={26} track={track} turnIndex={turnIndex} width={110} />
        </div>
      </div>
      <div className="h-[4px] rounded-sm" style={{ background: withAlpha(SHELL.line, 0.7) }}>
        <div
          className="h-[4px] rounded-sm"
          style={{ background: track.renderColor, width: `${(share * 100).toFixed(2)}%` }}
        />
      </div>
      <div className="grid grid-cols-4 gap-[8px] pt-[2px]">
        <StatCell label="Cities" value={Math.round(cities).toLocaleString('en-US')} />
        <StatCell label="Units" value={Math.round(units).toLocaleString('en-US')} />
        <StatCell label="Techs" value={String(stat?.techs ?? 0)} />
        <StatCell label="Gold" value={String(stat?.gold ?? 0)} />
      </div>
      <div className="flex items-baseline justify-between gap-[10px] border-t border-line pt-[8px]">
        <span className="font-mono text-[10px] tracking-[1.4px] text-muted uppercase">
          Researching
        </span>
        <span className="min-w-0 flex-1 truncate text-right font-mono text-[12px] text-ink">
          {researching || '—'}
        </span>
        <span className="font-mono text-[11px] text-muted tabular-nums">
          {bulbs > 0 ? `${Math.round(bulbs).toLocaleString('en-US')} bulbs` : ''}
        </span>
      </div>
      <div className="flex justify-between gap-[10px] font-mono text-[10px] tracking-[1px] text-muted uppercase">
        <span>{alive ? stat?.government || 'anarchy' : 'eliminated'}</span>
        <span>{alive ? `${stat?.citizens ?? 0} citizens` : 'no cities remain'}</span>
      </div>
    </div>
  )
}

export function ScorePanel({
  film, turn, turnIndex, progress, width,
}: ScorePanelProps) {
  const ranked = [...film.seatTracks].sort((left, right) => {
    const leftScore = left.scores[turnIndex] ?? 0
    const rightScore = right.scores[turnIndex] ?? 0
    return rightScore - leftScore
  })
  const leaderScore = Math.max(
    1, ...film.seatTracks.map((track) => track.scores[turnIndex] ?? 0),
  )
  const series = (pick: (track: PlayerTrack) => readonly number[]): ChartSeries[] =>
    film.seatTracks.map((track) => ({
      key: track.player.playerId,
      color: track.renderColor,
      values: pick(track),
    }))
  const thirdParties = film.tracks.filter(
    (track) => !track.player.seat
      && (turn.statsByPlayer.get(track.player.playerId)?.cities ?? 0) > 0,
  )

  return (
    <div className="flex h-full flex-col gap-[12px]" style={{ width }}>
      <div className="flex items-center justify-between border-b border-line pb-[8px] font-mono text-[10px] tracking-[2px] text-muted">
        <span>STANDINGS</span>
        <span>{film.meta.controlProtocol.toUpperCase()}</span>
      </div>
      {ranked.map((track, index) => (
        <PlayerCard
          key={track.player.playerId}
          leaderScore={leaderScore}
          progress={progress}
          rank={index + 1}
          track={track}
          turn={turn}
          turnIndex={turnIndex}
        />
      ))}
      <MetricChart
        firstTurn={film.meta.firstTurn}
        height={CHART_HEIGHT}
        label="Cities"
        progress={progress}
        series={series((track) => track.cities)}
        showTurnAxis={false}
        totalTurns={film.turns.length}
        turnIndex={turnIndex}
        width={width}
      />
      <MetricChart
        firstTurn={film.meta.firstTurn}
        height={CHART_HEIGHT}
        label="Technologies"
        progress={progress}
        series={series((track) => track.techs)}
        showTurnAxis={false}
        totalTurns={film.turns.length}
        turnIndex={turnIndex}
        width={width}
      />
      <div className="flex-1" />
      {thirdParties.length > 0 && (
        <div className="flex items-center gap-[10px] rounded border border-line px-[12px] py-[8px] font-mono text-[11px] tracking-[1px] text-muted">
          <span className="tracking-[1.6px]">THIRD PARTY</span>
          {thirdParties.map((track) => (
            <span key={track.player.playerId} style={{ color: track.renderColor }}>
              {nationDisplayName(track.player)} ·{' '}
              {turn.statsByPlayer.get(track.player.playerId)?.cities ?? 0} cities
            </span>
          ))}
        </div>
      )}
    </div>
  )
}
