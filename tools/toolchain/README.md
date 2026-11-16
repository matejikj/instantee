# Verified native toolchain

From the repository root on an arm64 Mac with the selected Xcode:

```sh
npm ci --ignore-scripts --no-audit --no-fund
npm run check:toolchain
cmake --preset toolchain
cmake --build --preset toolchain
ctest --preset toolchain
python3 -m unittest discover -s tools/toolchain -p 'test_*.py' -v
```

Certification presets (`toolchain`, `release`, sanitizers) set
`INSTANTEE_TOOLCHAIN_MODE=certification` and enforce the exact pin below plus
the Node/TypeScript lock. The `dev` preset sets `development` mode: same
Darwin/arm64/Ninja/AppleClang and feature probes, without exact version or Node
requirements. Use `dev` for local iteration and CI smoke; use certification
presets for reference evidence.

`cmake/ToolchainLock.cmake` pins CMake 4.3.0, Ninja 1.13.2,
Apple Clang 21.0.0.21000401, Xcode 27.2 / 27B52 and macOS SDK 27.2 / 26B118.
Clang-tidy is pinned separately in [CLANG_TIDY.md](CLANG_TIDY.md).
The probes also verify Apple compiler build 21000401 and libc++ 220106.
`package.json`, `.nvmrc`, `.npmrc` and `package-lock.json` pin Node 22.15.0,
npm 11.14.0 and TypeScript 5.9.3, including package integrity.
Node/TypeScript support authoring builds and are not runtime dependencies.

Configuration rejects different versions, platforms, architectures, deployment
targets or libraries. It resolves the active Xcode SDK through `xcrun`; an
explicit sysroot is verified using that SDK's own metadata. Both C++ and
Objective-C++ require C++23 without language extensions. Objective-C++ requires
ARC. The probes cannot be disabled. Every configuration recompiles and runs
four programs in both languages: expected value/error and monadic transform,
jthread stop/join, a stop callback with an idempotent stop request, and a
condition-variable wait awakened by stop. Wait checks also cover a previously
stopped token and a satisfied predicate.

Every program has a ten-second runtime limit during configuration and CTest.
Negative tests shadow library headers in isolation and check rejection of
version drift, nonzero exits and hung programs, plus repeated probing after a
header change. Successful results are not cached. Cross-compilation/emulation
cannot replace native verification. The build produces eight probe programs
and the [minimal CLI workspace](../workspace/README.md).

The minimum macOS version of 27.2 is currently a feasibility-build lock for the
verified machine, not a promise of export support on older systems. Final video
API support requires a native export experiment. Lock changes require another
clean build, negative tests and evidence on the reference hardware. This is not
performance certification.

`vcpkg.json` pins the baseline of the public `2025.10.17` tag, verified at commit
`74e6536215718009aae747d86d84b78376bf9e09`. No C/C++ packages are used yet;
configuration neither downloads nor bootstraps vcpkg. The first package
integration must use manifest mode and the same vcpkg checkout rather than a
moving HEAD.

The contract follows [CMake compile/run support](https://cmake.org/cmake/help/v4.3/command/try_compile.html),
the [required Objective-C++ standard](https://cmake.org/cmake/help/v4.3/prop_tgt/OBJCXX_STANDARD.html)
and [vcpkg versioning](https://learn.microsoft.com/en-us/vcpkg/users/versioning).
