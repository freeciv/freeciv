/**
 * Nation flag art for the contender cards.
 *
 * A replay names its factions the way the ruleset does and the art is filed
 * under a different name entirely: the nation is `English`, the file is
 * `england.svg`; `Greek` is `greece_ancient`. Only `data/nation/*.ruleset` knows
 * both halves, so `dataset/nation-flags.json` is generated from them by
 * `scripts/build_nation_flags.py` rather than written by hand. That script also
 * stages the handful of flags the checked-in datasets need into `public/flags/`
 * and records which ones landed, because the full set is 582 files and 16.6 MB
 * and no single film needs more than four of them.
 *
 * Unlike the harness and provider marks in `logos.tsx`, a flag is real art in
 * its own colours, so it is an `<img>` and not a mask. The one thing it borrows
 * from the surface is a hairline edge, and that is not decoration: see below.
 */

import type { CSSProperties, ReactElement } from 'react'
import { staticFile } from 'remotion'
import FLAG_ASSETS from './dataset/flag-assets.json'
import NATION_FLAGS from './dataset/nation-flags.json'

const SLUG_BY_NATION: Readonly<Record<string, string>> = NATION_FLAGS
const STAGED: ReadonlySet<string> = new Set<string>(FLAG_ASSETS)

/**
 * The flag slug for a ruleset nation name, or `null` for a nation no ruleset
 * declares. Exact match: these names arrive from the same rulesets the map was
 * built from, so a near miss means a genuinely unknown nation, not a typo worth
 * guessing at.
 */
export function flagSlugForNation(nation: string): string | null {
  return SLUG_BY_NATION[nation.trim()] ?? null
}

/**
 * Whether that nation's art is actually staged in `public/flags/`.
 *
 * The map covers all 572 nations; only the ones the checked-in datasets name are
 * on disk. An `<img>` pointed at a file that is not there draws an empty box
 * inside its own hairline, which looks exactly like a flag that failed to load
 * -- so the component asks this first and renders nothing instead. Staging is
 * known at build time, so nothing has to fail before we find out.
 */
export function hasFlagAsset(nation: string): boolean {
  const slug = flagSlugForNation(nation)
  return slug !== null && STAGED.has(slug)
}

/** Freeciv draws every flag 3:2. */
const FLAG_ASPECT = 3 / 2
const DEFAULT_WIDTH = 48

export interface NationFlagProps {
  /** Ruleset nation name, e.g. `English`. Unknown or unstaged renders nothing. */
  readonly nation: string | null | undefined
  /** Width in px; height follows the 3:2 the art is drawn at. */
  readonly size?: number | undefined
  readonly className?: string | undefined
  readonly style?: CSSProperties | undefined
  /** Accessible name; defaults to `<nation> flag`. */
  readonly title?: string | undefined
  /**
   * Set false to drop the hairline. Only worth doing over a surface that
   * already contains the flag, like a filled chip.
   */
  readonly outlined?: boolean | undefined
}

/**
 * A nation's flag, or nothing at all.
 *
 * The hairline is on by default and is load-bearing rather than styling. The
 * film is a light surface (`#eceae8`), and a flag with a pale field has no edge
 * against it: England reads as a red cross floating in the page.
 *
 * This is not a two-flag problem to paper over with an exception list. Drawing
 * all 582 flags to a canvas and measuring how much of each border ring lands
 * within 28/255 of the page colour: 118 of them (20%) lose more than a quarter
 * of their outline, and 56 (10%) lose more than half. England loses 83%, and
 * even Italy loses 21% where its white stripe meets the top and bottom edges.
 * So the hairline is unconditional. At 20% alpha it rescues the pale ones and
 * costs the flags that already hold their own shape nothing but a crisp edge.
 *
 * It is mixed from `currentColor` rather than a fixed grey so it inverts by
 * itself over the dark board, the same trick the logo marks use.
 *
 * It is an `outline` pulled inward and not an inset `box-shadow`, because an
 * inset shadow on a replaced element paints *behind* the image: on an `<img>`
 * it is simply invisible, with no warning. Outline and box-shadow both stay out
 * of layout, so the flag still measures exactly `size` across.
 */
export function NationFlag({
  nation, size = DEFAULT_WIDTH, title, className, style, outlined = true,
}: NationFlagProps): ReactElement | null {
  if (nation === null || nation === undefined) return null
  const slug = flagSlugForNation(nation)
  if (slug === null || !STAGED.has(slug)) return null
  return (
    <img
      alt={title ?? `${nation.trim()} flag`}
      className={className}
      height={Math.round(size / FLAG_ASPECT)}
      src={staticFile(`flags/${slug}.svg`)}
      style={{
        display: 'block',
        flex: 'none',
        width: size,
        height: Math.round(size / FLAG_ASPECT),
        // `cover` rather than `contain`: every flag is already 3:2, and on the
        // day one is not, a hair of crop reads better than page-coloured bars
        // sitting inside the hairline pretending to be part of the flag.
        objectFit: 'cover',
        ...(outlined
          ? {
            outline: '1px solid color-mix(in srgb, currentColor 22%, transparent)',
            outlineOffset: '-1px',
            boxShadow: '0 1px 2px color-mix(in srgb, currentColor 8%, transparent)',
          }
          : {}),
        ...style,
      }}
      width={size}
    />
  )
}
