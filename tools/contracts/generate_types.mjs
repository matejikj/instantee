#!/usr/bin/env node
/**
 * Generate checked-in TypeScript types from contracts/schemas.
 * json-schema-to-typescript is the only codegen path; hand edits of
 * contracts/generated/ts must fail the drift check.
 */
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { compileFromFile } from "json-schema-to-typescript";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const schemasDir = path.join(repoRoot, "contracts/schemas");
const outDir = path.join(repoRoot, "contracts/generated/ts");
const authoringCopyDir = path.join(
  repoRoot,
  "authoring/packages/authoring/src/generated",
);

const schemas = [
  { file: "authoring-v1.schema.json", out: "authoring-v1.ts" },
  { file: "scene-ir-v1.schema.json", out: "scene-ir-v1.ts" },
  { file: "style-bible-v1.schema.json", out: "style-bible-v1.ts" },
];

const banner = `/* eslint-disable */
/**
 * Generated from contracts/schemas. Do not edit by hand.
 * Regenerate with: npm run contracts:generate
 */
`;

async function generateOne(schemaFile, outName) {
  const schemaPath = path.join(schemasDir, schemaFile);
  const body = await compileFromFile(schemaPath, {
    bannerComment: "",
    cwd: schemasDir,
    declareExternallyReferenced: true,
    enableConstEnums: false,
    strictIndexSignatures: true,
    unreachableDefinitions: true,
    additionalProperties: false,
  });
  const text = `${banner}\n${body.trimEnd()}\n`;
  await writeFile(path.join(outDir, outName), text, "utf8");
  await writeFile(path.join(authoringCopyDir, outName), text, "utf8");
  return outName;
}

await mkdir(outDir, { recursive: true });
await mkdir(authoringCopyDir, { recursive: true });
const written = [];
for (const entry of schemas) {
  written.push(await generateOne(entry.file, entry.out));
}
await writeFile(
  path.join(authoringCopyDir, "index.ts"),
  `export type {
  InstanteeAuthoringManifestV1 as AuthoringManifestV1,
  Uint64Value as AuthoringUint64Value,
  Id as AuthoringId,
  Node as AuthoringNode,
} from "./authoring-v1.js";
export type {
  InstanteeSceneIRV1 as SceneIrV1,
  Uint64Value as SceneIrUint64Value,
  Id as SceneIrId,
} from "./scene-ir-v1.js";
export type {
  InstanteeStyleBibleV1 as StyleBibleV1,
} from "./style-bible-v1.js";
`,
  "utf8",
);
console.log(`generated: ${written.join(", ")}`);
