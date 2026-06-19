import { visit } from "unist-util-visit";

/**
 * Rewrites in-content markdown cross-links so they resolve on the site.
 *
 * The source docs (which also render on GitHub) link to siblings by filename,
 * e.g. `[Primitives](primitives.md)` or `(./architecture.md#section)`. On the
 * site those files are routes under `<base>/docs/<slug>`. This plugin rewrites
 * any relative `*.md` href to that route, preserving hash fragments, and leaves
 * absolute URLs untouched.
 *
 * `base` is Astro's configured base path (here "/"); it is passed in from
 * astro.config so the plugin stays config-agnostic.
 */
export function rehypeDocsLinks({ base = "" } = {}) {
  const prefix = `${base.replace(/\/$/, "")}/docs`;

  return (tree) => {
    visit(tree, "element", (node) => {
      if (node.tagName !== "a") return;
      const href = node.properties?.href;
      if (typeof href !== "string") return;

      // Skip absolute URLs, anchors, and mailto.
      if (/^(https?:|mailto:|#|\/)/.test(href)) return;

      const match = href.match(/^\.?\/?([a-z0-9-]+)\.md(#.*)?$/i);
      if (!match) return;

      const slug = match[1];
      const hash = match[2] ?? "";
      node.properties.href = `${prefix}/${slug}${hash}`;
    });
  };
}
