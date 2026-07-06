/**
 * Ordered docs navigation. The `slug` matches the markdown file's basename
 * (its content-collection id) in the repo `docs/` folder. Order here is the
 * order in the sidebar and the prev/next chain.
 */
export interface DocEntry {
  slug: string;
  title: string;
  /** Short label for the sidebar when the full title is long. */
  label?: string;
}

export const DOCS_NAV: DocEntry[] = [
  { slug: "getting-started", title: "Getting Started" },
  { slug: "primitives", title: "Primitives" },
  { slug: "widgets", title: "Widgets" },
  { slug: "overlays", title: "Overlays" },
  { slug: "text-editing", title: "Text Editing", label: "Text Editing" },
  { slug: "components", title: "Components" },
  { slug: "architecture", title: "Architecture" },
  { slug: "extending", title: "Extending Primitives", label: "Extending" },
  { slug: "integration-notes", title: "Integration Notes", label: "Integration" },
  { slug: "migration-1-0-to-1-1", title: "Migrating from 1.0", label: "Migration" },
];

export function navIndex(slug: string): number {
  return DOCS_NAV.findIndex((d) => d.slug === slug);
}

export function docTitle(slug: string): string {
  return DOCS_NAV.find((d) => d.slug === slug)?.title ?? slug;
}
