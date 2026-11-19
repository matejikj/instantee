#!/usr/bin/env node
/**
 * Validate the schema-version matrix and locked compatibility catalog.
 * Does not claim runtime load results; those are covered by the C++ suite.
 */
import { readFile, access } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const Ajv2020 = require("ajv/dist/2020.js");
const addFormats = require("ajv-formats");

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const matrixPath = path.join(
  repoRoot,
  "contracts/compatibility/schema-version-matrix-v1.json",
);
const catalogPath = path.join(
  repoRoot,
  "contracts/compatibility/locked-examples-v1.json",
);

function fail(message) {
  console.error(message);
  process.exitCode = 1;
}

async function exists(rel) {
  try {
    await access(path.join(repoRoot, rel));
    return true;
  } catch {
    return false;
  }
}

const matrix = JSON.parse(await readFile(matrixPath, "utf8"));
const catalog = JSON.parse(await readFile(catalogPath, "utf8"));

if (matrix.unknown_fields?.policy !== "reject") {
  fail("unknown_fields.policy must be reject");
}
if (matrix.unknown_fields?.error_code !== "unknown_field") {
  fail("unknown_fields.error_code must be unknown_field");
}

const families = matrix.families ?? {};
for (const [name, family] of Object.entries(families)) {
  if (!family.current) {
    fail(`family ${name} missing current version`);
  }
  if (!(await exists(path.join("contracts", family.schema)))) {
    fail(`family ${name} schema missing: ${family.schema}`);
  }
  const supported = family.supported ?? [];
  if (!supported.some((row) => row.version === family.current && row.status === "current")) {
    fail(`family ${name} must list current version as status=current`);
  }
  for (const row of supported) {
    if (row.status === "migratable") {
      if (!row.fixture) {
        fail(`migratable ${name}@${row.version} missing fixture`);
      } else if (!(await exists(path.join("contracts", row.fixture)))) {
        fail(`missing migration fixture dir: ${row.fixture}`);
      }
    }
  }
  if (!Array.isArray(family.rejected_examples) || family.rejected_examples.length === 0) {
    fail(`family ${name} needs rejected_examples`);
  }
}

const authoring = families.authoring;
if (!authoring?.supported?.some((row) => row.version === "0.9.0" && row.status === "migratable")) {
  fail("authoring must keep 0.9.0 migratable");
}

const examples = catalog.examples ?? [];
if (examples.length < 8) {
  fail("locked-examples catalog is too small");
}

const ajv = new Ajv2020.default({ allErrors: true, strict: true, validateFormats: true });
addFormats.default(ajv);
/** @type {Map<string, import('ajv').ValidateFunction>} */
const validators = new Map();
async function validatorFor(familyName) {
  if (validators.has(familyName)) {
    return validators.get(familyName);
  }
  const family = families[familyName];
  if (!family) {
    fail(`catalog references unknown family ${familyName}`);
    return null;
  }
  const schema = JSON.parse(
    await readFile(path.join(repoRoot, "contracts", family.schema), "utf8"),
  );
  const validate = ajv.compile(schema);
  validators.set(familyName, validate);
  return validate;
}

for (const entry of examples) {
  if (!(await exists(entry.path))) {
    fail(`locked example missing: ${entry.path}`);
    continue;
  }
  const doc = JSON.parse(await readFile(path.join(repoRoot, entry.path), "utf8"));
  const validate = await validatorFor(entry.family);
  if (!validate) {
    continue;
  }
  if (entry.expect === "load") {
    if (!validate(doc)) {
      fail(
        `load example must satisfy current schema (${entry.id})\n${JSON.stringify(validate.errors, null, 2)}`,
      );
    }
  } else if (entry.expect === "migrate_and_load") {
    if (validate(doc)) {
      fail(`migrate input must not already validate as current (${entry.id})`);
    }
    if (doc.schema_version !== entry.from_version) {
      fail(`${entry.id} from_version mismatch`);
    }
  } else if (entry.expect === "unsupported_schema") {
    if (doc.schema_version === families[entry.family]?.current) {
      fail(`${entry.id} unsupported example still on current version`);
    }
  } else if (entry.expect === "unknown_field") {
    if (validate(doc)) {
      fail(`${entry.id} unknown_field example unexpectedly schema-valid`);
    }
  } else {
    fail(`${entry.id} has unknown expect ${entry.expect}`);
  }
}

if (process.exitCode) {
  process.exit(process.exitCode);
}
console.log(
  `schema compatibility policy ok (${Object.keys(families).length} families, ${examples.length} locked examples)`,
);
