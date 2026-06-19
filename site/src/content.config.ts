import { defineCollection } from "astro:content";
import { glob } from "astro/loaders";

/**
 * Docs are loaded directly from the repo's top-level `docs/` folder — that
 * markdown is the single source of truth (it also renders on GitHub), so the
 * site never keeps a divergent copy. Titles and ordering live in `docs-nav.ts`,
 * keyed by the file's id (its basename), since the source files carry no
 * frontmatter.
 */
const docs = defineCollection({
  loader: glob({ pattern: "*.md", base: "../docs" }),
});

export const collections = { docs };
