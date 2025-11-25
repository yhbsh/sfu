#pragma once
#ifdef __ANDROID__
#include <android/log.h>
static inline void rtp_log(const char *fmt, ...) {
    static char buf[4096];
    static size_t pos = 0;

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf + pos, sizeof(buf) - pos, fmt, args);
    va_end(args);

    if (written < 0) return;

    pos += written;
    if (pos >= sizeof(buf) - 1) pos = sizeof(buf) - 1;

    if (memchr(buf, '\n', pos)) {
        __android_log_print(ANDROID_LOG_INFO, "ENGINE", "%.*s", (int)pos, buf);
        pos = 0;
    }
}
#else

static inline void rtp_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
#endif
