import { Config } from '@remotion/cli/config'
import { enableTailwind } from '@remotion/tailwind-v4'

Config.setVideoImageFormat('jpeg')
Config.setJpegQuality(92)
Config.setOverwriteOutput(true)
Config.setConcurrency(null)
// The film draws every tile into a 2D canvas; software GL keeps the render
// deterministic and works headless without a GPU.
Config.setChromiumOpenGlRenderer('swangle')
Config.setDelayRenderTimeoutInMilliseconds(120_000)
Config.overrideWebpackConfig(enableTailwind)
