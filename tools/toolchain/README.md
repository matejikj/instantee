# Ověřený nativní toolchain

Z kořene projektu na arm64 Macu s vybraným Xcode:

```sh
npm ci --ignore-scripts --no-audit --no-fund
npm run check:toolchain
cmake --preset toolchain
cmake --build --preset toolchain
ctest --preset toolchain
python3 -m unittest discover -s tools/toolchain -p 'test_*.py' -v
```

`cmake/ToolchainLock.cmake` zamyká CMake 4.3.0, Ninja 1.13.2,
Apple Clang 21.0.0.21000401, Xcode 27.2 / 27B52 a macOS SDK 27.2 / 26B118.
Probes kontrolují i Apple compiler build 21000401 a libc++ 220106.
`package.json`, `.nvmrc`, `.npmrc` a `package-lock.json` zamykají Node 22.15.0,
npm 11.14.0 a TypeScript 5.9.3 s integritou distribuovaného balíčku.
Node/TypeScript slouží sestavení authoringu, nejsou závislostí runtime.

Konfigurace odmítá jinou verzi, platformu, architekturu, deployment target
nebo knihovnu. Používá SDK z aktivního Xcode přes `xcrun`; explicitní sysroot
se kontroluje podle vlastních SDK metadat. C++23 je povinný v C++ i Objective-C++,
bez jazykových extensions; Objective-C++ vyžaduje ARC. Probe nelze vypnout.
Při každé konfiguraci se znovu sestaví a spustí čtyři programy v obou jazycích:
expected value/error a monadic transform, jthread stop/join, stop callback
s idempotentním stop requestem a condition-variable wait probuzený stopem.
Čekání kontroluje také předem zastavený token a splněný predicate.

Každý program má desetisekundový runtime limit v configure i CTest.
Negativní testy izolovaně zastíní knihovní headery, ověří odmítnutí změn
verzí, nenulového výsledku a zaseknutého programu i opakování probes po
změně headeru. Výsledky se necachují; není povolen cross-compile/emulátor
jako náhrada nativního ověření. Build vytváří osm probe programů a
[minimální workspace s CLI](../workspace/README.md).

Minimum macOS 27.2 je nyní zámek pro feasibility build na ověřeném stroji,
nikoli slib podpory exportu na starších systémech. Finální podpora video API
vyžaduje nativní exportní experiment. Změny zámku vyžadují opakovat čistý build,
negativní testy a evidenci na referenčním hardwaru; nejde o výkonnostní certifikaci.

`vcpkg.json` připíná baseline veřejného tagu `2025.10.17`, ověřeného na commit
`74e6536215718009aae747d86d84b78376bf9e09`. Žádné C/C++ balíčky se zatím
nepoužívají a configure vcpkg nestahuje ani nebootstrapuje. Při první integraci
balíčku použít manifest mode a stejný checkout vcpkg, nikoli pohyblivý HEAD.

Kontrakt vychází z [CMake compile/run podpory](https://cmake.org/cmake/help/v4.3/command/try_compile.html),
[povinného Objective-C++ standardu](https://cmake.org/cmake/help/v4.3/prop_tgt/OBJCXX_STANDARD.html)
a [vcpkg versioningu](https://learn.microsoft.com/en-us/vcpkg/users/versioning).
