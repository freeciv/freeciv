import type { Film, PlayerTrack, TurnState } from '../dataset/film'
import { sampleTrack } from '../dataset/film'
import { controllerDisplayName, nationDisplayName } from '../faction-label'
import { HarnessLogo, ProviderLogo, controllerMarks } from '../logos'
import { NationFlag } from '../nation-flag'
import { SHELL, mixColors, withAlpha } from '../theme'

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
        stroke={withAlpha(SHELL.ink, 0.08)}
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
          strokeLinecap="square"
          strokeWidth={1.6}
        />
      )}
    </svg>
  )
}

function StatCell({ label, value }: { readonly label: string; readonly value: string }) {
  return (
    <div className="flex flex-col gap-[5px]">
      <span className="label">{label}</span>
      <span className="font-mono text-[19px] font-medium text-ink tabular-nums">{value}</span>
    </div>
  )
}

/**
 * A faction at a glance: flag, name in its own colour, and three numbers.
 *
 * Everything that is not the agent renders like this. In a two-handed match
 * the CPU still earns a full card, but past that the rail cannot give ten
 * seats two hundred pixels each -- and it should not, because in a
 * one-agent-against-many game the other nine are the weather, not the story.
 * Third parties have always been read this way; this is the same treatment,
 * applied to anyone who is not the subject of the film.
 */
