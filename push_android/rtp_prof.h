#pragma once
#include <time.h>

#ifdef __ANDROID__
    #include <android/log.h>
    #define PROFILE_PRINT(label, ms) \
        __android_log_print(ANDROID_LOG_DEBUG, "ENGINE", "[%s] %.3f ms", label, ms)
#else
    #include <stdio.h>
    #define PROFILE_PRINT(label, ms) \
        printf("[PROFILE] %s: %.3f ms\n", label, ms)
#endif

#define PROFILE_BEGIN(label) \
    clock_t __profile_start_##label = clock();

#define PROFILE_END(label) \
    do { \
        clock_t __profile_end_##label = clock(); \
        double __profile_ms_##label = \
            (double)(__profile_end_##label - __profile_start_##label) * 1000.0 / CLOCKS_PER_SEC; \
        PROFILE_PRINT(#label, __profile_ms_##label); \
    } while (0)

#define PROFILE_CALL(label, call) \
    ({ \
        clock_t __profile_start_##label = clock(); \
        __typeof__((call, 0)) __profile_ret_##label = ({ call; 0; }); \
        clock_t __profile_end_##label = clock(); \
        double __profile_ms_##label = \
            (double)(__profile_end_##label - __profile_start_##label) * 1000.0 / CLOCKS_PER_SEC; \
        PROFILE_PRINT(#label, __profile_ms_##label); \
        __profile_ret_##label; \
    })
