# libtess2 pin

Vendored `Include/tesselator.h` and `Source/` from memononen/libtess2 `v1.0.2`
(SGI Free Software License B 2.0). See `PIN.json` for the archive identity.

Linked only as a **private** dependency of `instantee::geometry` for flattened
polygon fills. Public geometry headers must not include `tesselator.h`. Stroke
expansion lives in the geometry module and does not use this library.
