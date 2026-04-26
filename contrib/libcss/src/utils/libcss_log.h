/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef css_utils_libcss_log_h_
#define css_utils_libcss_log_h_

#ifdef LIBCSS_ENABLE_LOGGING

void css__log(const char *level,
        const char *file,
        const char *func,
        int line,
        const char *fmt,
        ...)
#ifdef __GNUC__
        __attribute__((format(printf, 5, 6)))
#endif
        ;

void css__log_close(void);

#define CSS_LOG_ENABLED 1

#define CSS_LOG_FUNC __func__

#define CSS_LOG(level, fmt, ...) \
        css__log(#level, __FILE__, CSS_LOG_FUNC, __LINE__, fmt, ##__VA_ARGS__)

#else

#define CSS_LOG_ENABLED 0

#define CSS_LOG(level, fmt, ...) \
        do { \
        } while (0)

#endif

#endif
