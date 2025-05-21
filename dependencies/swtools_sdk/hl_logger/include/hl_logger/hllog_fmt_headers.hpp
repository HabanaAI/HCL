#pragma once
// on clang (and dpcpp that is based on clang) there is a warning in fmt library for inf comparison (e.g. value != inf)
// disable this warning because if warnings are treated as errors - it breaks the build
#if defined(__clang__) || defined(__INTEL_LLVM_COMPILER)
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wtautological-constant-compare"
#endif

#ifndef HLLOG_FMT_EXTERNAL
//  verify fmt lib version only for the default one
#   if defined(FMT_VERSION) && FMT_VERSION != 90100
#       error "fmt of an incompatible version was already included. if you use an external fmt - define HLLOG_FMT_EXTERNAL"
#   endif

#   ifndef FMT_HEADER_ONLY
#       define FMT_HEADER_ONLY
#   endif
//  enable simple ostream<< operator support (like in old versions of fmt)
//  this support can be disabled in the future by fmt. see fmt docs
#   define FMT_DEPRECATED_OSTREAM

#endif

#ifndef HLLOG_DISABLE_FMT_COMPILE
#   ifdef HLLOG_FMT_EXTERNAL
#       include <fmt/compile.h>
#   else
#       include <fmt-9.1.0/include/fmt/compile.h>
#   endif
#else
#   define FMT_COMPILE(v) v
#endif

#ifdef HLLOG_FMT_EXTERNAL
#   include <fmt/format.h>
#   include <fmt/ranges.h>
#   include <fmt/chrono.h>
#   include <fmt/ostream.h>
#   include <fmt/std.h>
#else
#   include <fmt-9.1.0/include/fmt/format.h>
#   include <fmt-9.1.0/include/fmt/chrono.h>
#   include <fmt-9.1.0/include/fmt/ostream.h>
#   include <fmt-9.1.0/include/fmt/std.h>
#endif

#if defined(__clang__) || defined(__INTEL_LLVM_COMPILER)
#pragma clang diagnostic pop
#endif