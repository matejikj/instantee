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
  for (const name of [
    "authoring-v1.ts",
    "scene-ir-v1.ts",
    "style-bible-v1.ts",
    "paper-material-params-v1.ts",
    "puppet-rig-v1.ts",
  ]) {
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
  const styleBibleSchema = JSON.parse(
    await readFile(path.join(schemasDir, "style-bible-v1.schema.json"), "utf8"),
  );
  const paperMaterialSchema = JSON.parse(
    await readFile(
      path.join(schemasDir, "paper-material-params-v1.schema.json"),
      "utf8",
    ),
  );
  const puppetRigSchema = JSON.parse(
    await readFile(path.join(schemasDir, "puppet-rig-v1.schema.json"), "utf8"),
  );
  const validateAuthoring = ajv.compile(authoringSchema);
  const validateScene = ajv.compile(sceneSchema);
  const validateStyleBible = ajv.compile(styleBibleSchema);
  const validatePaperMaterial = ajv.compile(paperMaterialSchema);
  const validatePuppetRig = ajv.compile(puppetRigSchema);

  const suites = [
    {
      kind: "authoring",
      validate: validateAuthoring,
      requireSeed: true,
      positive: path.join(fixturesRoot, "authoring/positive"),
      negative: path.join(fixturesRoot, "authoring/negative"),
    },
    {
      kind: "scene-ir",
      validate: validateScene,
      requireSeed: true,
      positive: path.join(fixturesRoot, "scene-ir/positive"),
      negative: path.join(fixturesRoot, "scene-ir/negative"),
    },
    {
      kind: "style-bible",
      validate: validateStyleBible,
      requireSeed: false,
      positive: path.join(fixturesRoot, "style-bible/positive"),
      negative: path.join(fixturesRoot, "style-bible/negative"),
    },
    {
      kind: "paper-material",
      validate: validatePaperMaterial,
      requireSeed: false,
      positive: path.join(fixturesRoot, "paper-material/positive"),
      negative: path.join(fixturesRoot, "paper-material/negative"),
    },
    {
      kind: "puppet-rig",
      validate: validatePuppetRig,
      requireSeed: false,
      positive: path.join(fixturesRoot, "puppet-rig/positive"),
      negative: path.join(fixturesRoot, "puppet-rig/negative"),
      semanticNegative: path.join(fixturesRoot, "puppet-rig/semantic-negative"),
    },
  ];

  for (const suite of suites) {
    for (const file of await listJsonFiles(suite.positive)) {
      const doc = JSON.parse(await readFile(file, "utf8"));
      const schemaOk = suite.validate(doc);
      const seedOk = suite.requireSeed ? seedSemanticallyValid(doc.seed) : true;
      if (!schemaOk || !seedOk) {
        fail(
          `positive fixture failed (${suite.kind}): ${path.relative(repoRoot, file)}\n${JSON.stringify(suite.validate.errors, null, 2)}`,
        );
      }
    }
    for (const file of await listJsonFiles(suite.negative)) {
      const doc = JSON.parse(await readFile(file, "utf8"));
      const schemaOk = suite.validate(doc);
      const seedOk = suite.requireSeed ? seedSemanticallyValid(doc.seed) : true;
      if (schemaOk && seedOk) {
        fail(
          `negative fixture unexpectedly valid (${suite.kind}): ${path.relative(repoRoot, file)}`,
        );
      }
    }
    if (suite.semanticNegative) {
      for (const file of await listJsonFiles(suite.semanticNegative)) {
        const doc = JSON.parse(await readFile(file, "utf8"));
        if (!suite.validate(doc)) {
          fail(
            `semantic-negative fixture must remain schema-valid (${suite.kind}): ${path.relative(repoRoot, file)}\n${JSON.stringify(suite.validate.errors, null, 2)}`,
          );
        }
      }
    }
  }
}

async function listDirs(dir) {
  const entries = await readdir(dir, { withFileTypes: true });
  return entries.filter((entry) => entry.isDirectory()).map((entry) => entry.name).sort();
}

async function checkMigrations(validateAuthoring) {
  const migrationsRoot = path.join(fixturesRoot, "authoring/migrations");
  const cases = await listDirs(migrationsRoot);
  if (cases.length === 0) {
    fail("missing authoring migration fixtures");
    return;
  }
  for (const name of cases) {
    const inputPath = path.join(migrationsRoot, name, "input.authoring.json");
    const expectedPath = path.join(migrationsRoot, name, "expected.authoring.json");
    const input = JSON.parse(await readFile(inputPath, "utf8"));
    const expected = JSON.parse(await readFile(expectedPath, "utf8"));
    if (validateAuthoring(input)) {
      fail(`migration input unexpectedly valid for current schema: ${name}`);
    }
    if (!validateAuthoring(expected) || !seedSemanticallyValid(expected.seed)) {
      fail(
        `migration expected fixture invalid (${name})\n${JSON.stringify(validateAuthoring.errors, null, 2)}`,
      );
    }
    if (expected.schema_version !== "1.0.0") {
      fail(`migration expected must target schema 1.0.0 (${name})`);
    }
  }
}

async function checkCleanClonePresence() {
  const required = [
    "contracts/README.md",
    "contracts/VERSIONING.md",
    "contracts/schemas/authoring-v1.schema.json",
    "contracts/schemas/scene-ir-v1.schema.json",
    "contracts/schemas/asset-lockfile-v1.schema.json",
    "contracts/schemas/puppet-rig-v1.schema.json",
    "contracts/defaults/v1.defaults.json",
    "contracts/fixtures/puppet-rig/positive/paper-human-basic.json",
    "contracts/fixtures/puppet-rig/negative/unsupported-retarget.json",
    "contracts/fixtures/puppet-rig/semantic-negative/cyclic-parent.json",
    "contracts/fixtures/authoring/positive/minimal.authoring.json",
    "contracts/fixtures/authoring/positive/cds-sdk-intents.authoring.json",
    "contracts/fixtures/authoring/positive/unicode-ids.authoring.json",
    "contracts/fixtures/authoring/positive/nested-hierarchy.authoring.json",
    "contracts/fixtures/authoring/migrations/from-0.9.0/input.authoring.json",
    "contracts/fixtures/authoring/migrations/from-0.9.0/expected.authoring.json",
    "contracts/fixtures/scene-ir/positive/minimal.scene.json",
    "contracts/fixtures/scene-ir/semantic-negative/cyclic-parent.scene.json",
    "contracts/fixtures/scene-ir/semantic-negative/dangling-ref.scene.json",
    "contracts/invariants-sas6.json",
  ];
  for (const rel of required) {
    try {
      await readFile(path.join(repoRoot, rel));
    } catch {
      fail(`clean-clone contracts presence missing: ${rel}`);
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
{
  const ajv = new Ajv2020.default({
    allErrors: true,
    strict: true,
    validateFormats: true,
  });
  addFormats.default(ajv);
  const authoringSchema = JSON.parse(
    await readFile(path.join(schemasDir, "authoring-v1.schema.json"), "utf8"),
  );
  await checkMigrations(ajv.compile(authoringSchema));
}
await checkCleanClonePresence();
await checkDefaults();
if (process.exitCode) {
  process.exit(process.exitCode);
}
console.log("contracts check passed");
