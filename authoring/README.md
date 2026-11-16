# Authoring workspace

This is the TypeScript side of Instantee: the future code-first authoring SDK
and its authoring-time helpers. It prepares documents for the native engine;
it does not participate in the engine's per-frame evaluation or export loop.
It is not a graphical frontend.

The npm workspace is managed from the repository root using the shared lockfile
and pinned TypeScript compiler:

```sh
npm ci --ignore-scripts --no-audit --no-fund
npm run check:authoring
npm run build:authoring
```

`packages/authoring` is currently a private, buildable package shell with an empty
module entry point. It does not implement an SDK, document schema or engine
integration. Its output is generated under the ignored `build/authoring/`
directory. The package is not publishable and has no public package exports yet.

Keep authoring packages under `packages/<name>/`, source under each package's
`src/`, and package-specific tests beside that package when behavior is added.
All packages must provide `build` and `typecheck` scripts. The shared base
configuration enables strict checking and excludes ambient Node/browser types;
add environment-specific dependencies only to the package that needs them.

Data shared with the engine belongs to [contracts/](../contracts/README.md).
A future graphical editor should be a separate application using this SDK,
with its own UI dependencies and lifecycle. It should not be folded into the SDK.
