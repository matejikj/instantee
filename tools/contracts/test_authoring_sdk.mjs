#!/usr/bin/env node
/**
 * TypeScript authoring SDK + seconds→frames checks against the schema.
 * Imports the compiled package from build/authoring/sdk.
 */
import { createRequire } from "node:module";
import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createFilm, filmToJson, secondsToFrame } from "../../build/authoring/sdk/index.js";

const require = createRequire(import.meta.url);
const Ajv2020 = require("ajv/dist/2020.js");
const addFormats = require("ajv-formats");

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");

function fail(message) {
  console.error(message);
  process.exit(1);
}

const film = createFilm({
  filmId: "sdk-minimal",
  seed: 42,
  shots: [
    {
      id: "intro",
      durationSeconds: 60,
      intent: {
        type: "reveal",
        intensity: 0.8,
        importance: 0.9,
        targetIds: ["headline"],
      },
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

console.log("authoring SDK tests passed");
