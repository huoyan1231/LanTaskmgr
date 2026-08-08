#include "api.h"
#include "assets.h"
#include "config.h"
#include "http.h"
#include "logging.h"
#include "procs.h"
#include "resource.h"

#include <stdio.h>
#include <string.h>
#include <bcrypt.h>

#define LTM_SESSION_HEX_LEN   32                 /* 16 random bytes */
#define LTM_SESSION_TTL_MS    (12 * 60 * 60 * 1000ULL)
#define LTM_MAX_SESSIONS      8
#define LTM_COOKIE_NAME       "ltm"
#define LTM_UA_HASH_LEN       28                 /* base64 of 16 bytes + NUL */

typedef struct session {
    char          token[LTM_SESSION_HEX_LEN + 1];
    unsigned long ip;
    ULONGLONG     expires;
    BOOL          active;
    char          ua_hash[LTM_UA_HASH_LEN]; /* binds session to the User-Agent */
} session;

static CRITICAL_SECTION g_lock;
static BOOL             g_ready;
static session          g_sessions[LTM_MAX_SESSIONS];

static void ensure_init(void)
{
    if (!g_ready) {
        InitializeCriticalSection(&g_lock);
        g_ready = TRUE;
    }
}

void ltm_api_reset(void)
{
    ensure_init();
    EnterCriticalSection(&g_lock);
    ZeroMemory(g_sessions, sizeof(g_sessions));
    LeaveCriticalSection(&g_lock);
}

int ltm_api_active_sessions(void)
{
    int i, n = 0;
    ULONGLONG now;

    ensure_init();
    now = GetTickCount64();
    EnterCriticalSection(&g_lock);
    for (i = 0; i < LTM_MAX_SESSIONS; ++i) {
        if (g_sessions[i].active && g_sessions[i].expires > now) {
            ++n;
        }
    }
    LeaveCriticalSection(&g_lock);
    return n;
}

/* ------------------------------------------------------------------ */
/* Sessions                                                           */
/* ------------------------------------------------------------------ */

static const char *cookie_token(const ltm_http_request *req, char *out, size_t cap)
{
    const char *cookie = ltm_http_header_get(req, "Cookie");
    const char *p;

    out[0] = '\0';
    if (cookie == NULL) {
        return NULL;
    }
    for (p = cookie; *p != '\0'; ++p) {
        if ((p == cookie || p[-1] == ' ' || p[-1] == ';') &&
            ltm_str_startswith_a(p, LTM_COOKIE_NAME "=")) {
            const char *v = p + sizeof(LTM_COOKIE_NAME); /* name + '=' */
            size_t      n = 0;
            while (v[n] != '\0' && v[n] != ';' && n < cap - 1) {
                ++n;
            }
            memcpy(out, v, n);
            out[n] = '\0';
            return out;
        }
    }
    return NULL;
}

/* SHA-256 of the User-Agent, truncated to the first 16 bytes and base64-encoded.
 * Used to bind a session to the client software, lowering the risk that two
 * devices behind the same NAT (hence the same client_ip) hijack each other's
 * session cookie. */
static void ua_hash_of(const char *ua, char *out, size_t out_cap)
{
    BYTE        digest[32];
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;

    out[0] = '\0';
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) {
        return;
    }
    if (BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0) {
        const char *s = (ua != NULL) ? ua : "";
        if (BCryptHashData(h, (PUCHAR)s, (ULONG)strlen(s), 0) == 0 &&
            BCryptFinishHash(h, digest, sizeof(digest), 0) == 0) {
            ltm_base64_encode(digest, 16, out, out_cap);
        }
        BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
}

/* Extracts the host portion (up to ':' or '/' or end) of a string. */
static void host_of(const char *s, char *out, size_t cap)
{
    size_t i = 0, o = 0;
    int    started = 0;

    if (s == NULL) {
        out[0] = '\0';
        return;
    }
    /* Skip a scheme like "http://". */
    if (strncmp(s, "http://", 7) == 0) {
        i = 7;
    } else if (strncmp(s, "https://", 8) == 0) {
        i = 8;
    }
    for (; s[i] != '\0'; ++i) {
        char c = s[i];
        if (c == '/' || c == '?' || (started && c == ':')) {
            break;
        }
        if (o + 1 < cap) {
            out[o++] = c;
            started = 1;
        }
    }
    out[o] = '\0';
}

