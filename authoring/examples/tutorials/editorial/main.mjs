#!/usr/bin/env node
/**
 * Tutorial sample: Editorial style film via the public SDK only.
 * Writes authoring JSON for the native compile → render path.
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
  filmId: "tutorial-editorial",
  style: styles.editorial,
  seed: 42,
  shots: [
    {
      id: "hook",
      durationFrames: 30,
      intent: direction.reveal(0.85, {
        importance: 0.9,
        targetIds: ["headline"],
      }),
      nodes: [
        {
          id: "headline",
          text: "MARKETS OPEN UNDER PRESSURE",
          role: "headline",
          x: 48,
          y: 220,
        },
        {
          id: "kicker",
          text: "Live desk",
          role: "subhead",
          x: 48,
          y: 360,
        },
      ],
    },
  ],
});

mkdirSync(dirname(outPath), {recursive: true});
writeFileSync(outPath, `${filmToJson(film)}\n`, "utf8");
console.log(
  JSON.stringify({
    style: styles.editorial,
    out: outPath,
    styleBible: "contracts/fixtures/style-bible/positive/editorial.style.json",
    assets: "assets/editorial/",
  }),
);
