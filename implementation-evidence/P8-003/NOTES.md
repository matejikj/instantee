# C++ public/private API surface freeze

`tools/dev/check_cpp_api_surface.py` lints the frozen public-header inventory
and CMake visibility rules:

- forbidden Apple/Metal/third-party includes in any public header
- Apple/Metal tokens in domain public headers
- impl/native/owner/detail headers must not sit under `include/`
- Apple frameworks and metal-cpp stay PRIVATE (domain modules must not probe them)
- locked snapshot at `contracts/cpp/public-headers-v1.snapshot.json` (163 headers)

Isolated public-header compiles and module-boundary checks from the earlier
boundary delivery remain in force. Measurement: none. Evidence tier A.
