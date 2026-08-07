/**
 * The composition: a title card, a turn-by-turn sweep of the synthetic board
 * with the live standings panel, then the final leaderboard.
 */

import { useMemo } from 'react'
import { AbsoluteFill, Sequence, interpolate, useCurrentFrame, useVideoConfig } from 'remotion'
import { BoardCanvas } from './components/BoardCanvas'
import { DitherWave } from './components/DitherWave'
import { EventCaption } from './components/EventCaption'
import { Outro } from './components/Outro'
import { MetricChart } from './components/MetricChart'
import { ScorePanel } from './components/ScorePanel'
import { Ticker } from './components/Ticker'
import { TitleCard } from './components/TitleCard'
import type { Film, PlayerTrack } from './dataset/film'
import { buildBoardLayout } from './dataset/geometry'
import { useFilm } from './dataset/load'
// Registers Archivo and JetBrains Mono from `public/fonts` and holds the first
// frame until both are resolved.
import './fonts'
import { formatDuration } from './format'

// A type alias, not an interface: Remotion constrains composition props to
// `Record<string, unknown>`, which only type aliases satisfy structurally.
export type FilmProps = {
  readonly gameId: string
  readonly framesPerTurn: number
  readonly titleFrames: number
  readonly outroFrames: number
  readonly superSample: number
  readonly showCityLabels: boolean
}

export const DEFAULT_FILM_TIMING = {
  framesPerTurn: 4,
  titleFrames: 120,
  // The end screen holds ~7s: long enough to read the final board, the
  // standings, all three metric charts, and the outcome line.
  outroFrames: 210,
} as const

export function filmDurationInFrames(
  turnCount: number,
  timing: Pick<FilmProps, 'framesPerTurn' | 'titleFrames' | 'outroFrames'>,
): number {
  return timing.titleFrames
    + Math.max(1, turnCount) * timing.framesPerTurn
    + timing.outroFrames
}

const MAP_WIDTH = 1316
const PANEL_WIDTH = 500
const CHART_HEIGHT = 236
// Three charts abreast inside the map's width, separated by 1px shared rules.
const CHART_WIDTH = Math.floor((MAP_WIDTH - 2 - 2) / 3)

const CHARTS: readonly {
  readonly label: string
  readonly pick: (track: PlayerTrack) => readonly number[]
}[] = [
  { label: 'Score history', pick: (track) => track.scores },
  { label: 'Cities', pick: (track) => track.cities },
  { label: 'Technologies', pick: (track) => track.techs },
]

function TopBar({ film }: { readonly film: Film }) {
  // The match dossier is gone; its one durable fact rides here instead.
  const wallClock = film.meta.startedAt !== null && film.meta.finishedAt !== null
    ? formatDuration(film.meta.finishedAt - film.meta.startedAt)
    : null
  return (
    <div className="flex items-center justify-between border-b border-line pb-[13px]">
      <span className="label">Freeciv Agent Arena</span>
      <span className="label">
        {film.meta.gameId}
        {wallClock && ` · ${wallClock} of play`}
      </span>
      <span className="label">
        {film.meta.ruleset} · {film.meta.width}x{film.meta.height} · {film.meta.topology}
      </span>
    </div>
  )
}

