import { defineConfig, loadEnv, type Plugin } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

function routeAwareAssetBases(): Plugin {
  return {
    name: 'route-aware-asset-bases',
    enforce: 'post',
    generateBundle(_options, bundle) {
      const index = bundle['index.html']
      if (!index || index.type !== 'asset' || typeof index.source !== 'string') {
        throw new Error('Vite did not emit the replay viewer index')
      }
      if (!index.source.includes('./assets/')) {
        throw new Error('Replay viewer index has no relative hashed assets')
      }
      const source = index.source
      this.emitFile({
        type: 'asset',
        fileName: 'arena.html',
        source: source.replaceAll('./assets/', './viewer/assets/'),
      })
      index.source = source.replaceAll('./assets/', '../viewer/assets/')
    },
  }
}

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, '.', '')
  return {
    base: './',
    plugins: [react(), tailwindcss(), routeAwareAssetBases()],
    server: {
      proxy: {
        '/v1': {
          target: env.AGENT_EVAL_SERVICE_URL || 'http://127.0.0.1:8765',
          changeOrigin: true,
        },
      },
    },
    build: {
      outDir: 'dist',
      emptyOutDir: true,
      sourcemap: false,
    },
  }
})
