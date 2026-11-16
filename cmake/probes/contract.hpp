#pragma once

#include <version>

static_assert(__cplusplus == 202302L, "C++23 is required");
#ifndef __STRICT_ANSI__
#error "C++ language extensions must be disabled"
#endif
#ifndef __apple_build_version__
#error "The pinned Apple Clang is required"
#else
static_assert(__apple_build_version__ == 21000401, "Apple Clang build drift");
#endif
#ifndef _LIBCPP_VERSION
#error "libc++ is required"
#else
static_assert(_LIBCPP_VERSION == 220106, "libc++ version drift");
#endif
#ifndef __arm64__
#error "arm64 is required"
#endif
#ifdef __OBJC__
#if !__has_feature(objc_arc)
#error "Objective-C++ requires ARC"
#endif
#endif
