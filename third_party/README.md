# Third-party pins

Vendored sources that are not managed through the vcpkg manifest live here with
an immutable archive identity.

| Path | Source | License |
| --- | --- | --- |
| `metal-cpp/` | Apple metal-cpp tag `release/metal-cpp_macOS27.0.1_iOS27.0.1` | Apache-2.0 |

Each pin directory contains `PIN.json` with the tag, commit and archive SHA-256
used to verify the snapshot. Do not edit vendored headers in place; replace the
pin as a whole and update the matching CMake checks.
