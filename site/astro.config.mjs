// @ts-check
import { defineConfig } from "astro/config";
import { rehypeDocsLinks } from "./src/lib/rehype-docs-links.mjs";

// Served from the custom domain at its root via GitHub Pages
// (CNAME yui.ldlework.com). Root base means assets/links resolve at "/".
const BASE = "/";

export default defineConfig({
  site: "https://yui.ldlework.com",
  base: BASE,
  trailingSlash: "ignore",
  // /docs lands on the first guide.
  redirects: {
    "/docs": "/docs/getting-started",
  },
  markdown: {
    shikiConfig: {
      theme: "github-dark",
      wrap: false,
    },
    rehypePlugins: [[rehypeDocsLinks, { base: BASE }]],
  },
});
