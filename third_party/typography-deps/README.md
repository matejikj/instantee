# Typography native dependencies

HarfBuzz, FreeType and ICU are linked privately by `instantee::typography`.
They are not vendored here; CMake resolves them with pkg-config and enforces
the minimum versions in `PIN.json`.
