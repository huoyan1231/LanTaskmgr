#include "logging.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LTM_LOG_MAX_BYTES (1024 * 1024)

static CRITICAL_SECTION g_log_lock;
static BOOL             g_log_ready = FALSE;
static WCHAR            g_log_path[MAX_PATH];

void ltm_log_init(void)
{
    if (g_log_ready) {
        return;
    }
    InitializeCriticalSection(&g_log_lock);
    ltm_data_path(L"lantaskmgr.log", g_log_path, MAX_PATH);
    g_log_ready = TRUE;
}

void ltm_log_shutdown(void)
{
    if (!g_log_ready) {
        return;
    }
    g_log_ready = FALSE;
    DeleteCriticalSection(&g_log_lock);
}

static void rotate_if_needed(void)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    WCHAR old[MAX_PATH];

    if (!GetFileAttributesExW(g_log_path, GetFileExInfoStandard, &fad)) {
        return;
    }
    if (fad.nFileSizeHigh == 0 && fad.nFileSizeLow < LTM_LOG_MAX_BYTES) {
        return;
    }
    if (_snwprintf_s(old, MAX_PATH, _TRUNCATE, L"%s.1", g_log_path) < 0) {
        return;
    }
    DeleteFileW(old);
    MoveFileW(g_log_path, old);
}

void ltm_log(const WCHAR *fmt, ...)
{
    WCHAR      body[1024];
    WCHAR      line[1200];
    SYSTEMTIME st;
    va_list    ap;
    HANDLE     h;
    char      *utf8;
    DWORD      written = 0;

    if (!g_log_ready) {
        return;
    }

    va_start(ap, fmt);
    _vsnwprintf_s(body, LTM_COUNTOF(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    GetLocalTime(&st);
    if (_snwprintf_s(line, LTM_COUNTOF(line), _TRUNCATE,
                     L"%04d/%02d/%02d %02d:%02d:%02d | %s\r\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                     st.wSecond, body) < 0) {
        return;
    }

    utf8 = ltm_utf16_to_utf8(line, -1);
    if (utf8 == NULL) {
        return;
    }

    EnterCriticalSection(&g_log_lock);
    rotate_if_needed();
    h = CreateFileW(g_log_path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SetFilePointer(h, 0, NULL, FILE_END);
        WriteFile(h, utf8, (DWORD)strlen(utf8), &written, NULL);
        CloseHandle(h);
    }
    LeaveCriticalSection(&g_log_lock);

    ltm_free(utf8);
}
