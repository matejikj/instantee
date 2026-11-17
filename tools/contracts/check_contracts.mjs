#!/usr/bin/env node
/**
 * Drift check for generated TypeScript types plus JSON Schema fixture suite.
 * Semantic uint64 overflow (string form beyond 2^64-1) is enforced here; the
 * schema alone cannot pin that bound for every Draft 2020-12 implementation.
 */
import { createRequire } from "node:module";
import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { compileFromFile } from "json-schema-to-typescript";

const require = createRequire(import.meta.url);
const Ajv2020 = require("ajv/dist/2020.js");
const addFormats = require("ajv-formats");

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const schemasDir = path.join(repoRoot, "contracts/schemas");
const generatedDir = path.join(repoRoot, "contracts/generated/ts");
const authoringGenerated = path.join(
  repoRoot,
  "authoring/packages/authoring/src/generated",
);
const fixturesRoot = path.join(repoRoot, "contracts/fixtures");
const defaultsPath = path.join(repoRoot, "contracts/defaults/v1.defaults.json");
const UINT64_MAX = 18446744073709551615n;
const JS_SAFE_MAX = 9007199254740991n;

const banner = `/* eslint-disable */
/**
 * Generated from contracts/schemas. Do not edit by hand.
 * Regenerate with: npm run contracts:generate
 */
`;

function fail(message) {
  console.error(message);
  process.exitCode = 1;
}

async function listJsonFiles(dir) {
  const entries = await readdir(dir, { withFileTypes: true });
  return entries
    .filter((entry) => entry.isFile() && entry.name.endsWith(".json"))
    .map((entry) => path.join(dir, entry.name))
    .sort();
}

function parseUint64Decimal(text) {
  if (typeof text !== "string") {
    return { ok: false, reason: "not a string" };
  }
  if (!/^(0|[1-9][0-9]{0,19})$/.test(text)) {
    return { ok: false, reason: "malformed decimal" };
  }
  const value = BigInt(text);
  if (value > UINT64_MAX) {
    return { ok: false, reason: "overflow uint64" };
  }
  return { ok: true, value };
}

function seedSemanticallyValid(seed) {
  if (typeof seed === "number") {
    return Number.isInteger(seed) && seed >= 0 && seed <= Number(JS_SAFE_MAX);
  }
  if (typeof seed === "string") {
    return parseUint64Decimal(seed).ok;
  }
  return false;
}

async function expectedGenerated(schemaFile) {
  const body = await compileFromFile(path.join(schemasDir, schemaFile), {
    bannerComment: "",
    cwd: schemasDir,
    declareExternallyReferenced: true,
    enableConstEnums: false,
    strictIndexSignatures: true,
    unreachableDefinitions: true,
    additionalProperties: false,
  });
  return `${banner}\n${body.trimEnd()}\n`;
}

async function checkDrift() {
  for (const name of ["authoring-v1.ts", "scene-ir-v1.ts"]) {
    const schemaName = name.replace(".ts", ".schema.json");
    const expected = await expectedGenerated(schemaName);
    const actual = await readFile(path.join(generatedDir, name), "utf8");
    if (actual !== expected) {
      fail(`TypeScript drift detected for ${name}; run npm run contracts:generate`);
    }
    const authoringCopy = await readFile(path.join(authoringGenerated, name), "utf8");
    if (authoringCopy !== expected) {
      fail(`Authoring generated copy drifted for ${name}`);
    }
  }
}

async function checkFixtures() {
  const ajv = new Ajv2020.default({
    allErrors: true,
    strict: true,
    validateFormats: true,
  });
  addFormats.default(ajv);

  const authoringSchema = JSON.parse(
    await readFile(path.join(schemasDir, "authoring-v1.schema.json"), "utf8"),
  );
  const sceneSchema = JSON.parse(
    await readFile(path.join(schemasDir, "scene-ir-v1.schema.json"), "utf8"),
  );
  const validateAuthoring = ajv.compile(authoringSchema);
  const validateScene = ajv.compile(sceneSchema);

  const suites = [
    {
      kind: "authoring",
      validate: validateAuthoring,
      positive: path.join(fixturesRoot, "authoring/positive"),
      negative: path.join(fixturesRoot, "authoring/negative"),
    },
    {
      kind: "scene-ir",
      validate: validateScene,
      positive: path.join(fixturesRoot, "scene-ir/positive"),
      negative: path.join(fixturesRoot, "scene-ir/negative"),
    },
  ];

  for (const suite of suites) {
    for (const file of await listJsonFiles(suite.positive)) {
      const doc = JSON.parse(await readFile(file, "utf8"));
      const schemaOk = suite.validate(doc);
      const seedOk = seedSemanticallyValid(doc.seed);
      if (!schemaOk || !seedOk) {
        fail(
          `positive fixture failed (${suite.kind}): ${path.relative(repoRoot, file)}\n${JSON.stringify(suite.validate.errors, null, 2)}`,
        );
      }
    }
    for (const file of await listJsonFiles(suite.negative)) {
      const doc = JSON.parse(await readFile(file, "utf8"));
      const schemaOk = suite.validate(doc);
      const seedOk = seedSemanticallyValid(doc.seed);
      if (schemaOk && seedOk) {
        fail(
          `negative fixture unexpectedly valid (${suite.kind}): ${path.relative(repoRoot, file)}`,
        );
      }
    }
  }
}

async function checkDefaults() {
  const defaults = JSON.parse(await readFile(defaultsPath, "utf8"));
  const encoded = defaults.integer_encoding;
  if (encoded.js_safe_integer_max !== Number(JS_SAFE_MAX)) {
    fail("defaults js_safe_integer_max mismatch");
  }
  if (encoded.uint64_max_decimal !== String(UINT64_MAX)) {
    fail("defaults uint64_max_decimal mismatch");
  }
  const required = [
    "authoring.node.visible",
    "authoring.shot.camera.projection",
    "authoring.shot.camera.near",
    "authoring.shot.camera.far",
    "scene_ir.cameras",
    "scene_ir.transition_spans",
    "scene_ir.rigs",
    "scene_ir.node.visible",
    "scene_ir.camera.projection",
  ];
  for (const key of required) {
    if (!(key in defaults.documented_defaults)) {
      fail(`missing documented default: ${key}`);
    }
  }
}

await checkDrift();
await checkFixtures();
await checkDefaults();
if (process.exitCode) {
  process.exit(process.exitCode);
}
console.log("contracts check passed");
