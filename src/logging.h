/*
 * logging.h - append-only text log, mirroring the original program's log.txt.
 */
#ifndef LTM_LOGGING_H
#define LTM_LOGGING_H

#include "common.h"

void ltm_log_init(void);
void ltm_log_shutdown(void);

/* Thread safe. Writes "YYYY/MM/DD HH:MM:SS | <message>" and rotates the file
 * once it grows past a megabyte. */
void ltm_log(const WCHAR *fmt, ...);

#endif /* LTM_LOGGING_H */
