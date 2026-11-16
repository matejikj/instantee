# Nativní feasibility workspace

Z kořene repozitáře s [ověřeným toolchainem](../toolchain/README.md):

```sh
cmake --workflow --preset release
cmake --workflow --preset asan-ubsan
cmake --workflow --preset tsan
python3 -m unittest discover -s tools/toolchain -p 'test_*.py' -v
build/release/spike-render --help
build/release/spike-render --smoke
build/release/spike-benchmark --smoke
```

Každý workflow provede configure, build a CTest. Release používá
`RelWithDebInfo`, diagnostické buildy `Debug` a vlastní adresáře. Instrumentace
se aplikuje na všechny projektové překladové jednotky i executable linky.
ASan/UBSan a TSan se nekombinují. Sanitizers nekontrolují všechny interní
operace systémových Apple knihoven a nedokazují životnost GPU/encoder resources.
Python 3 je potřeba pro procesní smoke testy; runtime CLI jej nepoužívá.

`spike-render` a `spike-benchmark` přijímají právě jednu volbu `--help` nebo
`--smoke`. Smoke ověřuje synchronní Core Foundation roundtrip v autorelease
poolu, nevytváří soubory a vrací JSON s `published=false`, `certified=false`.
Neznámé, duplicitní a kombinované argumenty vracejí kód 2 a JSON na stderr;
uživatelský vstup se do diagnostiky neopisuje. Bez argumentů renderer vrací
kód 5 (`renderer_unavailable`), benchmark kód 2 (`benchmark_unavailable`).
Vykreslování, export a měření budou doplněny navazujícími dodávkami.
Chyba zápisu nebo flush stdout vrací kód 11 a minimální diagnostiku na stderr.

Oficiální vstup pro budoucí release benchmark:

```sh
cmake --build --preset release --target run-spike-benchmark
```

Target nejprve sestaví všechny potřebné testovací programy a CLI, poté spustí
CPU a smoke testy. Každé selhání zabrání spuštění benchmarkového procesu.
Teprve úspěšné testy dovolí spustit `spike-benchmark`; ten nyní pravdivě skončí
chybou nedostupné implementace. Target proto zatím vrací nenulový výsledek
i po úspěšném preflightu. V sanitizovaných a neoptimalizovaných konfiguracích
target není dostupný. Nejde o měřicí harness; čas testů a buildu nesmí vstoupit
do budoucího intervalu spawn→wait exportního procesu.

`engine/apps/cli` vlastní parser a oba spike executable. Sdílené chybové typy jsou
v malém header-only modulu `engine/src/diagnostics` s veřejným prefixem
`instantee/diagnostics/`. Apple adaptér je samostatná statická knihovna
v `engine/native/export-apple`, sestavovaná jako Objective-C++ s ARC. Jeho veřejné
hlavičky jsou pouze v `include/instantee/export_apple/`; privátní CF wrapper
a implementace jsou v `src/`. Adaptér závisí na diagnostice, nikoli na CLI.
Nemá persistentní stav vyžadující PImpl.

Každý modul, aplikace a skupina testů vlastní svůj `CMakeLists.txt`.
`cmake/` obsahuje pouze společná build pravidla a toolchain probes;
`benchmarks/CMakeLists.txt` vlastní preflight a benchmarkový vstup.
CPU kontraktový test překládá veřejné hlavičky bez Apple frameworků.
Buildové integrační testy kontrolují skutečné transitivní include závislosti
a odmítnutí pokusu konzumenta zahrnout privátní CF nebo CLI hlavičku.

Privátní `CfOwner<Ref>` přebírá výhradně již vlastněnou +1 Create/Copy referenci
a uvolní ji přes `CFRelease`. `get()` půjčuje referenci; přebírání +0 Get
reference vyžaduje předchozí `CFRetain`. Vlastník je move-only, přesuny a cleanup
jsou `noexcept`. ARC řídí Objective-C objekty, nikoli vlastněné CF reference.
Native test ověřuje kopii vstupních bytů, přesun vlastnictví a explicitně
retained referenci přežívající uvolnění původního vlastníka.

Použité kontrakty: [CMake workflow presety](https://cmake.org/cmake/help/v4.3/manual/cmake-presets.7.html)
a [Core Foundation ownership](https://developer.apple.com/library/archive/documentation/CoreFoundation/Conceptual/CFMemoryMgmt/Concepts/Ownership.html).
