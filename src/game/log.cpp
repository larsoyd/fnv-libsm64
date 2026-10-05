#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <windows.h>

namespace sm64nv {

static FILE *g_log;
static DWORD g_t0;

void log_open(const char *path) {
    g_log = fopen(path, "w");
    g_t0 = GetTickCount();
}

void logf(const char *fmt, ...) {
    if (!g_log) return;
    fprintf(g_log, "%8lu [sm64nv] ", (unsigned long)(GetTickCount() - g_t0));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

}
