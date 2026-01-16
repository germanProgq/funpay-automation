/* FunPay Vertex core export configuration. */

#ifndef FPV_EXPORT_H
#define FPV_EXPORT_H

#if defined(FPV_CORE_BUILD_SHARED)
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(FPV_CORE_BUILD)
#define FPV_CORE_API __declspec(dllexport)
#else
#define FPV_CORE_API __declspec(dllimport)
#endif
#else
#define FPV_CORE_API __attribute__((visibility("default")))
#endif
#else
#define FPV_CORE_API
#endif

#endif
