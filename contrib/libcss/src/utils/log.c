/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include "utils/libcss_log.h"

#ifdef LIBCSS_ENABLE_LOGGING

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static FILE *css_log_file;
static bool css_log_open_failed;
static bool css_log_close_registered;

static FILE *css__log_get_file(void)
{
        const char *path;

        if (css_log_file != NULL)
                return css_log_file;

        if (css_log_open_failed)
                return NULL;

        path = getenv("LIBCSS_LOG_FILE");
        if (path == NULL || path[0] == '\0')
                path = "libcss-log.txt";

        css_log_file = fopen(path, "a");
        if (css_log_file == NULL) {
                css_log_open_failed = true;
                return NULL;
        }

        if (!css_log_close_registered) {
                atexit(css__log_close);
                css_log_close_registered = true;
        }

        return css_log_file;
}

void css__log_close(void)
{
        if (css_log_file != NULL) {
                fclose(css_log_file);
                css_log_file = NULL;
        }
}

void css__log(const char *level,
        const char *file,
        const char *func,
        int line,
        const char *fmt,
        ...)
{
        FILE *out = css__log_get_file();
        va_list ap;

        if (out == NULL)
                return;

        fprintf(out, "[%s] %s:%d %s: ", level, file, line, func);

        va_start(ap, fmt);
        vfprintf(out, fmt, ap);
        va_end(ap);

        fputc('\n', out);
        fflush(out);
}

#endif
