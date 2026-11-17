// stb_image implementation TU. Keep PNG-only enablement so JPEG/HDR paths are
// not compiled into the product binary.
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
