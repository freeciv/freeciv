import { createContext, useContext, type ReactNode } from 'react'
import {
  buildDisplayPalette,
  EMPTY_DISPLAY_PALETTE,
  type DisplayFaction,
  type DisplayPalette,
} from './display-color'

/**
 * Carries one game's color plan to every paint site.
 *
 * Clearance is decided across a whole faction set, so the plan cannot be
 * computed by the leaf that happens to be drawing a swatch. It is built once
 * where the faction list is known — the match view, or a single card in the
 * arena picker — and read back by whatever paints. The default is the empty
 * palette, under which the pinned substitutions still apply, so a component
 * rendered outside a provider degrades to the pinned table rather than to raw
 * recorded color.
 */
const DisplayPaletteContext = createContext<DisplayPalette>(EMPTY_DISPLAY_PALETTE)

export function useDisplayPalette(): DisplayPalette {
  return useContext(DisplayPaletteContext)
}

export function DisplayPaletteProvider({
  children,
  factions,
}: {
  children: ReactNode
  factions: readonly DisplayFaction[]
}) {
  // No `useMemo` on purpose: `buildDisplayPalette` memoizes on the faction set
  // itself, so a caller that rebuilds this array every render still gets the
  // same palette object back. Consumers may depend on its identity.
  const palette = buildDisplayPalette(factions)
  return (
    <DisplayPaletteContext.Provider value={palette}>
      {children}
    </DisplayPaletteContext.Provider>
  )
}
