#!/usr/bin/env node
/**
 * Tutorial sample: Paper Cinema style film via the public SDK only.
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
  filmId: "tutorial-paper-cinema",
  style: styles.paperCinema,
  seed: 19,
  shots: [
    {
      id: "open",
      durationFrames: 30,
      intent: direction.focus(0.7, {
        importance: 0.8,
        targetIds: ["title"],
      }),
      nodes: [
        {
          id: "title",
          text: "PAPER ARCHIVES",
          role: "headline",
          x: 72,
          y: 260,
        },
        {
          id: "caption",
          text: "A folded history",
          role: "caption",
          x: 72,
          y: 400,
        },
      ],
    },
  ],
});

mkdirSync(dirname(outPath), {recursive: true});
writeFileSync(outPath, `${filmToJson(film)}\n`, "utf8");
console.log(
  JSON.stringify({
    style: styles.paperCinema,
    out: outPath,
    styleBible: "contracts/fixtures/style-bible/positive/paper-cinema.style.json",
    assets: "assets/paper-cinema/",
  }),
);
