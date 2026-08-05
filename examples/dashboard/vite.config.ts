import { defineConfig } from 'vite';

export default defineConfig({
  /* Relative, so the build works under a project path on GitHub Pages. */
  base: './',

  /* The AudioWorklet is bundled as an ES module: Emscripten's loader reads
   * import.meta.url, which only exists in a module. */
  worker: { format: 'es' },

  server: {
    /* The binding lives outside this app's root. */
    fs: { allow: ['../..'] },
  },

  build: {
    /* Never inline the wasm as a data: URI. Shipping it as a real file the
     * main thread fetches is the entire point of the loading design. */
    assetsInlineLimit: 0,
    target: 'es2022',
  },
});
