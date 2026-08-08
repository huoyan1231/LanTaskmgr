#include "config.h"
#include "logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bcrypt.h>

ltm_config g_cfg;

static const WCHAR *const kLangCodes[LTM_LANG_COUNT] = { L"EN", L"CN", L"TW" };

/* ------------------------------------------------------------------ */
/* Password protection at rest                                          */
/* ------------------------------------------------------------------ */

/* The password is encrypted with AES-256-CBC so the on-disk value is not
 * cleartext. The key is derived from a compile-time salt (NOT from the
 * password itself, and NOT from any Windows user/account): the encrypted blob
 * is the password, so the key must be obtainable at startup without knowing
 * the password. This keeps the scheme free of any Windows-user or machine
 * binding -- it is portable and decrypts on any machine running this binary.
 *
 * On-disk format (stored base64):  IV[16] || ciphertext (PKCS7-padded). */

static const char kPwSalt[] = "LanTaskmgr-pw-salt-v1";

/* Derives the 32-byte AES-256 key from the fixed salt via SHA-256. */
static void pw_derive_key(BYTE key[32])
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) {
        return;
    }
    if (BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0) {
        BCryptHashData(h, (PUCHAR)kPwSalt, (ULONG)(sizeof(kPwSalt) - 1), 0);
        BCryptFinishHash(h, key, 32, 0);
        BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
}

/* Encrypts `pw` (UTF-16) into a base64 string written to `out`.
 * Returns TRUE on success. */
static BOOL pw_encrypt_to_b64(const WCHAR *pw, char *out, size_t out_cap)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE keyh = NULL;
    BYTE              aes_key[32];
    BYTE              iv[16];
    BYTE              plain[256];
    BYTE              buf[512];          /* IV || ciphertext */
    ULONG             plain_len, ct_len, got;
    BOOL              ok = FALSE;

    if (pw == NULL) {
        pw = L"";
    }
    plain_len = (ULONG)(wcslen(pw) * sizeof(WCHAR));
    if (plain_len == 0 || plain_len + 2 > sizeof(plain)) {
        return FALSE; /* empty handled by caller; too long rejected */
    }
    memcpy(plain, pw, plain_len);
    /* PKCS7 padding to a 16-byte boundary. */
    {
        ULONG pad = 16 - (plain_len % 16);
        ULONG i;
        for (i = 0; i < pad; i++) {
            plain[plain_len + i] = (BYTE)pad;
        }
        plain_len += pad;
    }

    pw_derive_key(aes_key);
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0) != 0) {
        return FALSE;
    }
    if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                          (LPWSTR)BCRYPT_CHAIN_MODE_CBC,
                          (ULONG)sizeof(BCRYPT_CHAIN_MODE_CBC), 0) != 0) {
        goto done;
    }
    if (BCryptGenerateSymmetricKey(alg, &keyh, NULL, 0, aes_key, 32, 0) != 0) {
        goto done;
    }
    ltm_random_bytes(iv, sizeof(iv));

    ct_len = plain_len; /* AES block cipher: ciphertext length == plaintext */
    if (BCryptEncrypt(keyh, plain, plain_len, NULL, iv, sizeof(iv),
                      buf + sizeof(iv), ct_len, &got, 0) == 0) {
        memcpy(buf, iv, sizeof(iv));
        ok = ltm_base64_encode(buf, sizeof(iv) + got, out, out_cap);
    }

done:
    if (keyh) BCryptDestroyKey(keyh);
    if (alg)  BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

/* Reverses pw_encrypt_to_b64: base64-decodes then AES-CBC-decrypts into `out`
 * (WCHAR buffer of `out_chars` elements). Returns the character count (without
 * NUL) or -1 on failure. */