/* Allows a request unless it carries an Origin that points at a different host
 * than the one in the Host header (classic CSRF / cross-site check). A missing
 * Origin (same-origin simple requests, old browsers) is permitted. */
static BOOL origin_allowed(const ltm_http_request *req)
{
    char o_host[256];
    char h_host[256];

    if (req->origin[0] == '\0') {
        return TRUE;
    }
    host_of(req->origin, o_host, sizeof(o_host));
    host_of(req->host,   h_host, sizeof(h_host));
    if (o_host[0] == '\0' || h_host[0] == '\0') {
        /* Could not parse; fail open only when no Origin was supplied (handled
         * above). With an unparseable Origin we refuse to be safe. */
        return (req->host[0] == '\0');
    }
    /* Case-insensitive host comparison. */
    return (ltm_stricmp_a(o_host, h_host) == 0);
}

static BOOL session_validate(const ltm_http_request *req)
{
    char       token[128];
    ULONGLONG  now;
    int        i;
    BOOL       ok = FALSE;

    if (cookie_token(req, token, sizeof(token)) == NULL || token[0] == '\0') {
        return FALSE;
    }
    now = GetTickCount64();

    EnterCriticalSection(&g_lock);
    for (i = 0; i < LTM_MAX_SESSIONS; ++i) {
        session *s = &g_sessions[i];
        if (!s->active) {
            continue;
        }
        if (s->expires <= now) {
            s->active = FALSE;
            continue;
        }
        if (s->ip == req->client_ip &&
            ltm_const_time_equal(s->token, token) &&
            ltm_const_time_equal(s->ua_hash, req->ua_hash)) {
            s->expires = now + LTM_SESSION_TTL_MS; /* sliding window */
            ok = TRUE;
            break;
        }
    }
    LeaveCriticalSection(&g_lock);
    return ok;
}

static BOOL session_create(unsigned long ip, const char *ua_hash,
                            char *token_out /* >= 33 */)
{
    unsigned char raw[16];
    ULONGLONG     now = GetTickCount64();
    int           i, slot = -1;
    ULONGLONG     oldest = ~0ULL;

    ltm_random_bytes(raw, sizeof(raw));
    ltm_bytes_to_hex(raw, sizeof(raw), token_out);

    EnterCriticalSection(&g_lock);
    for (i = 0; i < LTM_MAX_SESSIONS; ++i) {
        if (!g_sessions[i].active || g_sessions[i].expires <= now) {
            slot = i;
            break;
        }
        if (g_sessions[i].expires < oldest) {
            oldest = g_sessions[i].expires;
            slot = i;
        }
    }
    if (slot >= 0) {
        ltm_strlcpy_a(g_sessions[slot].token, sizeof(g_sessions[slot].token), token_out);
        g_sessions[slot].ip = ip;
        ltm_strlcpy_a(g_sessions[slot].ua_hash, sizeof(g_sessions[slot].ua_hash),
                      ua_hash);
        g_sessions[slot].expires = now + LTM_SESSION_TTL_MS;
        g_sessions[slot].active = TRUE;
    }
    LeaveCriticalSection(&g_lock);
    return slot >= 0;
}

