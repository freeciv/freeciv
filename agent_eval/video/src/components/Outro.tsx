import { useMemo } from 'react'
import { interpolate, useCurrentFrame } from 'remotion'
import type { Film, PlayerTrack } from '../dataset/film'
import { buildBoardLayout } from '../dataset/geometry'
import { controllerDisplayName, nationDisplayName } from '../faction-label'
import { formatDuration } from '../format'
import { SHELL, withAlpha } from '../theme'
import { BoardCanvas } from './BoardCanvas'
import { MetricChart, type ChartSeries } from './MetricChart'

interface OutroProps {
  readonly film: Film
  readonly superSample: number
}

// The end screen is a full-bleed final board over a band of summaries. The
// board's 2.59:1 aspect fixes its height, and the band takes what is left.
const STAGE_WIDTH = 1700
const PANEL_WIDTH = 620
const CHART_GAP = 16
const CHART_WIDTH = Math.round((STAGE_WIDTH - PANEL_WIDTH - CHART_GAP - 2 * CHART_GAP) / 3)
const BAND_HEIGHT = 308

/**
 * How the match ended, in the archive's own words.
 *
 * The manifest records no victory condition for these runs, so nothing is
 * invented: a recorded error string is the outcome, otherwise the terminal
 * state is stated plainly and the scores speak for themselves.
 */
function outcomeLine(film: Film): string {
  const error = film.meta.error?.trim()
  if (error) return error
  const state = film.meta.state || 'unknown'
  return `Match ended in state "${state}" with no recorded victory condition.`
}

function StandingRow({
  track, rank, film,
}: {
  readonly track: PlayerTrack
  readonly rank: number
  readonly film: Film
}) {
  const lastTurn = film.turns[film.turns.length - 1]
  const stat = lastTurn?.statsByPlayer.get(track.player.playerId)
  return (
    <div
      className="flex items-center gap-[16px] rounded bg-panel px-[18px] py-[14px]"
      style={{
        border: `1px solid ${
          rank === 1 ? withAlpha(track.renderColor, 0.6) : SHELL.line
        }`,
      }}
    >
      <span
        className="w-[30px] font-mono text-[22px] font-bold"
        style={{ color: rank === 1 ? SHELL.amber : SHELL.muted }}
      >
        {rank}
      </span>
      <span
        className="h-[30px] w-[5px] rounded-sm"
        style={{ background: track.renderColor }}
      />
      <div className="flex min-w-0 flex-col gap-[3px]">
        <span className="truncate font-serif text-[22px] text-ink">
          {controllerDisplayName(track.player)}
          <span className="text-muted">: </span>
          <span style={{ color: track.renderColor }}>
            {nationDisplayName(track.player)}
          </span>
        </span>
        <span className="font-mono text-[11px] text-muted">
          peak {track.peakScore.toLocaleString('en-US')} · {stat?.cities ?? 0} cities ·{' '}
          {stat?.techs ?? 0} techs
        </span>
      </div>
      <span className="ml-auto font-mono text-[34px] font-bold text-ink tabular-nums">
        {track.finalScore.toLocaleString('en-US')}
      </span>
    </div>
  )
}

