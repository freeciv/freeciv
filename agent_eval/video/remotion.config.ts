import { Config } from '@remotion/cli/config'
import { enableTailwind } from '@remotion/tailwind-v4'

Config.setVideoImageFormat('jpeg')
Config.setJpegQuality(92)
Config.setOverwriteOutput(true)
// Left on auto deliberately. Worker count measured neutral on the 52-turn film
// both under software GL (8 workers 100.2s, 12 workers 104.3s, 14 workers
// 103.7s) and under `angle` (8 workers 7.5s, 12 workers 8.4s, 14 workers 7.1s
// -- a spread inside run-to-run noise). The renderer is not worker-bound.
Config.setConcurrency(null)
// The film draws ~3,900 hexes per frame into a 2D canvas, and that
// rasterization is the whole cost of a frame. `swangle` does it on the CPU and
// measured 5.4 fps; `angle` hands it to the platform GPU and measured 62-74 fps
// on the same film -- a 12x difference, with output visually identical
// (43.5 dB PSNR, and bit-identical across worker counts).
Config.setChromiumOpenGlRenderer('angle')
Config.setDelayRenderTimeoutInMilliseconds(120_000)
Config.overrideWebpackConfig(enableTailwind)
