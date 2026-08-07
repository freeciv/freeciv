import { useEffect, useState } from 'react'
import { applyTheme, rememberTheme, resolveTheme, type Theme } from '../theme-preference'

/**
 * Two segments rather than one flipping switch: the arena has two surfaces and
 * the control says which one you are on, not which one you would get next.
 *
 * It borrows `.map-tool-group` outright -- the segmented, hairline, mono-label
 * control the map already uses -- so the chrome gains no new vocabulary.
 */
export function ThemeToggle() {
  const [theme, setTheme] = useState<Theme>(() => resolveTheme())

  useEffect(() => { applyTheme(theme) }, [theme])

  function choose(next: Theme) {
    setTheme(next)
    rememberTheme(next)
  }

  return (
    <div aria-label="Arena surface" className="map-tool-group" role="group">
      <span>Surface</span>
      <button
        aria-pressed={theme === 'dark'}
        onClick={() => choose('dark')}
        type="button"
      >
        Dark
      </button>
      <button
        aria-pressed={theme === 'light'}
        onClick={() => choose('light')}
        type="button"
      >
        Light
      </button>
    </div>
  )
}
