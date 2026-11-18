# TypeScript public API v1.0 freeze

`@instantee/authoring` is frozen at 1.0.0 with namespaced helpers (`film`,
`direction`, `sceneGraph`, `time`, `styles`, `api`), typed `AuthoringError`,
and a documented deprecation policy. Flat `createFilm` aliases remain.

Acceptance proof: `authoring/examples/three-styles/main.mjs` builds films for
all three locked style tags via the public package entry only. The runtime
export surface is locked by
`contracts/fixtures/authoring/public-api-v1.snapshot.json`.

Measurement: none (correctness only). Evidence tier A.
