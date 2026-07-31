#include "config.h"
#include "logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ltm_config g_cfg;

static const WCHAR *const kLangCodes[LTM_LANG_COUNT] = { L"EN", L"CN", L"TW" };

const WCHAR *ltm_lang_code(ltm_lang_id id)
{
    if (id < 0 || id >= LTM_LANG_COUNT) {
        id = LTM_LANG_EN;
    }
    return kLangCodes[id];
}

ltm_lang_id ltm_lang_from_code(const WCHAR *s)
{
    int i;
    if (s == NULL) {
        return LTM_LANG_EN;
    }
    for (i = 0; i < LTM_LANG_COUNT; ++i) {
        if (_wcsicmp(s, kLangCodes[i]) == 0) {
            return (ltm_lang_id)i;
        }
    }
    return LTM_LANG_EN;
}

ltm_lang_id ltm_lang_detect_system(void)
{
    LANGID lid = GetUserDefaultUILanguage();
    WORD   primary = (WORD)(lid & 0x3ff);

    if (primary == 0x04 /* LANG_CHINESE */) {
        WORD sub = (WORD)(lid >> 10);
        /* SUBLANG_CHINESE_TRADITIONAL(1) / _HONGKONG(3) / _MACAU(5) */
        if (sub == 1 || sub == 3 || sub == 5) {
            return LTM_LANG_TW;
        }
        return LTM_LANG_CN;
    }
    return LTM_LANG_EN;
}

void ltm_config_defaults(void)
{
    unsigned char rnd[4];
    unsigned      v;

    ZeroMemory(&g_cfg, sizeof(g_cfg));
    g_cfg.port = LTM_DEFAULT_PORT;
    g_cfg.lang = ltm_lang_detect_system();
    g_cfg.autostart = FALSE;
    g_cfg.start_hidden = FALSE;
    g_cfg.auto_start_svc = TRUE; /* most users expect the service to start */

    /* A random 6 digit first-run password beats shipping "1234" like the
     * original did: the service listens on the LAN from the very first start. */
    ltm_random_bytes(rnd, sizeof(rnd));
    v = ((unsigned)rnd[0] << 24) | ((unsigned)rnd[1] << 16) |
        ((unsigned)rnd[2] << 8) | (unsigned)rnd[3];
    _snwprintf_s(g_cfg.password, LTM_PASSWORD_MAX, _TRUNCATE, L"%06u", v % 1000000u);
}

static void config_path(WCHAR *out, size_t cap)
{
    ltm_data_path(L"settings.ini", out, cap);
}

/* Trims ASCII whitespace in place and returns the new start pointer. */
static WCHAR *trim(WCHAR *s)
{
    WCHAR *end;
    while (*s == L' ' || *s == L'\t' || *s == L'\r' || *s == L'\n') {
        ++s;
    }
    end = s + wcslen(s);
    while (end > s) {
        WCHAR c = end[-1];
        if (c != L' ' && c != L'\t' && c != L'\r' && c != L'\n') {
            break;
        }
        --end;
    }
    *end = L'\0';
    return s;
}

BOOL ltm_config_load(void)
{
    WCHAR   path[MAX_PATH];
    HANDLE  h;
    DWORD   size, got = 0;
    char   *raw;
    WCHAR  *text;
    WCHAR  *cursor;
    BOOL    found_any = FALSE;

    ltm_config_defaults();
    config_path(path, MAX_PATH);

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size > (1u << 20)) {
        CloseHandle(h);
        return FALSE;
    }
    raw = (char *)ltm_alloc((size_t)size + 1);
    if (raw == NULL) {
        CloseHandle(h);
        return FALSE;
    }
    ReadFile(h, raw, size, &got, NULL);
    CloseHandle(h);
    raw[got] = '\0';

    /* Skip a UTF-8 BOM if present. */
    text = ltm_utf8_to_utf16((got >= 3 && (unsigned char)raw[0] == 0xEF &&
                              (unsigned char)raw[1] == 0xBB &&
                              (unsigned char)raw[2] == 0xBF) ? raw + 3 : raw, -1);
    ltm_free(raw);
    if (text == NULL) {
        return FALSE;
    }

    cursor = text;
    while (*cursor != L'\0') {
        WCHAR *line = cursor;
        WCHAR *nl = wcspbrk(cursor, L"\r\n");
        WCHAR *eq;
        WCHAR *key;
        WCHAR *val;

        if (nl != NULL) {
            *nl = L'\0';
            cursor = nl + 1;
            while (*cursor == L'\n' || *cursor == L'\r') {
                ++cursor;
            }
        } else {
            cursor = line + wcslen(line);
        }

        line = trim(line);
        if (line[0] == L'\0' || line[0] == L'#' || line[0] == L';' || line[0] == L'[') {
            continue;
        }
        eq = wcschr(line, L'=');
        if (eq == NULL) {
            continue;
        }
        *eq = L'\0';
        key = trim(line);
        val = trim(eq + 1);
        found_any = TRUE;

        if (_wcsicmp(key, L"Port") == 0) {
            int p = _wtoi(val);
            if (p >= 1 && p <= 65535) {
                g_cfg.port = p;
            }
        } else if (_wcsicmp(key, L"Password") == 0) {
            if (val[0] != L'\0') {
                ltm_strlcpy_w(g_cfg.password, LTM_PASSWORD_MAX, val);
            }
        } else if (_wcsicmp(key, L"Language") == 0) {
            g_cfg.lang = ltm_lang_from_code(val);
        } else if (_wcsicmp(key, L"AutoStart") == 0) {
            g_cfg.autostart = (_wtoi(val) != 0);
        } else if (_wcsicmp(key, L"StartHidden") == 0) {
            g_cfg.start_hidden = (_wtoi(val) != 0);
        } else if (_wcsicmp(key, L"AutoStartSvc") == 0) {
            g_cfg.auto_start_svc = (_wtoi(val) != 0);
        }
    }

    ltm_free(text);
    return found_any;
}

BOOL ltm_config_save(BOOL *ok_out)
{
    WCHAR  path[MAX_PATH];
    WCHAR  textw[1024];
    char  *utf8;
    HANDLE h;
    DWORD  written = 0;
    BOOL   ok;
    int    n;

    config_path(path, MAX_PATH);

    n = _snwprintf_s(textw, LTM_COUNTOF(textw), _TRUNCATE,
                     L"; LanTaskmgr settings\r\n"
                     L"; Anyone who can read this file can read your password.\r\n"
                     L"\r\n"
                     L"[LanTaskmgr]\r\n"
                     L"Port=%d\r\n"
                     L"Password=%s\r\n"
                     L"Language=%s\r\n"
                     L"AutoStart=%d\r\n"
                     L"StartHidden=%d\r\n"
                     L"AutoStartSvc=%d\r\n",
                     g_cfg.port, g_cfg.password, ltm_lang_code(g_cfg.lang),
                     g_cfg.autostart ? 1 : 0, g_cfg.start_hidden ? 1 : 0,
                     g_cfg.auto_start_svc ? 1 : 0);
    if (n <= 0) {
        return FALSE;
    }

    utf8 = ltm_utf16_to_utf8(textw, -1);
    if (utf8 == NULL) {
        return FALSE;
    }

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        ltm_free(utf8);
        ltm_log(L"failed to write settings.ini (error %lu)", GetLastError());
        return FALSE;
    }
    ok = WriteFile(h, utf8, (DWORD)strlen(utf8), &written, NULL);
    CloseHandle(h);
    ltm_free(utf8);
    if (ok_out) { *ok_out = ok; }
    return ok;
}
