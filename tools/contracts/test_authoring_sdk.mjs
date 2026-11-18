#!/usr/bin/env node
/**
 * TypeScript authoring SDK + seconds→frames checks against the schema.
 * Imports the compiled package through the public workspace entry.
 */
import { createRequire } from "node:module";
import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  AuthoringError,
  INTENT_TYPES,
  PUBLIC_API_VERSION,
  STYLE_PACKAGE_IDS,
  collectNodeIds,
  createFilm,
  createIntent,
  direction,
  filmToJson,
  isAuthoringError,
  parentIdOf,
  reparentNode,
  secondsToFrame,
  styles,
} from "@instantee/authoring";

const require = createRequire(import.meta.url);
const Ajv2020 = require("ajv/dist/2020.js");
const addFormats = require("ajv-formats");

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");

function fail(message) {
  console.error(message);
  process.exit(1);
}

if (!INTENT_TYPES.includes("reveal") || INTENT_TYPES.length < 5) {
  fail("INTENT_TYPES must list CDS intent kinds");
}
if (PUBLIC_API_VERSION !== "1.0.0") {
  fail(`PUBLIC_API_VERSION must be 1.0.0, got ${PUBLIC_API_VERSION}`);
}
if (STYLE_PACKAGE_IDS.length !== 3) {
  fail("STYLE_PACKAGE_IDS must list the three V1 styles");
}
if (
  styles.editorial !== "editorial@1" ||
  styles.scientificNotebook !== "scientific-notebook@1" ||
  styles.paperCinema !== "paper-cinema@1"
) {
  fail("locked style package tags drifted");
}

const reveal = createIntent("reveal", 0.8, {
  importance: 0.9,
  targetIds: ["headline"],
});
if (reveal.type !== "reveal" || reveal.targetIds?.[0] !== "headline") {
  fail("createIntent must produce typed semantic intent");
}
const directed = direction.reveal(0.8, {
  importance: 0.9,
  targetIds: ["headline"],
});
if (directed.type !== "reveal" || directed.intensity !== 0.8) {
  fail("direction.reveal must build a typed semantic intent");
}

try {
  createIntent(/** @type {any} */ ("not-an-intent"), 0.5);
  fail("unsupported intent must throw AuthoringError");
} catch (error) {
  if (!isAuthoringError(error) || error.code !== "unsupported_intent") {
    fail(`expected unsupported_intent AuthoringError, got ${error}`);
  }
}

try {
  createIntent("reveal", 2);
  fail("out-of-range intensity must throw AuthoringError");
} catch (error) {
  if (!(error instanceof AuthoringError) || error.code !== "range_error") {
    fail(`expected range_error AuthoringError, got ${error}`);
  }
}

const film = createFilm({
  filmId: "sdk-minimal",
  seed: 42,
  shots: [
    {
      id: "intro",
      durationSeconds: 60,
      styleOverride: "editorial@1",
      intent: reveal,
      nodes: [
        {
          id: "headline",
          text: "TECH HAS CHANGED",
          role: "headline",
          x: 50,
          y: 250,
        },
      ],
    },
  ],
});

if (film.shots[0]?.duration_frames !== 1800) {
  fail(`expected 60s → 1800 frames, got ${film.shots[0]?.duration_frames}`);
}
if (film.shots[0]?.style_override !== "editorial@1") {
  fail("createFilm must emit style_override from styleOverride");
}
if (film.seed !== 42) {
  fail("createFilm must preserve explicit seed");
}
if (film.shots[0]?.intent?.type !== "reveal") {
  fail("createFilm must emit typed intent");
}

Object.freeze(film);
try {
  // @ts-expect-error runtime freeze check
  film.film_id = "mutated";
  fail("film should be frozen");
} catch {
  // expected
}

const schema = JSON.parse(
  await readFile(path.join(repoRoot, "contracts/schemas/authoring-v1.schema.json"), "utf8"),
);
const ajv = new Ajv2020.default({ allErrors: true, strict: true });
addFormats.default(ajv);
const validate = ajv.compile(schema);
if (!validate(film)) {
  fail(`SDK film failed schema: ${JSON.stringify(validate.errors, null, 2)}`);
}

const json = filmToJson(film);
if (!json.includes('"schema_version": "1.0.0"')) {
  fail("serialized film missing schema_version");
}

const rate = { numerator: 30, denominator: 1 };
const half = secondsToFrame(0.5, rate);
if (half.frame !== 15 || half.subFrameLoss) {
  fail(`0.5s conversion failed: ${JSON.stringify(half)}`);
}
const sixtieth = secondsToFrame(1 / 60, rate);
if (sixtieth.frame !== 1 || !sixtieth.subFrameLoss) {
  fail(`1/60s conversion failed: ${JSON.stringify(sixtieth)}`);
}

// Cross-check C++ micros conversion when the binary is available.
const cpp = [
  path.join(repoRoot, "build/release/engine/tests/unit/core-model/spike_core_model_time_convert_test"),
  path.join(repoRoot, "build/dev/engine/tests/unit/core-model/spike_core_model_time_convert_test"),
].find((candidate) => existsSync(candidate));
if (cpp) {
  const result = spawnSync(cpp, { encoding: "utf8" });
  if (result.status !== 0) {
    fail(`C++ time convert test failed: ${result.stderr}`);
  }
}

// Hierarchical scene graph: reparent preserves ids, rejects cycles, no UI APIs.
{
  /** @type {import("@instantee/authoring").AuthoringNode[]} */
  const roots = [
    {
      id: "group_a",
      type: "group",
      children: [
        { id: "child_a", type: "text", text: "A" },
        { id: "child_b", type: "text", text: "B" },
      ],
    },
    { id: "root_b", type: "text", text: "R" },
  ];
  const before = collectNodeIds(roots).slice().sort();
  const moved = reparentNode(roots, "child_a", "root_b");
  if (parentIdOf(moved, "child_a") !== "root_b") {
    fail(`expected child_a under root_b, got ${parentIdOf(moved, "child_a")}`);
  }
  if (parentIdOf(moved, "child_b") !== "group_a") {
    fail("child_b parent must stay group_a");
  }
  const after = collectNodeIds(moved).slice().sort();
  if (JSON.stringify(before) !== JSON.stringify(after)) {
    fail("reparent must preserve node identities");
  }
  let cyclic = false;
  try {
    reparentNode(roots, "group_a", "child_a");
  } catch (error) {
    cyclic = String(error).includes("cyclic_parent");
  }
  if (!cyclic) {
    fail("reparent under descendant must throw cyclic_parent");
  }
  const toRoot = reparentNode(roots, "child_b", null, { siblingIndex: 0 });
  if (parentIdOf(toRoot, "child_b") !== null) {
    fail("child_b should become a shot root");
  }
  if (toRoot[0]?.id !== "child_b") {
    fail("child_b should be inserted at sibling index 0");
  }
}

console.log("authoring SDK tests passed");
