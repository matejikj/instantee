# libwebp pin

Decode-only subset of webmproject/libwebp `v1.5.0` (BSD-3-Clause), plus the
`src/enc/*.h` headers required by lossless decode includes. See `PIN.json`
for the archive identity.

Linked only as a **private** dependency of `instantee::asset_pipeline`. Public
asset-pipeline headers must not include `webp/*.h`.
