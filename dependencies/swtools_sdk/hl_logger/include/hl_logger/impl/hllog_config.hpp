#pragma once
#define FMT_HEADER_ONLY

#define HLLOG_API __attribute__((visibility("default")))

#define HLLOG_COMBINE_(a, b) a##b
#define HLLOG_COMBINE(a, b)  HLLOG_COMBINE_(a, b)

#if (_GLIBCXX_USE_CXX11_ABI == 0) || defined(HLLOG_USE_ABI0)
#  define HLLOG_ABI_SUFFIX _abi0
#  ifndef HLLOG_USE_ABI0
#    define HLLOG_USE_ABI0
#  endif
#else
#  define HLLOG_ABI_SUFFIX
#endif

#define HLLOG_INLINE_API_NAMESPACE_ v1_11_inline

#ifndef HLLOG_DISABLE_FMT_COMPILE
#  define HLLOG_INLINE_API_NAMESPACE HLLOG_COMBINE(HLLOG_COMBINE(HLLOG_INLINE_API_NAMESPACE_, _fmt_compile), HLLOG_ABI_SUFFIX)
#else
#  define HLLOG_INLINE_API_NAMESPACE HLLOG_COMBINE(HLLOG_INLINE_API_NAMESPACE_, HLLOG_ABI_SUFFIX)
#endif

#define HLLOG_BEGIN_NAMESPACE namespace hl_logger{ inline namespace HLLOG_INLINE_API_NAMESPACE{
#define HLLOG_END_NAMESPACE }}

#define HLLOG_FORCE_INLINE __attribute__((always_inline))
