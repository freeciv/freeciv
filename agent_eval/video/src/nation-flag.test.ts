import { existsSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import FLAG_ASSETS from './dataset/flag-assets.json'
import NATION_FLAGS from './dataset/nation-flags.json'
import { flagSlugForNation, hasFlagAsset } from './nation-flag'

/**
 * A faction's nation name and its flag file agree about almost nothing, and the
 * only thing that knows both is `data/nation/*.ruleset`. These cases pin the
 * three shapes the mapping takes, so a regenerated map that quietly drops or
 * renames one is caught here rather than in a rendered frame.
 */

describe('flagSlugForNation', () => {
  it('maps a nation whose flag is named after it', () => {
    expect(flagSlugForNation('Aztec')).toBe('aztec')
    expect(flagSlugForNation('Pirate')).toBe('pirate')
    expect(flagSlugForNation('Barbarian')).toBe('barbarian')
  })

  it('maps a nation to the country its flag is filed under', () => {
    // The demonym is not the file name, which is the whole reason this map
    // exists rather than a lowercase() call.
    expect(flagSlugForNation('English')).toBe('england')
    expect(flagSlugForNation('Spanish')).toBe('spain')
    expect(flagSlugForNation('Italian')).toBe('italy')
    expect(flagSlugForNation('Portuguese')).toBe('portugal')
  })

  it('maps a nation whose flag is not the modern one', () => {
    // Nothing about `Greek` suggests `greece_ancient`, and `greece.svg` also
    // exists, so a guesser would confidently pick the wrong file.
    expect(flagSlugForNation('Greek')).toBe('greece_ancient')
    expect(flagSlugForNation('Babylonian')).toBe('babylon')
  })

  it('returns null for a nation no ruleset declares', () => {
    // Freeciv ships 572 nations including Atlantean and Martian, so an
    // "obviously fictional" name is a bad negative case. This one is not in it.
    expect(flagSlugForNation('Nation Of Nowhere')).toBeNull()
    expect(flagSlugForNation('')).toBeNull()
    // Freeciv's own non-faction labels must not resolve to art either.
    expect(flagSlugForNation('Freeciv Classic AI')).toBeNull()
  })

  it('tolerates surrounding whitespace but not a different name', () => {
    expect(flagSlugForNation('  English  ')).toBe('england')
    expect(flagSlugForNation('english')).toBeNull()
  })
})

describe('hasFlagAsset', () => {
  it('separates a staged flag from one that is only mapped', () => {
    expect(hasFlagAsset('English')).toBe(true)
    // Mapped in the full 572, deliberately not staged: no dataset names it.
    expect(flagSlugForNation('Japanese')).toBe('japan')
    expect(hasFlagAsset('Japanese')).toBe(false)
    expect(hasFlagAsset('Nation Of Nowhere')).toBe(false)
  })

  it('covers every nation the checked-in datasets actually name', () => {
    // If this fails, `scripts/build_nation_flags.py` needs re-running.
    for (const nation of [
      'English', 'Spanish', 'Italian', 'Portuguese', 'Aztec',
      'Babylonian', 'Greek', 'Pirate', 'Barbarian',
    ]) {
      expect(hasFlagAsset(nation), nation).toBe(true)
    }
  })
})

describe('the generated files', () => {
  const PUBLIC = join(dirname(fileURLToPath(import.meta.url)), '..', 'public')

  it('stages art on disk for every slug the manifest claims', () => {
    // The manifest is what lets the component skip a nation without waiting for
    // a load to fail, so a manifest that overstates what is on disk puts an
    // empty framed box back on screen.
    expect(FLAG_ASSETS.length).toBeGreaterThan(0)
    for (const slug of FLAG_ASSETS) {
      expect(existsSync(join(PUBLIC, 'flags', `${slug}.svg`)), slug).toBe(true)
    }
  })

  it('keeps the manifest a subset of the map', () => {
    const slugs = new Set(Object.values(NATION_FLAGS))
    for (const slug of FLAG_ASSETS) expect(slugs.has(slug), slug).toBe(true)
  })

  it('maps every nation to a non-empty slug', () => {
    const entries = Object.entries(NATION_FLAGS)
    expect(entries.length).toBeGreaterThan(500)
    for (const [nation, slug] of entries) {
      expect(nation.length, nation).toBeGreaterThan(0)
      // Hyphens are real: `guinea-bissau`, `nuu-chah-nulth`.
      expect(slug, nation).toMatch(/^[a-z0-9_-]+$/)
    }
  })
})
