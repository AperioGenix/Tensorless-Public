#pragma once

#if defined(_WIN32)
#    if defined(TENSORLESS_BUILD_DLL)
#        define TENSORLESS_API __declspec(dllexport)
#    else
#        define TENSORLESS_API __declspec(dllimport)
#    endif
#    define TENSORLESS_CALL __cdecl
#else
#    define TENSORLESS_API __attribute__((visibility("default")))
#    define TENSORLESS_CALL
#endif

#if defined(__cplusplus)
#    define TENSORLESS_EXTERN_C extern "C"
#else
#    define TENSORLESS_EXTERN_C
#endif

#if defined(__cplusplus)
#    define TENSORLESS_ALIGNAS(value) alignas(value)
#elif defined(_MSC_VER)
#    define TENSORLESS_ALIGNAS(value) __declspec(align(value))
#else
#    define TENSORLESS_ALIGNAS(value) __attribute__((aligned(value)))
#endif
