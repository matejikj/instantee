#!/usr/bin/env node
/**
 * Tutorial sample: Scientific Notebook style film via the public SDK only.
 */
import {mkdirSync, writeFileSync, readFileSync} from "node:fs";
import {dirname, resolve} from "node:path";
import {fileURLToPath} from "node:url";
import {
  createFilm,
  direction,
  filmToJson,
  styles,
} from "@instantee/authoring";

const selfPath = fileURLToPath(import.meta.url);
const source = readFileSync(selfPath, "utf8");
for (const pattern of [
  /from\s+['"][^'"]*\/src\//,
  /from\s+['"][^'"]*generated\//,
  /from\s+['"][^'"]*\/dist\//,
]) {
  if (pattern.test(source)) {
    console.error(`forbidden import matching ${pattern}`);
    process.exit(1);
  }
}

const outFlag = process.argv.indexOf("--out");
if (outFlag < 0 || !process.argv[outFlag + 1]) {
  console.error("usage: node main.mjs --out <authoring.json>");
  process.exit(2);
}
const outPath = resolve(process.argv[outFlag + 1]);

const film = createFilm({
  filmId: "tutorial-notebook",
  style: styles.scientificNotebook,
  seed: 7,
  shots: [
    {
      id: "explain",
      durationFrames: 30,
      intent: direction.explain(0.75, {
        importance: 0.85,
        targetIds: ["theorem"],
      }),
      nodes: [
        {
          id: "theorem",
          text: "a^2 + b^2 = c^2",
          role: "headline",
          x: 64,
          y: 280,
        },
        {
          id: "note",
          text: "Right triangle identity",
          role: "body",
          x: 64,
          y: 420,
        },
      ],
    },
  ],
});

mkdirSync(dirname(outPath), {recursive: true});
writeFileSync(outPath, `${filmToJson(film)}\n`, "utf8");
console.log(
  JSON.stringify({
    style: styles.scientificNotebook,
    out: outPath,
    styleBible:
      "contracts/fixtures/style-bible/positive/scientific-notebook.style.json",
    assets: "assets/notebook/",
  }),
);
