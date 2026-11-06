import { defineConfig } from "astro/config";

export default defineConfig({
  site: "https://cmb.software",
  base: "/skifree-wasm",
  output: "static",
  outDir: "./dist/skifree-wasm",
  trailingSlash: "always",
});