function CompactFaction({
  track, turn,
}: {
  readonly track: PlayerTrack
  readonly turn: TurnState
}) {
  const stat = turn.statsByPlayer.get(track.player.playerId)
  const alive = stat?.alive !== false
  return (
    <div className="flex items-center gap-[10px]" style={{ opacity: alive ? 1 : 0.5 }}>
      <NationFlag nation={track.player.nation} size={24} />
      <span
        className="min-w-0 flex-1 truncate font-mono text-[14px] font-medium"
        style={{ color: track.renderColor }}
      >
        {nationDisplayName(track.player)}
      </span>
      <span className="shrink-0 font-mono text-[12px] text-muted tabular-nums">
        {(stat?.cities ?? 0).toLocaleString('en-US')}c
      </span>
      <span className="shrink-0 font-mono text-[12px] text-muted tabular-nums">
        {(stat?.units ?? 0).toLocaleString('en-US')}u
      </span>
      {/* An em-dash, not a zero. Freeciv does not score barbarians, and "0"
          beside thirteen cities reads as a measured result rather than an
          absent one. */}
      <span className="w-[62px] shrink-0 text-right font-mono text-[17px] font-medium text-ink tabular-nums">
        {stat?.score ? stat.score.toLocaleString('en-US') : '—'}
      </span>
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
  const isAgent = track.player.controllerType !== 'native'
  // Both return null for anything unregistered, so a native seat draws no
  // marks rather than a gap where they would be.
  const { harness, provider } = controllerMarks(track.player.controllerLabel)
  const marks = harness !== null || provider !== null

  return (
    /*
     * The leader is marked by a faint wash of its own colour rather than a lit
     * border. A tinted fill reads at a glance and never competes with the
     * faction rail, which is the one place the colour is at full strength.
     */
    <div
      className="flex items-stretch gap-[16px]"
      style={{
        background: rank === 1
          ? mixColors(SHELL.panelRaised, track.renderColor, 0.055)
          : SHELL.panelRaised,
        opacity: alive ? 1 : 0.5,
      }}
    >
      <span className="w-[4px] shrink-0" style={{ background: track.renderColor }} />
      <div className="flex min-w-0 flex-1 flex-col gap-[11px] pt-[15px] pr-[17px] pb-[13px]">
        {/*
         * Role, then identity -- the same reading order the title card uses,
         * so a viewer who has just watched the title knows what these rows
         * are. "Agent" is the useful word here: which model it is sits on the
         * line below, under its own marks.
         */}
        <div className="flex items-center gap-[10px]">
          {marks && (
            <div className="flex shrink-0 items-center gap-[7px]" style={{ color: SHELL.ink }}>
              <HarnessLogo harness={harness} size={16} />
              <ProviderLogo provider={provider} size={16} />
            </div>
          )}
          <span className="min-w-0 truncate font-mono text-[13px] font-medium tracking-[0.01em] text-ink">
            {isAgent ? 'Agent' : controllerDisplayName(track.player)}
          </span>
        </div>
        <div className="flex items-center gap-[9px]">
          {/* Raw ruleset nation: the flag lookup is an exact match on
              Freeciv's own name, so a decorated label resolves to null. */}
          <NationFlag nation={track.player.nation} size={26} />
          <span
            className="min-w-0 truncate font-mono text-[12px]"
            style={{ color: track.renderColor }}
          >
            {nationDisplayName(track.player)}
          </span>
          {isAgent && (
            <span className="ml-auto min-w-0 truncate font-mono text-[11px] text-muted">
              {controllerDisplayName(track.player)}
            </span>
          )}
        </div>
        <div className="flex items-end gap-[13px]">
          <span className="font-mono text-[46px] leading-[0.86] font-medium tracking-[-0.045em] text-ink tabular-nums">
            {Math.round(score).toLocaleString('en-US')}
          </span>
          <span className="label pb-[6px]">Score</span>
          <div className="ml-auto pb-[4px]">
            <Sparkline height={26} track={track} turnIndex={turnIndex} width={110} />
          </div>
        </div>
        <div className="h-[3px]" style={{ background: withAlpha(SHELL.ink, 0.07) }}>
          <div
            className="h-[3px]"
            style={{ background: track.renderColor, width: `${(share * 100).toFixed(2)}%` }}
          />
        </div>
        <div className="grid grid-cols-4 gap-[8px] pt-[3px]">
          <StatCell label="Cities" value={Math.round(cities).toLocaleString('en-US')} />
          <StatCell label="Units" value={Math.round(units).toLocaleString('en-US')} />
          <StatCell label="Techs" value={String(stat?.techs ?? 0)} />
          <StatCell label="Gold" value={String(stat?.gold ?? 0)} />
        </div>
        <div
          className="flex items-baseline justify-between gap-[10px] pt-[10px]"
          style={{ borderTop: `1px solid ${withAlpha(SHELL.ink, 0.07)}` }}
        >
          <span className="label">Researching</span>
          <span className="min-w-0 flex-1 truncate text-right font-mono text-[12px] text-ink">
            {researching || '—'}
          </span>
          <span className="font-mono text-[11px] text-dim tabular-nums">
            {bulbs > 0 ? `${Math.round(bulbs).toLocaleString('en-US')} bulbs` : ''}
          </span>
        </div>
        <div className="flex justify-between gap-[10px]">
          <span className="label">{alive ? stat?.government || 'anarchy' : 'eliminated'}</span>
          <span className="label">
            {alive ? `${stat?.citizens ?? 0} citizens` : 'no cities remain'}
          </span>
        </div>
      </div>
    </div>
  )
}

export function ScorePanel({
  film, turn, turnIndex, progress, width,
}: ScorePanelProps) {
  const scoreOf = (track: PlayerTrack): number => track.scores[turnIndex] ?? 0
  const byScore = [...film.seatTracks].sort((left, right) => scoreOf(right) - scoreOf(left))
  /*
   * Rank is always the standing; the row *order* is not.
   *
   * In a single-player match the rail is seated in film order, which puts the
   * agent on top and keeps it there. Ordering by score made the two rows swap
   * places whenever the lead changed, so the number you were tracking moved
   * under your eye mid-film -- and it buried the model under the built-in AI
   * for most of a losing run, which is the opposite of what the film is about.
   * Agent-vs-agent still sorts by score: there, the standing is the story.
   */
  const agents = film.seatTracks.filter((track) => track.player.controllerType !== 'native')
  const singlePlayer = agents.length === 1 && film.seatTracks.length > 1
  const ranked = singlePlayer ? film.seatTracks : byScore
  const rankOf = new Map(byScore.map((track, index) => [track.player.playerId, index + 1]))
  const leaderScore = Math.max(
    1, ...film.seatTracks.map((track) => track.scores[turnIndex] ?? 0),
  )
  /*
   * Who gets a card, and who gets a row.
   *
   * Two-handed, both sides get a card -- the CPU is half of that match. Past
   * two, only the agents do: the rail cannot give ten seats two hundred pixels
   * each, and in one-agent-against-many the other nine are the weather, not
   * the story. Everyone demoted joins the third parties in the compact list,
   * which is the same reading they already had.
   */
  const heroes = ranked.length > 2
    ? ranked.filter((track) => track.player.controllerType !== 'native')
    : ranked
  const heroIds = new Set(heroes.map((track) => track.player.playerId))
  const demoted = ranked.filter((track) => !heroIds.has(track.player.playerId))
  const thirdParties = film.tracks.filter(
    (track) => !track.player.seat
      && (turn.statsByPlayer.get(track.player.playerId)?.cities ?? 0) > 0,
  )
  const others = [...demoted, ...thirdParties]

  return (
    <div className="flex h-full flex-col gap-[13px]" style={{ width }}>
      <div className="flex items-center justify-between border-b border-line pb-[10px]">
        <span className="label">Score card</span>
        <span className="label">{film.meta.controlProtocol}</span>
      </div>
      {/*
       * Each side is its own bordered card with real space around it, rather
       * than two rows sharing a collapsed 1px rule. A shared seam says "these
       * are two entries in one list"; a gap says "these are two players", and
       * the score card is a comparison of two players. The histories below
       * keep the collapsed frame, because they genuinely are one instrument.
       */}
      <div className="flex flex-col gap-[13px]">
        {heroes.map((track, index) => (
          <div className="border border-line" key={track.player.playerId}>
            <PlayerCard
              leaderScore={leaderScore}
              progress={progress}
              rank={rankOf.get(track.player.playerId) ?? index + 1}
              track={track}
              turn={turn}
              turnIndex={turnIndex}
            />
          </div>
        ))}
      </div>
      {/* A raiding third party is a faction on the board, so it sits with
          the other factions rather than past both charts. It used to trail
          the rail behind a flex spacer, and once the two score cards grew
          borders and a gap it was pushed off the bottom edge entirely --
          invisible, while holding more ground than a contender. */}
      {others.length > 0 && (
        /*
         * The space the two rail charts used to take. A raiding faction can
         * hold more ground and more cities than a contender -- and when it
         * does, the board is a third of its colour while the rail says
         * nothing. This is the room to say it, and the room later things can
         * grow into.
         */
        <div className="flex flex-1 flex-col gap-[13px] border border-line bg-panel px-[16px] py-[14px]">
          <div className="flex items-baseline justify-between">
            <span className="label">Also on the board</span>
            <span className="label">Cities · units · score</span>
          </div>
          {others.map((track) => (
            <CompactFaction key={track.player.playerId} track={track} turn={turn} />
          ))}
        </div>
      )}
    </div>
  )
}