function PlayStage({
  film, framesPerTurn, superSample, showCityLabels,
}: {
  readonly film: Film
  readonly framesPerTurn: number
  readonly superSample: number
  readonly showCityLabels: boolean
}) {
  const frame = useCurrentFrame()
  const { fps } = useVideoConfig()
  // The board is a rotated rectangle; its height follows from the fitted width,
  // so the panel hugs the map instead of letterboxing it.
  const mapHeight = useMemo(() => {
    const layout = buildBoardLayout(film.meta.width, film.meta.height)
    const aspect = (layout.bounds.maxX - layout.bounds.minX)
      / (layout.bounds.maxY - layout.bounds.minY)
    return Math.round(MAP_WIDTH / aspect)
  }, [film.meta.height, film.meta.width])
  const lastIndex = film.turns.length - 1
  const raw = frame / framesPerTurn
  const turnIndex = Math.min(lastIndex, Math.floor(raw))
  const progress = turnIndex >= lastIndex ? 0 : raw - turnIndex
  const turn = film.turns[turnIndex] ?? film.turns[0]
  const reveal = interpolate(frame, [0, 22], [0, 1], {
    extrapolateLeft: 'clamp', extrapolateRight: 'clamp',
  })
  const fadeIn = interpolate(frame, [0, 14], [0, 1], {
    extrapolateLeft: 'clamp', extrapolateRight: 'clamp',
  })
  if (!turn) return null

  return (
    <div
      className="flex h-full w-full flex-col gap-[18px] px-[30px] py-[26px]"
      style={{ opacity: fadeIn }}
    >
      <TopBar film={film} />
      <div className="flex min-h-0 flex-1 gap-[24px]">
        <div className="flex flex-1 flex-col gap-[18px]">
          <Ticker film={film} turn={turn} turnIndex={turnIndex} width={MAP_WIDTH} />
          <div
            className="board-frame relative overflow-hidden border border-board-edge bg-board"
            style={{ height: mapHeight, width: MAP_WIDTH }}
          >
            <BoardCanvas
              colorByPlayer={film.colors.colorByPlayer}
              height={mapHeight}
              meta={film.meta}
              reveal={reveal}
              showLabels={showCityLabels}
              superSample={superSample}
              turn={turn}
              width={MAP_WIDTH}
            />
            <EventCaption
              film={film}
              fps={fps}
              frame={frame}
              framesPerTurn={framesPerTurn}
              width={MAP_WIDTH}
            />
          </div>
          <div className="flex gap-[28px]">
            <span className="label">Synthetic board · native save data</span>
            <span className="label">
              {(film.meta.width * film.meta.height).toLocaleString('en-US')} tiles
            </span>
            <span className="label">{turn.cities.length.toLocaleString('en-US')} cities</span>
            <span className="label">
              {turn.units.length.toLocaleString('en-US')} unit stacks
            </span>
            <span className="label ml-auto">board turn {turn.boardTurn ?? '—'}</span>
          </div>
          {/*
           * Score, cities and technologies in one row. Opaque, because the
           * ground behind the page is a dithered bitmap and a transparent
           * panel puts that texture directly under a chart's own gridlines --
           * it shows through the gutters, never under content. The 1px seams
           * between the three are shared rules, not gutters.
           */}
          <div
            className="hair-grid grid grid-cols-3 border border-line"
            style={{ width: MAP_WIDTH }}
          >
            {CHARTS.map((chart) => (
              <MetricChart
                firstTurn={film.meta.firstTurn}
                height={CHART_HEIGHT}
                key={chart.label}
                label={chart.label}
                progress={progress}
                series={film.seatTracks.map((track) => ({
                  key: track.player.playerId,
                  color: track.renderColor,
                  values: chart.pick(track),
                }))}
                totalTurns={film.turns.length}
                turnIndex={turnIndex}
                width={CHART_WIDTH}
              />
            ))}
          </div>
        </div>
        <ScorePanel
          film={film}
          progress={progress}
          turn={turn}
          turnIndex={turnIndex}
          width={PANEL_WIDTH}
        />
      </div>
    </div>
  )
}

export function GameFilm({
  gameId, framesPerTurn, titleFrames, outroFrames, superSample, showCityLabels,
}: FilmProps) {
  const film = useFilm(gameId)

  return (
    <AbsoluteFill className="arena-page">
      {/* The ground, under every sequence: a dithered sine band that drifts for
          the length of the film. It sits behind the title, the board and the
          standings alike, so the three acts read as one surface rather than as
          three cards that happen to follow each other. */}
      <DitherWave />
      {film && (
        <>
          <Sequence durationInFrames={titleFrames} name="Title">
            <TitleCard durationInFrames={titleFrames} film={film} />
          </Sequence>
          <Sequence
            durationInFrames={film.turns.length * framesPerTurn}
            from={titleFrames}
            name="Match"
          >
            <PlayStage
              film={film}
              framesPerTurn={framesPerTurn}
              showCityLabels={showCityLabels}
              superSample={superSample}
            />
          </Sequence>
          <Sequence
            durationInFrames={outroFrames}
            from={titleFrames + film.turns.length * framesPerTurn}
            name="Standings"
          >
            <Outro film={film} superSample={superSample} />
          </Sequence>
        </>
      )}
    </AbsoluteFill>
  );
}
