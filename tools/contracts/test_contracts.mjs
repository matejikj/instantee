#!/usr/bin/env node
/**
 * Cross-language contract suite: TypeScript-shaped manifests validate against
 * schemas, and designated integer seeds round-trip through the C++ helper.
 */
import { spawnSync } from "node:child_process";
import { createRequire } from "node:module";
import { existsSync } from "node:fs";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const Ajv2020 = require("ajv/dist/2020.js");
const addFormats = require("ajv-formats");

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const schema = JSON.parse(
  await readFile(path.join(repoRoot, "contracts/schemas/authoring-v1.schema.json"), "utf8"),
);
const defaults = JSON.parse(
  await readFile(path.join(repoRoot, "contracts/defaults/v1.defaults.json"), "utf8"),
);

function fail(message) {
  console.error(message);
  process.exit(1);
}

function findSeedCheck() {
  const candidates = [
    path.join(repoRoot, "build/release/contract-seed-check"),
    path.join(repoRoot, "build/dev/contract-seed-check"),
    path.join(repoRoot, "build/asan-ubsan/contract-seed-check"),
  ];
  return candidates.find((candidate) => existsSync(candidate)) ?? null;
}

function runSeedCheck(exe, args) {
  const result = spawnSync(exe, args, { encoding: "utf8" });
  return {
    status: result.status,
    stdout: (result.stdout ?? "").trim(),
    stderr: (result.stderr ?? "").trim(),
  };
}

const ajv = new Ajv2020.default({ allErrors: true, strict: true });
addFormats.default(ajv);
const validate = ajv.compile(schema);

// Typed against generated contracts; kept as a plain object so this suite runs
// under Node without a separate tsc emit for tools/.
const typedManifest = {
  schema_version: "1.0.0",
  film_id: "ts-roundtrip",
  canvas: { width: 1080, height: 1920, color: "rec709-sdr" },
  frame_rate: { numerator: 30, denominator: 1 },
  seed: "18446744073709551615",
  style: "editorial@1",
  assets: [],
  rigs: [
    {
      id: "rigA",
      format_version: "0.1.0",
      payload_ref: "rigs/rigA@0.1.0",
    },
  ],
  transitions: [
    {
      id: "t0",
      from_shot_id: "a",
      to_shot_id: "b",
      overlap_frames: 0,
      blend: "cut",
    },
  ],
  shots: [
    {
      id: "a",
      duration_frames: 60,
      camera: { projection: "orthographic" },
      nodes: [
        {
          id: "title",
          type: "text",
          text: "こんにちは",
          visible: true,
          transform: { opacity: 1, translate: { x: 0, y: 0, z: 0 } },
        },
      ],
    },
    {
      id: "b",
      duration_frames: 30,
      nodes: [{ id: "mark", type: "shape", shape: "rect" }],
    },
  ],
};

if (!validate(typedManifest)) {
  fail(`typed manifest failed schema: ${JSON.stringify(validate.errors, null, 2)}`);
}

const fixture = JSON.parse(
  await readFile(
    path.join(
      repoRoot,
      "contracts/fixtures/authoring/positive/seed-uint64-max-string.authoring.json",
    ),
    "utf8",
  ),
);
if (!validate(fixture)) {
  fail("uint64 max fixture failed schema");
}

const exe = findSeedCheck();
if (exe === null) {
  fail(
    "contract-seed-check binary not found under build/{release,dev,asan-ubsan}; build the native tree first",
  );
}

const cases = [
  { args: ["parse-decimal", "0"], expect: "0", status: 0 },
  { args: ["parse-decimal", "9007199254740991"], expect: "9007199254740991", status: 0 },
  {
    args: ["parse-decimal", "18446744073709551615"],
    expect: "18446744073709551615",
    status: 0,
  },
  { args: ["parse-decimal", "18446744073709551616"], expect: null, status: 2 },
  { args: ["parse-decimal", "042"], expect: null, status: 2 },
  { args: ["from-number", "9007199254740991"], expect: "9007199254740991", status: 0 },
  { args: ["from-number", "9007199254740992"], expect: null, status: 2 },
  { args: ["preferred-form", "42"], expect: "number", status: 0 },
  {
    args: ["preferred-form", "18446744073709551615"],
    expect: "decimal_string",
    status: 0,
  },
];

for (const testCase of cases) {
  const result = runSeedCheck(exe, testCase.args);
  if (result.status !== testCase.status) {
    fail(
      `C++ seed check status mismatch for ${testCase.args.join(" ")}: got ${result.status}, want ${testCase.status}; stderr=${result.stderr}`,
    );
  }
  if (testCase.expect !== null && result.stdout !== testCase.expect) {
    fail(
      `C++ seed check output mismatch for ${testCase.args.join(" ")}: got ${result.stdout}`,
    );
  }
}

const roundTrip = runSeedCheck(exe, ["parse-decimal", String(typedManifest.seed)]);
if (roundTrip.status !== 0 || roundTrip.stdout !== typedManifest.seed) {
  fail(`TS seed did not round-trip through C++: ${roundTrip.stdout} / ${roundTrip.stderr}`);
}

if (defaults.documented_defaults["authoring.node.visible"] !== true) {
  fail("documented default node.visible drifted");
}
if (defaults.documented_defaults["scene_ir.camera.projection"] !== "orthographic") {
  fail("documented default camera.projection drifted");
}

console.log("contracts cross-language tests passed");
