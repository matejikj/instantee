# Authoring and CLI API

This page is the contributor-facing API map. The frozen TypeScript surface is
documented in detail in
[`packages/authoring/PUBLIC_API.md`](packages/authoring/PUBLIC_API.md).

## TypeScript SDK (`@instantee/authoring`)

Import **only** from `@instantee/authoring`. Do not deep-import `src/`,
`generated/`, or `dist/`.

| Namespace / export | Purpose |
| --- | --- |
| `createFilm` / `film.create` | Build an `AuthoringFilm` value |
| `filmToJson` / `film.toJson` | Serialize for the native CLI |
| `direction.*` | Semantic intents (`reveal`, `explain`, `compare`, `focus`, …) |
| `styles` / `STYLE_PACKAGES` | Locked V1 tags (`editorial@1`, …) |
| `sceneGraph.*` | Hierarchy helpers |
| `time.*` | Seconds ↔ frames |
| `AuthoringError` | Stable `code` + optional JSON path |

SDK validation is convenience only. The C++ compiler remains the security and
schema boundary (`instantee validate` / `compile`).

### Minimal film

```js
import {createFilm, direction, filmToJson, styles} from "@instantee/authoring";

const film = createFilm({
  filmId: "demo",
  style: styles.editorial,
  seed: 42,
  shots: [{
    id: "s0",
    durationFrames: 30,
    intent: direction.reveal(0.8, {targetIds: ["headline"]}),
    nodes: [{id: "headline", text: "HELLO", role: "headline", x: 40, y: 200}],
  }],
});
process.stdout.write(filmToJson(film));
```

## Native CLI (`instantee`)

Build the release preset, then use the binary at `build/release/instantee`.

| Command | Role |
| --- | --- |
| `validate <authoring.json>` | Parse + compile without writing Scene IR |
| `compile <authoring.json> --out <scene.json>` | Authoring → Scene IR |
| `render <scene.json> --out <video.mp4>` | Scene IR → H.264 MP4 |
| `frame <scene.json> --at <N>` | Single-frame evaluate / optional PNG |
| `lint <findings.json>` | Quality lint policy |
| `benchmark …` | Harness timings (**not** certified M1 Air claims) |

Common flags:

- `--style-bible <path>` — Style Bible tokens for CDS resolve/lint
- `--frames <N>` — render only the first N frames (smoke / tutorials)
- `--overwrite` — replace an existing output path
- `--json-log` — NDJSON diagnostics on stderr

Exit codes are stable process boundaries; errors always report
`code` / `phase` / `published` / `durability` on stderr.

## End-to-end tutorials

Follow [`tutorials/README.md`](../tutorials/README.md) for Editorial, Notebook
and Paper Cinema samples that write authoring JSON and render three MP4 files.
