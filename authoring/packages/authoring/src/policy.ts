/**
 * Public API versioning and deprecation policy for `@instantee/authoring`.
 *
 * - Semver applies to this package surface only (not Scene IR / Style Bible /
 *   report protocol versions, which version independently).
 * - Removing or renaming a public export is a MAJOR change.
 * - Changing a default that alters rendered output requires a review note and
 *   either a MAJOR bump or an explicit opt-in replacement API.
 * - Deprecated exports stay callable for at least one MINOR after announcement
 *   and must keep emitting {@link deprecate} diagnostics when used.
 */

export const PUBLIC_API_VERSION = "1.0.0" as const;

/** Locked Style Package tags for the three V1 styles. */
export const STYLE_PACKAGES = Object.freeze({
  editorial: "editorial@1",
  scientificNotebook: "scientific-notebook@1",
  paperCinema: "paper-cinema@1",
} as const);

export type StylePackageId =
  (typeof STYLE_PACKAGES)[keyof typeof STYLE_PACKAGES];

export const STYLE_PACKAGE_IDS: readonly StylePackageId[] = Object.freeze([
  STYLE_PACKAGES.editorial,
  STYLE_PACKAGES.scientificNotebook,
  STYLE_PACKAGES.paperCinema,
]);

export type DeprecationRecord = {
  readonly symbol: string;
  readonly since: string;
  readonly replaceWith?: string;
  readonly removeAfter?: string;
  readonly changesOutputDefault?: boolean;
};

/** Announced deprecations. Empty at 1.0.0 freeze; grow via review. */
export const DEPRECATIONS: readonly DeprecationRecord[] = Object.freeze([]);

const warned = new Set<string>();

type HostGlobals = typeof globalThis & {
  console?: { warn?: (message: string) => void };
  process?: { stderr?: { write?: (chunk: string) => unknown } };
};

function emitWarning(message: string): void {
  const host = globalThis as HostGlobals;
  if (typeof host.console?.warn === "function") {
    host.console.warn(message);
    return;
  }
  host.process?.stderr?.write?.(`${message}\n`);
}

/**
 * Record a one-shot deprecation warning for a public symbol. Does not throw.
 */
export function deprecate(record: DeprecationRecord): void {
  if (warned.has(record.symbol)) {
    return;
  }
  warned.add(record.symbol);
  const replacement = record.replaceWith
    ? `; use ${record.replaceWith} instead`
    : "";
  const removal = record.removeAfter
    ? `; planned removal after ${record.removeAfter}`
    : "";
  const output = record.changesOutputDefault
    ? "; changes default rendered output and needs review"
    : "";
  emitWarning(
    `[instantee/authoring ${PUBLIC_API_VERSION}] deprecated ${record.symbol} since ${record.since}${replacement}${removal}${output}`,
  );
}

/** Test helper: clear one-shot warning state. */
export function resetDeprecationWarnings(): void {
  warned.clear();
}