static void session_drop(const ltm_http_request *req)
{
    char token[128];
    int  i;

    if (cookie_token(req, token, sizeof(token)) == NULL) {
        return;
    }
    EnterCriticalSection(&g_lock);
    for (i = 0; i < LTM_MAX_SESSIONS; ++i) {
        if (g_sessions[i].active && ltm_const_time_equal(g_sessions[i].token, token)) {
            ZeroMemory(&g_sessions[i], sizeof(g_sessions[i]));
        }
    }
    LeaveCriticalSection(&g_lock);
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void ip_to_string(unsigned long ip, char *out, size_t cap)
{
    _snprintf_s(out, cap, _TRUNCATE, "%lu.%lu.%lu.%lu",
                (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
}

static void serve_asset(ltm_http_response *res, int id, const char *ctype,
                        const char *cache)
{
    size_t      len = 0;
    const void *data = ltm_asset_get(id, &len);

    if (data == NULL) {
        ltm_http_set_text(res, 500, "text/plain", "missing embedded resource");
        return;
    }
    ltm_http_set_static(res, data, len, ctype);
    if (cache != NULL) {
        ltm_http_add_header(res, cache);
    }
}

/* Tells the page which language table to use, so the JS ships all three and
 * the server never has to patch the asset before sending it. */
static void add_lang_cookie(ltm_http_response *res)
{
    char line[96];
    char code[8];
    ltm_strlcpy_a(code, sizeof(code),
                  (g_cfg.lang == LTM_LANG_CN) ? "CN" :
                  (g_cfg.lang == LTM_LANG_TW) ? "TW" : "EN");
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "Set-Cookie: ltm_lang=%s; Path=/; Max-Age=31536000; SameSite=Strict", code);
    ltm_http_add_header(res, line);
}

/* ------------------------------------------------------------------ */
/* Endpoints                                                          */
/* ------------------------------------------------------------------ */

static void handle_login(const ltm_http_request *req, ltm_http_response *res)
{
    char   ipstr[24];
    WCHAR *supplied;
    BOOL   ok;

    ip_to_string(req->client_ip, ipstr, sizeof(ipstr));

    /* An empty body is a valid (empty) password attempt; only reject an
     * over-long body. */
    if (req->body_len > 256) {
        ltm_http_set_text(res, 400, "text/plain", "bad");
        return;
    }

    supplied = ltm_utf8_to_utf16(req->body, (int)req->body_len);
    if (supplied == NULL) {
        ltm_http_set_text(res, 500, "text/plain", "bad");
        return;
    }

    /* g_cfg is only mutated while the service is stopped (see ui.c), so a
     * plain read here cannot race with a settings change. */
    {
        /* Constant-time comparison on the UTF-8 forms. */
        char *a = ltm_utf16_to_utf8(supplied, -1);
        char *b = ltm_utf16_to_utf8(g_cfg.password, -1);
        if (a != NULL && b != NULL) {
            ok = ltm_const_time_equal(a, b);
        } else {
            ok = FALSE;
        }
        ltm_free(a);
        ltm_free(b);
    }
    SecureZeroMemory(supplied, wcslen(supplied) * sizeof(WCHAR));
    ltm_free(supplied);

    if (!ok) {
        ltm_log(L"failed login from %S", ipstr);
        ltm_http_set_text(res, 401, "text/plain", "bad");
        return;
    }

    {
        char token[LTM_SESSION_HEX_LEN + 1];
        char cookie[160];

        if (!session_create(req->client_ip, req->ua_hash, token)) {
            ltm_http_set_text(res, 500, "text/plain", "bad");
            return;
        }
        _snprintf_s(cookie, sizeof(cookie), _TRUNCATE,
                    "Set-Cookie: %s=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=43200",
                    LTM_COOKIE_NAME, token);
        ltm_http_add_header(res, cookie);
        ltm_http_set_text(res, 200, "text/plain", "ok");

        {
            const char *ua = ltm_http_header_get(req, "User-Agent");
            ltm_log(L"new device: %S (%S)", ipstr, (ua != NULL) ? ua : "unknown");
        }
    }
}

static void handle_list(ltm_http_response *res)
{
    ltm_proc_snapshot snap;
    MEMORYSTATUSEX    mem;
    int               i;

    res->status = 200;
    res->content_type = "application/json; charset=utf-8";
    res->is_static = FALSE;

    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) {
        ZeroMemory(&mem, sizeof(mem));
        mem.dwLength = sizeof(mem);
    }

    ltm_buf_printf(&res->body,
                   "{\"mem\":{\"pct\":%lu,\"used\":%llu,\"total\":%llu},\"list\":[",
                   mem.dwMemoryLoad,
                   (unsigned long long)(mem.ullTotalPhys - mem.ullAvailPhys),
                   (unsigned long long)mem.ullTotalPhys);

    if (!ltm_proc_snapshot_take(&snap)) {
        ltm_buf_puts(&res->body, "]}");
        return;
    }

    /* Pre-size the body buffer so the item loop does not reallocate. */
    ltm_buf_reserve(&res->body, (size_t)snap.count * 64 + 256);

    for (i = 0; i < snap.count; ++i) {
        const ltm_proc_group *g = &snap.items[i];

        if (i > 0) {
            ltm_buf_putc(&res->body, ',');
        }
        ltm_buf_puts(&res->body, "{\"n\":\"");
        ltm_buf_put_json_escaped_w(&res->body, g->name);
        ltm_buf_puts(&res->body, "\"");
        if (g->title[0] != L'\0') {
            ltm_buf_puts(&res->body, ",\"t\":\"");
            ltm_buf_put_json_escaped_w(&res->body, g->title);
            ltm_buf_puts(&res->body, "\"");
        }
        ltm_buf_printf(&res->body, ",\"c\":%d,\"i\":%lu,\"m\":%llu,\"p\":%.1f",
                       (int)g->klass, (unsigned long)g->instances,
                       (unsigned long long)g->mem_bytes, (double)g->cpu_pct);
        if (g->is_protected) {
            ltm_buf_puts(&res->body, ",\"k\":1");
        }
        {
            int j;
            ltm_buf_puts(&res->body, ",\"pins\":[");
            for (j = 0; j < g->pid_count; ++j) {
                if (j > 0) {
                    ltm_buf_putc(&res->body, ',');
                }
                ltm_buf_printf(&res->body, "{\"p\":%lu", (unsigned long)g->pids[j]);
                if (g->ptitles[j][0] != L'\0') {
                    ltm_buf_puts(&res->body, ",\"t\":\"");
                    ltm_buf_put_json_escaped_w(&res->body, g->ptitles[j]);
                    ltm_buf_puts(&res->body, "\"");
                }
                if (g->piswin[j]) {
                    ltm_buf_puts(&res->body, ",\"w\":1");
                }
                ltm_buf_putc(&res->body, '}');
            }
            ltm_buf_puts(&res->body, "]");
        }
        ltm_buf_putc(&res->body, '}');
    }

    ltm_buf_puts(&res->body, "]}");
    ltm_proc_snapshot_free(&snap);
}

static void handle_kill(const ltm_http_request *req, ltm_http_response *res)
{
    char  ipstr[24];
    char  buf[4096];
    int   killed = 0, denied = 0, protected_n = 0, notfound = 0;
    char *cursor;

    /* The body is a list of PIDs (comma/space separated). A group can contain
     * hundreds of instances, so allow up to the HTTP body cap. */
    if (req->body_len == 0 || req->body_len > (int)sizeof(buf) - 1) {
        ltm_http_set_text(res, 400, "text/plain", "fail");
        return;
    }
    memcpy(buf, req->body, req->body_len);
    buf[req->body_len] = '\0';

    ip_to_string(req->client_ip, ipstr, sizeof(ipstr));
    ltm_log(L"%S requested kill of pids '%s'", ipstr, buf);

    /* The body is a comma- or space-separated list of decimal PIDs. Each is
     * terminated individually so one bad/already-gone pid does not abort the
     * rest. */
    cursor = buf;
    for (;;) {
        char *sep = cursor;
        DWORD pid;
        ltm_kill_result r;

        while (*sep != '\0' && *sep != ',' && *sep != ' ' && *sep != '\t') {
            ++sep;
        }
        if (sep != cursor) {
            char  saved = *sep;
            *sep = '\0';
            pid = (DWORD)strtoul(cursor, NULL, 10);
            *sep = saved;

            r = ltm_proc_kill_by_pid(pid);
            switch (r) {
            case LTM_KILL_OK:        ++killed; break;
            case LTM_KILL_PROTECTED: ++protected_n; break;
            case LTM_KILL_NOT_FOUND: ++notfound; break;
            default:                 ++denied; break;
            }
        }
        if (*sep == '\0') {
            break;
        }
        cursor = sep + 1;
    }

    if (killed > 0 && denied == 0 && protected_n == 0) {
        ltm_http_set_text(res, 200, "text/plain", "ok");
    } else if (killed > 0) {
        ltm_http_set_text(res, 200, "text/plain", "partial");
    } else if (protected_n > 0) {
        ltm_http_set_text(res, 403, "text/plain", "protected");
    } else if (notfound > 0) {
        ltm_http_set_text(res, 404, "text/plain", "gone");
    } else {
        ltm_http_set_text(res, 403, "text/plain", "denied");
    }
}

/* ------------------------------------------------------------------ */
/* Router                                                             */
/* ------------------------------------------------------------------ */

void ltm_api_handle(const ltm_http_request *req, ltm_http_response *res)
{
    BOOL is_get = (strcmp(req->method, "GET") == 0);
    BOOL is_post = (strcmp(req->method, "POST") == 0);
    BOOL authed;
    char ua_hash[LTM_UA_HASH_LEN];

    ensure_init();

    /* Bind every request to its User-Agent fingerprint so sessions created in
     * handle_login can be validated against it later. */
    ua_hash_of(ltm_http_header_get(req, "User-Agent"), ua_hash, sizeof(ua_hash));
    ltm_strlcpy_a(req->ua_hash, sizeof(req->ua_hash), ua_hash);

    if (!is_get && !is_post) {
        ltm_http_set_text(res, 405, "text/plain", "method not allowed");
        return;
    }

    authed = session_validate(req);

    /* --- static assets ------------------------------------------- */
    if (is_get) {
        if (strcmp(req->path, "/") == 0) {
            serve_asset(res, authed ? IDR_INDEX_HTML : IDR_LOGIN_HTML,
                        "text/html; charset=utf-8", "Cache-Control: no-store");
            add_lang_cookie(res);
            return;
        }
        if (strcmp(req->path, "/app.js") == 0) {
            serve_asset(res, IDR_APP_JS, "application/javascript; charset=utf-8",
                        "Cache-Control: no-cache");
            return;
        }
        if (strcmp(req->path, "/app.css") == 0) {
            serve_asset(res, IDR_APP_CSS, "text/css; charset=utf-8",
                        "Cache-Control: no-cache");
            return;
        }
        if (strcmp(req->path, "/favicon.ico") == 0) {
            res->status = 204;
            res->content_type = "image/x-icon";
            return;
        }
        ltm_http_set_text(res, 404, "text/plain", "not found");
        return;
    }

    /* --- API ------------------------------------------------------ */
    if (strcmp(req->path, "/dologin") == 0) {
        if (!origin_allowed(req)) {
            ltm_http_set_text(res, 403, "text/plain", "forbidden");
            return;
        }
        handle_login(req, res);
        return;
    }
    if (strcmp(req->path, "/logout") == 0) {
        if (!origin_allowed(req)) {
            ltm_http_set_text(res, 403, "text/plain", "forbidden");
            return;
        }
        session_drop(req);
        ltm_http_add_header(res, "Set-Cookie: " LTM_COOKIE_NAME "=; Path=/; Max-Age=0");
        ltm_http_set_text(res, 200, "text/plain", "ok");
        return;
    }

    if (!authed) {
        ltm_http_set_text(res, 401, "text/plain", "auth");
        return;
    }

    if (strcmp(req->path, "/list") == 0) {
        handle_list(res);
        return;
    }
    if (strcmp(req->path, "/kill") == 0) {
        if (!origin_allowed(req)) {
            ltm_http_set_text(res, 403, "text/plain", "forbidden");
            return;
        }
        handle_kill(req, res);
        return;
    }

    ltm_http_set_text(res, 404, "text/plain", "not found");
}