static int pw_decrypt_from_b64(const char *b64, WCHAR *out, size_t out_chars)
{
    BYTE              buf[512];
    int               n = ltm_base64_decode(b64, buf, sizeof(buf));
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE keyh = NULL;
    BYTE              plain[256];
    BYTE              aes_key[32];
    ULONG             got;
    int               result = -1;

    if (n < 16 || (n - 16) % 16 != 0) {
        return -1;
    }
    pw_derive_key(aes_key);
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0) != 0) {
        return -1;
    }
    if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                          (LPWSTR)BCRYPT_CHAIN_MODE_CBC,
                          (ULONG)sizeof(BCRYPT_CHAIN_MODE_CBC), 0) != 0) {
        goto done;
    }
    if (BCryptGenerateSymmetricKey(alg, &keyh, NULL, 0, aes_key, 32, 0) != 0) {
        goto done;
    }
    if (BCryptDecrypt(keyh, buf + 16, (ULONG)(n - 16), NULL, buf, 16,
                      plain, sizeof(plain), &got, 0) == 0) {
        ULONG pad = (got > 0) ? plain[got - 1] : 0;
        ULONG chars;
        if (pad <= 16 && got >= pad) {
            got -= pad;
        }
        chars = got / sizeof(WCHAR);
        if (chars >= out_chars) {
            chars = (ULONG)out_chars - 1;
        }
        memcpy(out, plain, chars * sizeof(WCHAR));
        out[chars] = L'\0';
        result = (int)chars;
    }

done:
    if (keyh) BCryptDestroyKey(keyh);
    if (alg)  BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

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
    ZeroMemory(&g_cfg, sizeof(g_cfg));
    g_cfg.port = LTM_DEFAULT_PORT;
    g_cfg.lang = ltm_lang_detect_system();
    g_cfg.autostart = FALSE;
    g_cfg.start_hidden = FALSE;
    g_cfg.auto_start_svc = TRUE; /* most users expect the service to start */

    /* Default password is empty: the server runs without a password on first
     * launch. Set your own in the dialog if you want LAN access protected.
     * g_cfg.password is already zeroed by the ZeroMemory above. */
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
                /* New format: DPAPI-encrypted, stored as base64. Fall back to
                 * the legacy cleartext format if decryption fails (e.g. an
                 * old settings.ini edited by hand). */
                if (pw_decrypt_from_b64(val, g_cfg.password, LTM_PASSWORD_MAX) < 0) {
                    ltm_strlcpy_w(g_cfg.password, LTM_PASSWORD_MAX, val);
                }
            }
        } else if (_wcsicmp(key, L"Language") == 0) {
            g_cfg.lang = ltm_lang_from_code(val);
        } else if (_wcsicmp(key, L"BindIP") == 0) {
            if (val[0] != L'\0') {
                ltm_strlcpy_w(g_cfg.bind_ip, LTM_BINDIP_MAX, val);
            }
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
    char   pw_b64[1024];

    config_path(path, MAX_PATH);

    /* Encrypt the password with DPAPI so the on-disk value is not cleartext.
     * An empty password stays empty (no encryption needed). */
    if (g_cfg.password[0] != L'\0') {
        if (!pw_encrypt_to_b64(g_cfg.password, pw_b64, sizeof(pw_b64))) {
            pw_b64[0] = '\0';
        }
    } else {
        pw_b64[0] = '\0';
    }

    n = _snwprintf_s(textw, LTM_COUNTOF(textw), _TRUNCATE,
                     L"; LanTaskmgr settings\r\n"
                     L"; The password is encrypted with DPAPI and only the\r\n"
                     L"; current Windows user can decrypt it.\r\n"
                     L"\r\n"
                     L"[LanTaskmgr]\r\n"
                     L"Port=%d\r\n"
                     L"Password=%s\r\n"
                     L"Language=%s\r\n"
                     L"BindIP=%s\r\n"
                     L"AutoStart=%d\r\n"
                     L"StartHidden=%d\r\n"
                     L"AutoStartSvc=%d\r\n",
                     g_cfg.port, pw_b64, ltm_lang_code(g_cfg.lang),
                     g_cfg.bind_ip,
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
