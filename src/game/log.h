#pragma once

namespace sm64nv {

void log_open(const char *path);
void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

}
