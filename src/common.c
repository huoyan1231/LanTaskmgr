#include "common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RtlGenRandom. Declared by hand so we do not have to pull in <ntsecapi.h>
 * and link against a second crypto surface just for 16 random bytes. */
#define RtlGenRandom SystemFunction036
BOOLEAN WINAPI RtlGenRandom(PVOID RandomBuffer, ULONG RandomBufferLength);
#pragma comment(lib, "advapi32.lib")

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

void *ltm_alloc(size_t bytes)
{
    if (bytes == 0) {
        bytes = 1;
    }
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
}

void *ltm_realloc(void *p, size_t bytes)
{
    if (bytes == 0) {
        bytes = 1;
    }
    if (p == NULL) {
        return HeapAlloc(GetProcessHeap(), 0, bytes);
    }
    return HeapReAlloc(GetProcessHeap(), 0, p, bytes);
}

void ltm_free(void *p)
{
    if (p != NULL) {
        HeapFree(GetProcessHeap(), 0, p);
    }
}

/* ------------------------------------------------------------------ */
/* Growable buffer                                                     */
/* ------------------------------------------------------------------ */

void ltm_buf_init(ltm_buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void ltm_buf_free(ltm_buf *b)
{
    ltm_free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void ltm_buf_reset(ltm_buf *b)
{
    b->len = 0;
    if (b->data != NULL && b->cap > 0) {
        b->data[0] = '\0';
    }
}

BOOL ltm_buf_reserve(ltm_buf *b, size_t extra)
{
    size_t need = b->len + extra + 1; /* keep room for a NUL */
    size_t cap;
    char  *nd;

    if (need <= b->cap) {
        return TRUE;
    }
    cap = (b->cap != 0) ? b->cap : 256;
    while (cap < need) {
        cap *= 2;
    }
    nd = (char *)ltm_realloc(b->data, cap);
    if (nd == NULL) {
        return FALSE;
    }
    b->data = nd;
    b->cap = cap;
    return TRUE;
}

BOOL ltm_buf_append(ltm_buf *b, const void *data, size_t len)
{
    if (len == 0) {
        return TRUE;
    }
    if (!ltm_buf_reserve(b, len)) {
        return FALSE;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
    return TRUE;
}

BOOL ltm_buf_puts(ltm_buf *b, const char *s)
{
    return ltm_buf_append(b, s, strlen(s));
}

BOOL ltm_buf_putc(ltm_buf *b, char c)
{
    return ltm_buf_append(b, &c, 1);
}

BOOL ltm_buf_printf(ltm_buf *b, const char *fmt, ...)
{
    char    stackbuf[512];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = _vsnprintf_s(stackbuf, sizeof(stackbuf), _TRUNCATE, fmt, ap);
    va_end(ap);

    if (n >= 0) {
        return ltm_buf_append(b, stackbuf, (size_t)n);
    }

    /* Truncated: retry on the heap with a generous bound. */
    {
        size_t cap = sizeof(stackbuf) * 8;
        char  *big = (char *)ltm_alloc(cap);
        BOOL   ok = FALSE;
        if (big != NULL) {
            va_start(ap, fmt);
            n = _vsnprintf_s(big, cap, _TRUNCATE, fmt, ap);
            va_end(ap);
            if (n > 0) {
                ok = ltm_buf_append(b, big, (size_t)n);
            }
            ltm_free(big);
        }
        return ok;
    }
}

BOOL ltm_buf_put_json_escaped(ltm_buf *b, const char *s)
{
    const unsigned char *p;
    const unsigned char *run;

    if (s == NULL) {
        return TRUE;
    }
    /* Emit runs of plain bytes in a single append; only characters that need
     * escaping break the run. This collapses hundreds of 1-6 byte appends per
     * process name/title into a handful of bulk appends. */
    run = p = (const unsigned char *)s;
    while (*p != '\0') {
        unsigned char c = *p;
        BOOL          special;
        switch (c) {
        case '"':  case '\\': case '\n': case '\r':
        case '\t': case '\b': case '\f':
        case '<':  case '>':  case '&':
            special = TRUE; break;
        default:
            special = (c < 0x20);
            break;
        }
        if (special) {
            if (p > run) {
                if (!ltm_buf_append(b, run, (size_t)(p - run))) {
                    return FALSE;
                }
            }
            {
                const char *esc = NULL;
                size_t      n = 0;
                switch (c) {
                case '"':  esc = "\\\"",  n = 2; break;
                case '\\': esc = "\\\\",  n = 2; break;
                case '\n': esc = "\\n",   n = 2; break;
                case '\r': esc = "\\r",   n = 2; break;
                case '\t': esc = "\\t",   n = 2; break;
                case '\b': esc = "\\b",   n = 2; break;
                case '\f': esc = "\\f",   n = 2; break;
                case '<':  esc = "\\u003c", n = 6; break; /* defuse </script> */
                case '>':  esc = "\\u003e", n = 6; break;
                case '&':  esc = "\\u0026", n = 6; break;
                default: /* control char < 0x20 */
                {
                    char buf[8];
                    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "\\u%04x", (unsigned)c);
                    if (!ltm_buf_puts(b, buf)) {
                        return FALSE;
                    }
                }
                run = p + 1;
                ++p;
                continue;
                }
                if (!ltm_buf_append(b, esc, n)) {
                    return FALSE;
                }
            }
            run = p + 1;
        }
        ++p;
    }
    if (p > run) {
        if (!ltm_buf_append(b, run, (size_t)(p - run))) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Convert a UTF-16 string to UTF-8 in a stack scratch and JSON-escape it in
 * one pass. Avoids both the per-process heap allocation and the extra
 * WideCharToMultiByte that a separate utf16_to_utf8() call would incur. */
BOOL ltm_buf_put_json_escaped_w(ltm_buf *b, const WCHAR *ws)
{
    char  scratch[512];
    char *utf8;
    int   need;
    BOOL  own = FALSE;
    BOOL  ok;

    if (ws == NULL) {
        return TRUE;
    }
    need = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    if (need <= 1) {
        return TRUE; /* empty */
    }
    if ((size_t)need <= sizeof(scratch)) {
        utf8 = scratch;
    } else {
        utf8 = (char *)ltm_alloc((size_t)need);
        if (utf8 == NULL) {
            return FALSE;
        }
        own = TRUE;
    }
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, utf8, need, NULL, NULL);
    ok = ltm_buf_put_json_escaped(b, utf8);
    if (own) {
        ltm_free(utf8);
    }
    return ok;
}


/* ------------------------------------------------------------------ */
/* Text conversion                                                     */
/* ------------------------------------------------------------------ */

char *ltm_utf16_to_utf8(const WCHAR *ws, int wlen)
{
    int   need;
    char *out;

    if (ws == NULL) {
        return NULL;
    }
    need = WideCharToMultiByte(CP_UTF8, 0, ws, wlen, NULL, 0, NULL, NULL);
    if (need <= 0) {
        out = (char *)ltm_alloc(1);
        return out;
    }
    out = (char *)ltm_alloc((size_t)need + 1);
    if (out == NULL) {
        return NULL;
    }
    WideCharToMultiByte(CP_UTF8, 0, ws, wlen, out, need, NULL, NULL);
    out[need] = '\0';
    return out;
}

WCHAR *ltm_utf8_to_utf16(const char *s, int len)
{
    int    need;
    WCHAR *out;

    if (s == NULL) {
        return NULL;
    }
    need = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
    if (need <= 0) {
        out = (WCHAR *)ltm_alloc(sizeof(WCHAR));
        return out;
    }
    out = (WCHAR *)ltm_alloc(((size_t)need + 1) * sizeof(WCHAR));
    if (out == NULL) {
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, s, len, out, need);
    out[need] = L'\0';
    return out;
}

void ltm_strlcpy_a(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (cap == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void ltm_strlcpy_w(WCHAR *dst, size_t cap, const WCHAR *src)
{
    size_t n;
    if (cap == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = L'\0';
        return;
    }
    n = wcslen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n * sizeof(WCHAR));
    dst[n] = L'\0';
}

int ltm_stricmp_a(const char *a, const char *b)
{
    for (;;) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        if (ca == '\0') {
            return 0;
        }
    }
}

BOOL ltm_str_startswith_a(const char *s, const char *prefix)
{
    while (*prefix != '\0') {
        if (*s++ != *prefix++) {
            return FALSE;
        }
    }
    return TRUE;
}

BOOL ltm_const_time_equal(const char *a, const char *b)
{
    size_t        la, lb, i, n;
    unsigned char diff;

    if (a == NULL || b == NULL) {
        return FALSE;
    }
    la = strlen(a);
    lb = strlen(b);
    n = MAX(la, lb);
    diff = (unsigned char)((la ^ lb) != 0);
    for (i = 0; i < n; ++i) {
        unsigned char ca = (i < la) ? (unsigned char)a[i] : 0;
        unsigned char cb = (i < lb) ? (unsigned char)b[i] : 0;
        diff |= (unsigned char)(ca ^ cb);
    }
    return diff == 0;
}

BOOL ltm_random_bytes(void *out, size_t len)
{
    if (RtlGenRandom(out, (ULONG)len)) {
        return TRUE;
    }
    /* Extremely unlikely; fall back to something still unpredictable enough
     * for a LAN session id rather than returning zeroes. */
    {
        unsigned char *p = (unsigned char *)out;
        LARGE_INTEGER  qpc;
        size_t         i;
        QueryPerformanceCounter(&qpc);
        for (i = 0; i < len; ++i) {
            qpc.QuadPart = qpc.QuadPart * 6364136223846793005LL + 1442695040888963407LL;
            p[i] = (unsigned char)(qpc.QuadPart >> ((i % 8) * 8));
        }
    }
    return TRUE;
}

void ltm_bytes_to_hex(const void *bytes, size_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p = (const unsigned char *)bytes;
    size_t i;
    for (i = 0; i < len; ++i) {
        out[i * 2 + 0] = hex[p[i] >> 4];
        out[i * 2 + 1] = hex[p[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

/* ------------------------------------------------------------------ */
/* Paths                                                               */
/* ------------------------------------------------------------------ */

const WCHAR *ltm_exe_dir(void)
{
    static WCHAR dir[MAX_PATH];
    static BOOL  ready = FALSE;

    if (!ready) {
        DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) {
            dir[0] = L'.';
            dir[1] = L'\0';
        } else {
            WCHAR *slash = wcsrchr(dir, L'\\');
            if (slash != NULL) {
                *slash = L'\0';
            }
        }
        ready = TRUE;
    }
    return dir;
}

static BOOL dir_is_writable(const WCHAR *dir)
{
    WCHAR  probe[MAX_PATH];
    HANDLE h;

    if (_snwprintf_s(probe, MAX_PATH, _TRUNCATE, L"%s\\.ltm_write_test", dir) < 0) {
        return FALSE;
    }
    h = CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    CloseHandle(h);
    return TRUE;
}

BOOL ltm_data_path(const WCHAR *name, WCHAR *out, size_t cap)
{
    static WCHAR base[MAX_PATH];
    static BOOL  ready = FALSE;

    if (!ready) {
        const WCHAR *exedir = ltm_exe_dir();
        if (dir_is_writable(exedir)) {
            ltm_strlcpy_w(base, MAX_PATH, exedir);
        } else {
            WCHAR *appdata = _wgetenv(L"APPDATA");
            if (appdata != NULL && appdata[0] != L'\0') {
                _snwprintf_s(base, MAX_PATH, _TRUNCATE, L"%s\\%s", appdata, LTM_APP_NAME_W);
                CreateDirectoryW(base, NULL);
            } else {
                ltm_strlcpy_w(base, MAX_PATH, exedir);
            }
        }
        ready = TRUE;
    }
    return _snwprintf_s(out, cap, _TRUNCATE, L"%s\\%s", base, name) > 0;
}
