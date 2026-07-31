/*
 * common.h - shared types, allocator wrappers and small string helpers.
 *
 * The whole project deliberately avoids the C++ runtime, the STL and any
 * third-party library. Everything talks straight to Win32 so the resulting
 * binary has no dependency beyond the OS itself.
 */
#ifndef LTM_COMMON_H
#define LTM_COMMON_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601 /* Windows 7 */
#endif

#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#define LTM_APP_NAME_A "LanTaskmgr"
#define LTM_APP_NAME_W L"LanTaskmgr"
#define LTM_VERSION_W  L"1.0.0"
#define LTM_VERSION_A  "1.0.0"

#define LTM_COUNTOF(a) (sizeof(a) / sizeof((a)[0]))
#define LTM_UNUSED(x)  ((void)(x))

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/*                                                                     */
/* All allocations go through the process heap directly. There is no    */
/* need for the CRT allocator's extra bookkeeping and this keeps the    */
/* private working set noticeably smaller.                              */
/* ------------------------------------------------------------------ */

void *ltm_alloc(size_t bytes);              /* zero initialised */
void *ltm_realloc(void *p, size_t bytes);
void  ltm_free(void *p);

/* ------------------------------------------------------------------ */
/* Growable byte buffer (used to build HTTP responses and JSON)         */
/* ------------------------------------------------------------------ */

typedef struct ltm_buf {
    char  *data;
    size_t len;
    size_t cap;
} ltm_buf;

void ltm_buf_init(ltm_buf *b);
void ltm_buf_free(ltm_buf *b);
void ltm_buf_reset(ltm_buf *b);
BOOL ltm_buf_reserve(ltm_buf *b, size_t extra);
BOOL ltm_buf_append(ltm_buf *b, const void *data, size_t len);
BOOL ltm_buf_puts(ltm_buf *b, const char *s);
BOOL ltm_buf_putc(ltm_buf *b, char c);
BOOL ltm_buf_printf(ltm_buf *b, const char *fmt, ...);
/* Appends `s` with JSON string escaping (no surrounding quotes). */
BOOL ltm_buf_put_json_escaped(ltm_buf *b, const char *s);
/* Same, but takes a UTF-16 string and converts to UTF-8 in a stack scratch
 * (no heap allocation for typical process names/titles). */
BOOL ltm_buf_put_json_escaped_w(ltm_buf *b, const WCHAR *ws);

/* ------------------------------------------------------------------ */
/* Text conversion                                                     */
/* ------------------------------------------------------------------ */

/* Returns a heap block the caller must ltm_free(). NULL on failure. */
char    *ltm_utf16_to_utf8(const WCHAR *ws, int wlen /* -1 = NUL terminated */);
WCHAR   *ltm_utf8_to_utf16(const char *s, int len /* -1 = NUL terminated */);

/* Bounded copies that always NUL terminate. */
void ltm_strlcpy_a(char *dst, size_t cap, const char *src);
void ltm_strlcpy_w(WCHAR *dst, size_t cap, const WCHAR *src);

int  ltm_stricmp_a(const char *a, const char *b);
BOOL ltm_str_startswith_a(const char *s, const char *prefix);
/* Timing-safe comparison, used for password / token checks. */
BOOL ltm_const_time_equal(const char *a, const char *b);

/* Random bytes from the system CSPRNG. */
BOOL ltm_random_bytes(void *out, size_t len);
void ltm_bytes_to_hex(const void *bytes, size_t len, char *out /* 2*len+1 */);

/* Directory the executable lives in, without a trailing backslash. */
const WCHAR *ltm_exe_dir(void);
/* Builds "<writable dir>\<name>". Prefers the exe directory and falls back to
 * %APPDATA%\LanTaskmgr when the exe sits somewhere read-only. */
BOOL ltm_data_path(const WCHAR *name, WCHAR *out, size_t cap);

#endif /* LTM_COMMON_H */
