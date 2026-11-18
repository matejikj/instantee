#!/usr/bin/env node
/**
 * Verified project example: all three V1 styles via the public package entry.
 * Must not import package internals (src/, generated/, build paths).
 */
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";
import {
  PUBLIC_API_VERSION,
  api,
  createFilm,
  direction,
  filmToJson,
  styles,
} from "@instantee/authoring";

const selfPath = fileURLToPath(import.meta.url);
const source = readFileSync(selfPath, "utf8");
const forbidden = [
  /from\s+['"][^'"]*\/src\//,
  /from\s+['"][^'"]*generated\//,
  /from\s+['"][^'"]*\/dist\//,
  /from\s+['"]\.\.?\/.+/,
];
for (const pattern of forbidden) {
  if (pattern.test(source)) {
    console.error(`three-styles example has forbidden import matching ${pattern}`);
    process.exit(1);
  }
}

if (PUBLIC_API_VERSION !== "1.0.0" || api.version !== "1.0.0") {
  console.error(`expected public API 1.0.0, got ${PUBLIC_API_VERSION}`);
  process.exit(1);
}

const styleList = [
  styles.editorial,
  styles.scientificNotebook,
  styles.paperCinema,
];
if (styleList.length !== 3 || new Set(styleList).size !== 3) {
  console.error("expected three distinct locked style package ids");
  process.exit(1);
}

const films = styleList.map((style) =>
  createFilm({
    filmId: `three-styles-${style.replace(/[@/]/g, "-")}`,
    style,
    seed: 42,
    shots: [
      {
        id: "opening",
        durationFrames: 90,
        intent: direction.reveal(0.6, {
          importance: 0.85,
          targetIds: ["headline"],
        }),
        nodes: [
          {
            id: "headline",
            text: style,
            role: "headline",
            x: 40,
            y: 200,
          },
        ],
      },
    ],
  }),
);

for (const filmDoc of films) {
  if (!styleList.includes(filmDoc.style)) {
    console.error(`unexpected style on film ${filmDoc.film_id}: ${filmDoc.style}`);
    process.exit(1);
  }
  if (filmDoc.shots[0]?.intent?.type !== "reveal") {
    console.error(`missing reveal intent on ${filmDoc.film_id}`);
    process.exit(1);
  }
  // Touch serialization path used by CLI fixtures.
  if (!filmToJson(filmDoc).includes(filmDoc.film_id)) {
    console.error(`filmToJson missing film id for ${filmDoc.film_id}`);
    process.exit(1);
  }
}

console.log(
  JSON.stringify(
    {
      apiVersion: PUBLIC_API_VERSION,
      styles: styleList,
      filmIds: films.map((filmDoc) => filmDoc.film_id),
    },
    null,
    2,
  ),
);
