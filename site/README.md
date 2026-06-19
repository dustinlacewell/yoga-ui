# yui site

The yui project site — landing page plus documentation — built with
[Astro](https://astro.build) and deployed to GitHub Pages at
`https://dustinlacewell.github.io/yoga-ui/`.

## Single source of truth

The docs pages are **not** copied here. The content collection
(`src/content.config.ts`) loads the repo's top-level `docs/*.md` directly, so
the same markdown renders both on GitHub and on the site. To edit a doc, edit
`../docs/<name>.md`.

- **Order & titles** for the sidebar / prev-next live in `src/lib/docs-nav.ts`.
- **Cross-links** like `[Primitives](primitives.md)` are rewritten to site
  routes at build time by `src/lib/rehype-docs-links.mjs` — the source stays a
  valid GitHub relative link.
- **Screenshots** in `public/img/` are copied from `../res/`.

## Develop

```bash
pnpm install
pnpm dev        # http://localhost:4321/yoga-ui
```

## Build

```bash
pnpm build      # → dist/
pnpm preview    # serve dist/ locally
```

## Deploy

`.github/workflows/pages.yml` builds and publishes to GitHub Pages on every push
to `master` that touches `site/`, `docs/`, or `res/`. Enable Pages with the
"GitHub Actions" source in repo settings for the first deploy.
