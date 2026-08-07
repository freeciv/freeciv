/**
 * The arena's two typefaces, loaded from the renderer's own `public/` directory
 * so a render never reaches the network.
 *
 * Archivo carries the voice -- contender names, captions, prose -- and JetBrains
 * Mono carries the instrument: every label, every numeral, every identifier.
 * Both are SIL Open Font License, vendored as latin-subset variable woff2 next
 * to their licence text in `public/fonts/`.
 *
 * `delayRender` holds the first frame until both faces are in `document.fonts`,
 * because a frame rendered against the fallback stack reflows once the real face
 * arrives and the flicker is baked into the file.
 */

import { continueRender, delayRender, staticFile } from 'remotion'

export const DISPLAY_FAMILY = 'Archivo'
export const MONO_FAMILY = 'JetBrains Mono'

const FACES = [
  { family: DISPLAY_FAMILY, file: 'fonts/Archivo-latin-var.woff2', weight: '100 900' },
  { family: MONO_FAMILY, file: 'fonts/JetBrainsMono-latin-var.woff2', weight: '100 800' },
] as const

function load(): void {
  if (typeof document === 'undefined' || typeof FontFace === 'undefined') return
  const handle = delayRender('Loading arena typefaces')
  const settle = (): void => {
    continueRender(handle)
  }
  Promise.all(
    FACES.map(async ({ family, file, weight }) => {
      const face = new FontFace(family, `url(${staticFile(file)}) format('woff2')`, {
        weight,
        style: 'normal',
        display: 'block',
      })
      document.fonts.add(await face.load())
    }),
  ).then(settle, settle)
}

load()
