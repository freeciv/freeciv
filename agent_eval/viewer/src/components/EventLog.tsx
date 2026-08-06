import { useEffect, useMemo, useRef, useState } from 'react'
import {
  activeEventIndex,
  DENSITY_LABEL,
  DENSITY_ORDER,
  eventRows,
  eventsAtDensity,
  omittedSummary,
  type EventDensity,
  type WeightTier,
} from '../event-log'
import { displayPlayerColor } from '../display-color'
import { useDisplayPalette } from '../display-palette'
import type { GameEvent } from '../types'

const ROW = 'grid grid-cols-[auto_auto_minmax(0,1fr)_auto] gap-2.5 items-baseline w-full border-b border-b-[#272320] border-l-2 text-left bg-transparent cursor-pointer last:border-b-0 hover:bg-[#221e19] focus-visible:outline-2 focus-visible:outline-[#c5a46d] focus-visible:outline-offset-[-2px]'
const CURRENT_ROW = 'border-l-acid bg-[#211d16]'
const PAST_ROW = 'border-l-transparent'
const UPCOMING_ROW = 'border-l-transparent opacity-45'

/** Weight is the hierarchy: a bigger moment reads bigger. */
const TIER_ROW: Readonly<Record<WeightTier, string>> = {
  major: 'py-2.5 px-3',
  notable: 'py-2 px-3',
  routine: 'py-1.5 px-3',
}
const TIER_TEXT: Readonly<Record<WeightTier, string>> = {
  major: 'text-[#f0e6d4] text-[12px] font-semibold',
  notable: 'text-[#ded3c0] text-[11px]',
  routine: 'text-[#a89e8f] text-[10px]',
}
const TIER_DOT: Readonly<Record<WeightTier, string>> = {
  major: 'w-2.5 h-2.5',
  notable: 'w-[7px] h-[7px]',
  routine: 'w-[5px] h-[5px]',
}

const DENSITY_BUTTON = 'py-1.5 px-2.5 border border-[#3b352d] text-[8px] leading-none font-readout font-extrabold tracking-[.12em] uppercase cursor-pointer focus-visible:outline-2 focus-visible:outline-[#c5a46d] focus-visible:outline-offset-2'
const DENSITY_ON = 'text-[#e6d5b1] bg-[#2a251c] border-[#5d4e38]'
const DENSITY_OFF = 'text-[#8b8073] bg-transparent hover:text-[#c3b69f]'

interface EventLogProps {
  events: GameEvent[]
  /** Actor id to recorded faction color. */
  colors: ReadonlyMap<string, string>
  onSelectTurn: (turn: number) => void
  selectedTurn: number
}

export function EventLog({ colors, events, onSelectTurn, selectedTurn }: EventLogProps) {
  const [density, setDensity] = useState<EventDensity>('all')
  const visible = useMemo(() => eventsAtDensity(events, density), [density, events])
  const rows = useMemo(
    () => eventRows(visible, selectedTurn, colors),
    [colors, selectedTurn, visible],
  )
  const active = activeEventIndex(visible, selectedTurn)
  const palette = useDisplayPalette()
  const scroller = useRef<HTMLDivElement>(null)
  const currentRow = useRef<HTMLButtonElement>(null)

  useEffect(() => {
    const container = scroller.current
    const row = currentRow.current
    if (!container || !row) return
    // Scroll the log itself rather than the page: the panel follows playback
    // without yanking the spectator away from the map.
    const offset = row.offsetTop - container.offsetTop
    const above = offset < container.scrollTop
    const below = offset + row.offsetHeight > container.scrollTop + container.clientHeight
    if (above || below) {
      container.scrollTop = offset - container.clientHeight / 2 + row.offsetHeight / 2
    }
  }, [active, density])

  return (
    <>
      <div className="flex flex-wrap gap-1.5 items-center py-2 px-3 border-b border-b-[#332e27]" role="group" aria-label="Event log density">
        {DENSITY_ORDER.map((option) => (
          <button
            aria-pressed={density === option}
            className={`${DENSITY_BUTTON} ${density === option ? DENSITY_ON : DENSITY_OFF}`}
            key={option}
            onClick={() => setDensity(option)}
            type="button"
          >
            {DENSITY_LABEL[option]}
          </button>
        ))}
        <span className="ml-auto text-[#6f665b] text-[8px] leading-none font-readout tracking-[.1em] uppercase">
          {visible.length} of {events.length}
        </span>
      </div>
      <div
        className="max-h-[420px] overflow-y-auto max-[760px]:max-h-[300px]"
        ref={scroller}
      >
        {rows.map((row, index) => (
          <button
            aria-current={row.current ? 'true' : undefined}
            className={`${ROW} ${TIER_ROW[row.tier]} ${row.current ? CURRENT_ROW : row.upcoming ? UPCOMING_ROW : PAST_ROW}`}
            key={`${row.event.turn}-${row.event.kind}-${index}`}
            onClick={() => onSelectTurn(row.event.turn)}
            title={`Weight ${row.event.weight}`}
            type="button"
            ref={row.current ? currentRow : undefined}
          >
            <b className="text-[#cdb17d] font-bold text-[10px] leading-none font-readout tabular-nums">T{row.event.turn}</b>
            <span
              aria-hidden="true"
              className={`${TIER_DOT[row.tier]} rounded-full border border-[#0000004d]`}
              style={{ background: displayPlayerColor(row.color, palette) ?? '#5b544a' }}
            />
            <span className={`min-w-0 leading-[1.45] [overflow-wrap:anywhere] ${TIER_TEXT[row.tier]}`}>{row.event.summary}</span>
            <em className="text-[#8b8073] text-[8px] leading-none font-readout tracking-[.1em] uppercase not-italic max-[460px]:hidden">{row.label}</em>
          </button>
        ))}
        {!rows.length && <p className="empty-copy">Nothing at this density yet.</p>}
      </div>
    </>
  )
}

export function EventLogFootnote({ omitted }: { omitted: Record<string, number> }) {
  const summary = omittedSummary(omitted)
  if (!summary) return null
  return (
    <p className="m-0 py-2.5 px-3 border-t border-t-[#332e27] text-muted text-[10px] leading-[1.5]">
      The log is capped for this match: {summary} not shown.
    </p>
  )
}