export function Outro({ film, superSample }: OutroProps) {
  const frame = useCurrentFrame()
  const fadeIn = interpolate(frame, [0, 20], [0, 1], {
    extrapolateLeft: 'clamp', extrapolateRight: 'clamp',
  })
  const boardHeight = useMemo(() => {
    const layout = buildBoardLayout(film.meta.width, film.meta.height)
    const aspect = (layout.bounds.maxX - layout.bounds.minX)
      / (layout.bounds.maxY - layout.bounds.minY)
    return Math.round(STAGE_WIDTH / aspect)
  }, [film.meta.height, film.meta.width])

  const wallClock = film.meta.startedAt !== null && film.meta.finishedAt !== null
    ? formatDuration(film.meta.finishedAt - film.meta.startedAt)
    : 'unknown duration'
  const lastIndex = film.turns.length - 1
  const lastTurn = film.turns[lastIndex]
  const ranked = [...film.seatTracks].sort(
    (left, right) => right.finalScore - left.finalScore,
  )
  const series = (pick: (track: PlayerTrack) => readonly number[]): ChartSeries[] =>
    film.seatTracks.map((track) => ({
      key: track.player.playerId,
      color: track.renderColor,
      values: pick(track),
    }))
  const peak = (pick: (track: PlayerTrack) => readonly number[]): number =>
    film.seatTracks.reduce(
      (best, track) => Math.max(best, ...pick(track)), 1,
    )

  if (!lastTurn) return null

  return (
    <div
      className="flex h-full w-full flex-col items-center gap-[16px] px-[30px] py-[24px]"
      style={{ opacity: fadeIn }}
    >
      <div
        className="flex items-center justify-between border-b border-line pb-[10px] font-mono text-[11px] tracking-[3px] text-muted"
        style={{ width: STAGE_WIDTH }}
      >
        <span className="text-cyan">FINAL STANDINGS · TURN {lastTurn.turn}</span>
        <span>{film.meta.gameId}</span>
      </div>

      <div
        className="board-frame overflow-hidden rounded border border-board-edge bg-board"
        style={{ height: boardHeight, width: STAGE_WIDTH }}
      >
        <BoardCanvas
          colorByPlayer={film.colors.colorByPlayer}
          height={boardHeight}
          meta={film.meta}
          reveal={1}
          showLabels
          superSample={superSample}
          turn={lastTurn}
          width={STAGE_WIDTH}
        />
      </div>

      <div
        className="flex gap-[16px]"
        style={{ height: BAND_HEIGHT, width: STAGE_WIDTH }}
      >
        <MetricChart
          ceiling={peak((track) => track.scores)}
          firstTurn={film.meta.firstTurn}
          height={BAND_HEIGHT}
          label="Score"
          progress={0}
          series={series((track) => track.scores)}
          totalTurns={film.turns.length}
          turnIndex={lastIndex}
          width={CHART_WIDTH}
        />
        <MetricChart
          ceiling={peak((track) => track.cities)}
          firstTurn={film.meta.firstTurn}
          height={BAND_HEIGHT}
          label="Cities"
          progress={0}
          series={series((track) => track.cities)}
          totalTurns={film.turns.length}
          turnIndex={lastIndex}
          width={CHART_WIDTH}
        />
        <MetricChart
          ceiling={peak((track) => track.techs)}
          firstTurn={film.meta.firstTurn}
          height={BAND_HEIGHT}
          label="Technologies"
          progress={0}
          series={series((track) => track.techs)}
          totalTurns={film.turns.length}
          turnIndex={lastIndex}
          width={CHART_WIDTH}
        />
        <div
          className="flex flex-col gap-[10px]"
          style={{ width: PANEL_WIDTH }}
        >
          {ranked.map((track, index) => (
            <StandingRow
              film={film}
              key={track.player.playerId}
              rank={index + 1}
              track={track}
            />
          ))}
          <div className="flex flex-1 flex-col justify-center gap-[6px] rounded border border-line px-[18px] py-[12px]">
            <span className="font-mono text-[10px] tracking-[2px] text-muted">OUTCOME</span>
            <span className="font-sans text-[15px] leading-snug text-ink">
              {outcomeLine(film)}
            </span>
            <span className="font-mono text-[10px] text-muted">
              recorded state: {film.meta.state} · {film.meta.controlProtocol} ·{' '}
              {wallClock} of play ·{' '}
              {lastTurn.cities.length.toLocaleString('en-US')} cities ·{' '}
              {lastTurn.units
                .reduce((total, stack) => total + (stack[3] ?? 0), 0)
                .toLocaleString('en-US')}{' '}
              units on the final board
            </span>
          </div>
        </div>
      </div>
    </div>
  )
}
