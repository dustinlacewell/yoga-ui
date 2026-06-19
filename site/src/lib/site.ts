/** Single source of truth for site-wide constants and base-path URL handling. */

export const SITE = {
  name: "yui",
  tagline: "Declarative Flexbox UI for C++",
  description:
    "yui is a declarative, flexbox-based UI library for modern C++20 — a React-like component model with reactive state, powered by the Yoga layout engine.",
  repo: "https://github.com/dustinlacewell/yoga-ui",
  version: "0.1.0",
} as const;

/**
 * Prefix an absolute site path with Astro's configured `base`. The site serves
 * from the custom domain root, so `base` is "/" and this is effectively a
 * pass-through — but routing through it keeps every link base-correct if the
 * base ever changes again. Pass a leading-slash path, e.g. `url("/docs")`.
 */
export function url(path: string): string {
  const base = import.meta.env.BASE_URL.replace(/\/$/, "");
  const clean = path.startsWith("/") ? path : `/${path}`;
  return `${base}${clean}`;
}
