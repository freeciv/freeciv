import { useMemo } from 'react'
import { interpolate, useCurrentFrame } from 'remotion'
import type { Film, PlayerTrack } from '../dataset/film'
import { buildBoardLayout } from '../dataset/geometry'
import { controllerDisplayName, nationDisplayName } from '../faction-label'
import { eventKindLabel, topHighlights, weightTier } from '../event-log'
import { countNoun, formatDuration } from '../format'
import type { GameEvent } from '../dataset/schema'
import { SHELL, mixColors, withAlpha } from '../theme'
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
const BAND_HEIGHT = 434
const HIGHLIGHTS_WIDTH = 430
const BOARD_WIDTH = STAGE_WIDTH - HIGHLIGHTS_WIDTH - CHART_GAP
const MAX_HIGHLIGHTS = 7
// The three histories share one frame, so their widths have to account for the
// frame's border and the two hairlines between them.
const CHARTS_WIDTH = STAGE_WIDTH - PANEL_WIDTH - CHART_GAP
const CHART_WIDTH = Math.floor((CHARTS_WIDTH - 4) / 3)
const LAST_CHART_WIDTH = CHARTS_WIDTH - 4 - 2 * CHART_WIDTH

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

/** The match's biggest moments, by the extractor's weight. */
function Highlights({
  film, events,
}: {
  readonly film: Film
  readonly events: readonly GameEvent[]
}) {
  const colorFor = (event: GameEvent): string => {
    for (const actor of event.actors) {
      const track = film.tracks.find(
        (candidate) => candidate.player.seatId === actor
          || candidate.player.name === actor,
      )
      if (track) return track.renderColor
    }
    return SHELL.muted
  }
  return (
    <div
      className="flex flex-col gap-[10px] border border-line px-[18px] py-[15px]"
      style={{ width: HIGHLIGHTS_WIDTH }}
    >
      <span className="label">Defining moments</span>
      {events.length === 0 && (
        <span className="font-mono text-[11px] text-muted">
          No events were derived for this run.
        </span>
      )}
      {events.map((event) => {
        const color = colorFor(event)
        const major = weightTier(event.weight) === 'major'
        return (
          <div
            className="flex min-w-0 flex-1 flex-col gap-[4px] pl-[12px]"
            key={`${event.turn}-${event.kind}-${event.summary}`}
            // Rail weight is the beat's weight; a major moment holds more edge.
            style={{ borderLeft: `${major ? 3 : 2}px solid ${major ? color : withAlpha(color, 0.4)}` }}
          >
            <div className="flex items-baseline gap-[10px]">
              <span className="label">T{event.turn}</span>
              <span
                className="font-mono text-[10px] font-medium tracking-[0.16em] uppercase"
                style={{ color }}
              >
                {eventKindLabel(event.kind)}
              </span>
            </div>
            <span
              className={`font-display leading-snug tracking-[-0.01em] ${
                major ? 'text-[15px] text-ink' : 'text-[14px] text-muted'
              }`}
            >
              {event.summary}
            </span>
          </div>
        )
      })}
    </div>
  )
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
      className="flex flex-1 items-stretch gap-[16px]"
      style={{
        background: rank === 1
          ? mixColors(SHELL.panel, track.renderColor, 0.055)
          : SHELL.panel,
      }}
    >
      <span className="w-[5px] shrink-0" style={{ background: track.renderColor }} />
      <div className="flex flex-1 items-center gap-[16px] py-[15px] pr-[20px]">
        <span
          className="w-[26px] font-mono text-[22px] font-medium tabular-nums"
          style={{ color: rank === 1 ? SHELL.ink : SHELL.dim }}
        >
          {rank}
        </span>
        <div className="flex min-w-0 flex-col gap-[5px]">
          <span className="truncate font-display text-[24px] leading-tight tracking-[-0.025em] text-ink">
            {controllerDisplayName(track.player)}
            <span className="text-dim">: </span>
            <span style={{ color: track.renderColor }}>
              {nationDisplayName(track.player)}
            </span>
          </span>
          <span className="font-mono text-[11px] text-muted">
            peak {track.peakScore.toLocaleString('en-US')} · {countNoun(stat?.cities ?? 0, 'city', 'cities')} ·{' '}
            {stat?.techs ?? 0} techs
          </span>
        </div>
        <span className="ml-auto font-mono text-[36px] leading-none font-medium tracking-[-0.04em] text-ink tabular-nums">
          {track.finalScore.toLocaleString('en-US')}
        </span>
      </div>
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
    return Math.round(BOARD_WIDTH / aspect)
  }, [film.meta.height, film.meta.width])

  const wallClock = film.meta.startedAt !== null && film.meta.finishedAt !== null
    ? formatDuration(film.meta.finishedAt - film.meta.startedAt)
    : 'unknown duration'
  const highlights = topHighlights(film.events.events, MAX_HIGHLIGHTS)
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
      className="flex h-full w-full flex-col items-center justify-center gap-[18px] px-[30px] py-[24px]"
      style={{ opacity: fadeIn }}
    >
      <div
        className="flex items-center justify-between border-b border-line pb-[11px]"
        style={{ width: STAGE_WIDTH }}
      >
        <span className="label">Final standings · turn {lastTurn.turn}</span>
        <span className="label">{film.meta.gameId}</span>
      </div>

      <div className="flex gap-[16px]" style={{ width: STAGE_WIDTH }}>
        <div
          className="board-frame overflow-hidden border border-board-edge bg-board"
          style={{ height: boardHeight, width: BOARD_WIDTH }}
        >
          <BoardCanvas
            colorByPlayer={film.colors.colorByPlayer}
            height={boardHeight}
            meta={film.meta}
            reveal={1}
            showLabels
            superSample={superSample}
            turn={lastTurn}
            width={BOARD_WIDTH}
          />
        </div>
        <Highlights events={highlights} film={film} />
      </div>

      <div
        className="flex gap-[16px]"
        style={{ height: BAND_HEIGHT, width: STAGE_WIDTH }}
      >
        <div className="hair-grid flex border border-line" style={{ width: CHARTS_WIDTH }}>
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
            width={LAST_CHART_WIDTH}
          />
        </div>
        <div
          className="hair-grid flex flex-col border border-line"
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
          <div className="flex flex-col gap-[8px] bg-panel px-[20px] py-[18px]">
            <span className="label">Outcome</span>
            <span className="font-display text-[16px] leading-snug tracking-[-0.01em] text-ink">
              {outcomeLine(film)}
            </span>
            <span className="font-mono text-[10px] leading-relaxed text-dim">
              recorded state: {film.meta.state} · {film.meta.controlProtocol} ·{' '}
              {wallClock} of play ·{' '}
              {countNoun(lastTurn.cities.length, 'city', 'cities')} ·{' '}
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
