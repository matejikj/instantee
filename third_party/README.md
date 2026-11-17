# Third-party pins

Vendored sources that are not managed through the vcpkg manifest live here with
an immutable archive identity.

| Path | Source | License |
| --- | --- | --- |
| `metal-cpp/` | Apple metal-cpp tag `release/metal-cpp_macOS27.0.1_iOS27.0.1` | Apache-2.0 |
| `google-benchmark/` | Google Benchmark tag `v1.9.1` | Apache-2.0 |
| `libtess2/` | memononen/libtess2 tag `v1.0.2` | SGI-B-2.0 |
| `stb/` | nothings/stb `stb_image.h` commit `9600522` | MIT OR Unlicense |
| `libwebp/` | webmproject/libwebp tag `v1.5.0` (decode subset) | BSD-3-Clause |

Each pin directory contains `PIN.json` with the tag, commit and archive SHA-256
used to verify the snapshot. Prefer replacing a pin as a whole. Document any
minimal toolchain compatibility patch in that pin's `PIN.json` notes.
