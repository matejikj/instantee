#!/usr/bin/env node
/**
 * Freeze check for `@instantee/authoring` runtime public exports.
 * Compares the built package surface to the locked snapshot fixture.
 */
import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import * as authoring from "@instantee/authoring";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const snapshotPath = path.join(
  repoRoot,
  "contracts/fixtures/authoring/public-api-v1.snapshot.json",
);

function fail(message) {
  console.error(message);
  process.exit(1);
}

function collectSurface(mod) {
  const exports = Object.keys(mod)
    .filter((name) => name !== "default")
    .sort();
  /** @type {Record<string, string>} */
  const kinds = {};
  for (const name of exports) {
    const value = mod[name];
    kinds[name] = value === null ? "null" : typeof value;
  }
  return {
    schema_version: "1.0.0",
    package: "@instantee/authoring",
    api_version: mod.PUBLIC_API_VERSION,
    style_packages: [...mod.STYLE_PACKAGE_IDS],
    exports,
    kinds,
  };
}

const update = process.argv.includes("--update");
const actual = collectSurface(authoring);

if (actual.api_version !== "1.0.0") {
  fail(`PUBLIC_API_VERSION must be 1.0.0, got ${actual.api_version}`);
}
if (actual.style_packages.length !== 3) {
  fail("expected three locked style packages");
}
for (const required of [
  "createFilm",
  "createIntent",
  "filmToJson",
  "direction",
  "film",
  "sceneGraph",
  "time",
  "styles",
  "api",
  "AuthoringError",
  "PUBLIC_API_VERSION",
  "STYLE_PACKAGES",
  "DEPRECATIONS",
  "deprecate",
]) {
  if (!actual.exports.includes(required)) {
    fail(`missing public export: ${required}`);
  }
}

if (update) {
  await writeFile(snapshotPath, `${JSON.stringify(actual, null, 2)}\n`, "utf8");
  console.log(`updated ${path.relative(repoRoot, snapshotPath)}`);
  process.exit(0);
}

let expected;
try {
  expected = JSON.parse(await readFile(snapshotPath, "utf8"));
} catch (error) {
  fail(
    `missing snapshot fixture at ${path.relative(repoRoot, snapshotPath)}: ${error}`,
  );
}

const actualJson = JSON.stringify(actual, null, 2);
const expectedJson = JSON.stringify(expected, null, 2);
if (actualJson !== expectedJson) {
  fail(
    [
      "public API snapshot mismatch",
      `fixture: ${path.relative(repoRoot, snapshotPath)}`,
      "re-run with --update only after an intentional, reviewed API change",
      "--- expected ---",
      expectedJson,
      "--- actual ---",
      actualJson,
    ].join("\n"),
  );
}

// Namespace shape smoke checks (not duplicated into the snapshot kinds).
if (typeof authoring.direction.reveal !== "function") {
  fail("direction.reveal must be a function");
}
if (typeof authoring.film.create !== "function") {
  fail("film.create must alias createFilm");
}
if (authoring.styles.editorial !== "editorial@1") {
  fail("styles.editorial must be editorial@1");
}
if (authoring.api.version !== authoring.PUBLIC_API_VERSION) {
  fail("api.version must match PUBLIC_API_VERSION");
}

console.log("authoring public API snapshot matched");
