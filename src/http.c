#include "http.h"
#include "logging.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Response helpers                                                    */
/* ------------------------------------------------------------------ */

void ltm_http_response_init(ltm_http_response *res)
{
    ZeroMemory(res, sizeof(*res));
    res->status = 200;
    res->content_type = "text/plain; charset=utf-8";
    ltm_buf_init(&res->headers);
    ltm_buf_init(&res->body);
}

void ltm_http_response_free(ltm_http_response *res)
{
    ltm_buf_free(&res->headers);
    ltm_buf_free(&res->body);
}

void ltm_http_add_header(ltm_http_response *res, const char *line)
{
    ltm_buf_puts(&res->headers, line);
    ltm_buf_puts(&res->headers, "\r\n");
}

void ltm_http_set_static(ltm_http_response *res, const void *data, size_t len,
                         const char *content_type)
{
    res->is_static = TRUE;
    res->body_static = data;
    res->body_static_len = len;
    res->content_type = content_type;
}

void ltm_http_set_text(ltm_http_response *res, int status, const char *content_type,
                       const char *text)
{
    res->status = status;
    res->content_type = content_type;
    res->is_static = FALSE;
    ltm_buf_reset(&res->body);
    ltm_buf_puts(&res->body, text);
}

const char *ltm_http_header_get(const ltm_http_request *r, const char *name)
{
    int i;
    for (i = 0; i < r->header_count; ++i) {
        if (ltm_stricmp_a(r->headers[i].name, name) == 0) {
            return r->headers[i].value;
        }
    }
    return NULL;
}

size_t ltm_url_decode(char *s)
{
    char *w = s;
    char *p = s;

    while (*p != '\0') {
        if (*p == '%' && p[1] != '\0' && p[2] != '\0') {
            int hi = -1, lo = -1;
            char c1 = p[1], c2 = p[2];
            if (c1 >= '0' && c1 <= '9') hi = c1 - '0';
            else if (c1 >= 'a' && c1 <= 'f') hi = c1 - 'a' + 10;
            else if (c1 >= 'A' && c1 <= 'F') hi = c1 - 'A' + 10;
            if (c2 >= '0' && c2 <= '9') lo = c2 - '0';
            else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10;
            else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;
            if (hi >= 0 && lo >= 0) {
                *w++ = (char)((hi << 4) | lo);
                p += 3;
                continue;
            }
        }
        *w++ = *p++;
    }
    *w = '\0';
    return (size_t)(w - s);
}

static const char *status_text(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    default:  return "OK";
    }
}

/* ------------------------------------------------------------------ */
/* Connections                                                         */
/* ------------------------------------------------------------------ */

typedef enum conn_state {
    CONN_READING = 0,
    CONN_WRITING,
    CONN_DONE
} conn_state;

typedef struct conn {
    SOCKET        s;
    unsigned long ip;
    conn_state    state;

    char  *in;
    size_t in_len;
    size_t in_cap;

    ltm_buf     head;        /* status line + headers */
    ltm_buf     body_owned;  /* dynamic body storage  */
    const char *body;        /* points into body_owned or the PE image */
    size_t      body_len;
    size_t      sent;        /* across head + body */

    DWORD last_active;
} conn;

static SOCKET           g_listener = INVALID_SOCKET;
static SOCKET           g_wakeup = INVALID_SOCKET;
static struct sockaddr_in g_wakeup_addr;
static HANDLE           g_thread;
static volatile LONG    g_stop;
static volatile LONG    g_running;
static WCHAR            g_last_error[256];
static conn             g_conns[LTM_HTTP_MAX_CONNS];

const WCHAR *ltm_http_last_error(void)
{
    return g_last_error;
}

BOOL ltm_http_is_running(void)
{
    return InterlockedCompareExchange(&g_running, 0, 0) != 0;
}

static void set_error_from_wsa(const WCHAR *what, int err)
{
    WCHAR *msg = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, (DWORD)err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (LPWSTR)&msg, 0, NULL);
    _snwprintf_s(g_last_error, LTM_COUNTOF(g_last_error), _TRUNCATE, L"%s (%d) %s",
                 what, err, (msg != NULL) ? msg : L"");
    if (msg != NULL) {
        LocalFree(msg);
    }
    /* Trim the trailing CRLF FormatMessage likes to add. */
    {
        size_t n = wcslen(g_last_error);
        while (n > 0 && (g_last_error[n - 1] == L'\r' || g_last_error[n - 1] == L'\n' ||
                         g_last_error[n - 1] == L' ')) {
            g_last_error[--n] = L'\0';
        }
    }
}

static void conn_reset(conn *c)
{
    if (c->s != INVALID_SOCKET) {
        closesocket(c->s);
        c->s = INVALID_SOCKET;
    }
    ltm_free(c->in);
    c->in = NULL;
    c->in_len = c->in_cap = 0;
    ltm_buf_free(&c->head);
    ltm_buf_free(&c->body_owned);
    c->body = NULL;
    c->body_len = 0;
    c->sent = 0;
    c->state = CONN_READING;
    c->ip = 0;
}

static conn *conn_alloc(void)
{
    int i;
    for (i = 0; i < LTM_HTTP_MAX_CONNS; ++i) {
        if (g_conns[i].s == INVALID_SOCKET) {
            return &g_conns[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Request parsing                                                     */
/* ------------------------------------------------------------------ */

/* Returns:  1 complete, 0 need more data, -1 malformed. */
static int parse_request(char *buf, size_t len, ltm_http_request *req,
                         size_t *consumed)
{
    char  *head_end;
    char  *line;
    char  *cursor;
    char  *sp1, *sp2, *q;
    size_t header_len;
    long   content_len = 0;

    buf[len] = '\0';
    head_end = strstr(buf, "\r\n\r\n");
    if (head_end == NULL) {
        return 0;
    }
    header_len = (size_t)(head_end - buf) + 4;

    ZeroMemory(req, sizeof(*req));

    /* --- request line --- */
    line = buf;
    cursor = strstr(line, "\r\n");
    if (cursor == NULL) {
        return -1;
    }
    *cursor = '\0';
    cursor += 2;

    sp1 = strchr(line, ' ');
    if (sp1 == NULL) {
        return -1;
    }
    *sp1 = '\0';
    ltm_strlcpy_a(req->method, sizeof(req->method), line);

    sp2 = strchr(sp1 + 1, ' ');
    if (sp2 != NULL) {
        *sp2 = '\0';
    }
    q = strchr(sp1 + 1, '?');
    if (q != NULL) {
        *q = '\0';
    }
    ltm_strlcpy_a(req->path, sizeof(req->path), sp1 + 1);
    ltm_url_decode(req->path);
    if (req->path[0] != '/') {
        return -1;
    }

    /* --- headers --- */
    while (cursor < head_end) {
        char *eol = strstr(cursor, "\r\n");
        char *colon;
        if (eol == NULL || eol == cursor) {
            break;
        }
        *eol = '\0';
        colon = strchr(cursor, ':');
        if (colon != NULL && req->header_count < LTM_HTTP_MAX_HEADERS) {
            char *value = colon + 1;
            *colon = '\0';
            while (*value == ' ' || *value == '\t') {
                ++value;
            }
            req->headers[req->header_count].name = cursor;
            req->headers[req->header_count].value = value;
            req->header_count++;
        }
        cursor = eol + 2;
    }

    {
        const char *cl = ltm_http_header_get(req, "Content-Length");
        if (cl != NULL) {
            content_len = strtol(cl, NULL, 10);
            if (content_len < 0 || content_len > LTM_HTTP_MAX_BODY) {
                return -1;
            }
        }
    }

    if (len < header_len + (size_t)content_len) {
        return 0; /* body still arriving */
    }

    req->body = buf + header_len;
    req->body_len = (size_t)content_len;
    /* Safe: the caller always keeps one spare byte for this NUL. */
    buf[header_len + (size_t)content_len] = '\0';

    *consumed = header_len + (size_t)content_len;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Response serialisation                                              */
/* ------------------------------------------------------------------ */

static void conn_prepare_response(conn *c, ltm_http_response *res)
{
    ltm_buf_reset(&c->head);

    if (res->is_static) {
        c->body = (const char *)res->body_static;
        c->body_len = res->body_static_len;
    } else {
        /* Steal the buffer instead of copying it. */
        ltm_buf_free(&c->body_owned);
        c->body_owned = res->body;
        ltm_buf_init(&res->body);
        c->body = c->body_owned.data;
        c->body_len = c->body_owned.len;
    }

    ltm_buf_printf(&c->head, "HTTP/1.1 %d %s\r\n", res->status, status_text(res->status));
    ltm_buf_printf(&c->head, "Content-Length: %llu\r\n", (unsigned long long)c->body_len);
    ltm_buf_printf(&c->head, "Content-Type: %s\r\n", res->content_type);
    ltm_buf_puts(&c->head, "Connection: close\r\n");
    ltm_buf_puts(&c->head, "X-Content-Type-Options: nosniff\r\n");
    ltm_buf_puts(&c->head, "Referrer-Policy: no-referrer\r\n");
    if (res->headers.len > 0) {
        ltm_buf_append(&c->head, res->headers.data, res->headers.len);
    }
    ltm_buf_puts(&c->head, "\r\n");

    c->sent = 0;
    c->state = CONN_WRITING;
}

static void conn_fail(conn *c, int status, const char *text)
{
    ltm_http_response res;
    ltm_http_response_init(&res);
    ltm_http_set_text(&res, status, "text/plain; charset=utf-8", text);
    conn_prepare_response(c, &res);
    ltm_http_response_free(&res);
}

/* ------------------------------------------------------------------ */
/* I/O                                                                 */
/* ------------------------------------------------------------------ */

static void conn_on_readable(conn *c)
{
    char  chunk[2048];
    int   n;
    ltm_http_request req;
    size_t consumed = 0;
    int    r;

    n = recv(c->s, chunk, (int)sizeof(chunk), 0);
    if (n == 0) {
        c->state = CONN_DONE;
        return;
    }
    if (n < 0) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            c->state = CONN_DONE;
        }
        return;
    }

    c->last_active = GetTickCount();

    if (c->in_len + (size_t)n + 2 > c->in_cap) {
        size_t ncap = (c->in_cap == 0) ? 4096 : c->in_cap;
        char  *ni;
        while (ncap < c->in_len + (size_t)n + 2) {
            ncap *= 2;
        }
        if (ncap > LTM_HTTP_MAX_REQUEST) {
            conn_fail(c, 413, "request too large");
            return;
        }
        ni = (char *)ltm_realloc(c->in, ncap);
        if (ni == NULL) {
            c->state = CONN_DONE;
            return;
        }
        c->in = ni;
        c->in_cap = ncap;
    }
    memcpy(c->in + c->in_len, chunk, (size_t)n);
    c->in_len += (size_t)n;

    r = parse_request(c->in, c->in_len, &req, &consumed);
    if (r == 0) {
        return; /* wait for more */
    }
    if (r < 0) {
        conn_fail(c, 400, "bad request");
        return;
    }

    req.client_ip = c->ip;
    {
        ltm_http_response res;
        ltm_http_response_init(&res);
        ltm_api_handle(&req, &res);
        conn_prepare_response(c, &res);
        ltm_http_response_free(&res);
    }
}

static void conn_on_writable(conn *c)
{
    for (;;) {
        const char *src;
        size_t      remain;
        int         n;

        if (c->sent < c->head.len) {
            src = c->head.data + c->sent;
            remain = c->head.len - c->sent;
        } else if (c->sent < c->head.len + c->body_len) {
            size_t off = c->sent - c->head.len;
            src = c->body + off;
            remain = c->body_len - off;
        } else {
            c->state = CONN_DONE;
            return;
        }

        if (remain > 32768) {
            remain = 32768;
        }
        n = send(c->s, src, (int)remain, 0);
        if (n > 0) {
            c->sent += (size_t)n;
            c->last_active = GetTickCount();
            continue;
        }
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
            return; /* try again on the next select */
        }
        c->state = CONN_DONE;
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Server thread                                                       */
/* ------------------------------------------------------------------ */

static void accept_new(void)
{
    struct sockaddr_in sa;
    int                salen = (int)sizeof(sa);
    SOCKET             s;
    conn              *c;
    u_long             nb = 1;

    s = accept(g_listener, (struct sockaddr *)&sa, &salen);
    if (s == INVALID_SOCKET) {
        return;
    }
    c = conn_alloc();
    if (c == NULL) {
        closesocket(s); /* at capacity: drop rather than queue */
        return;
    }
    ioctlsocket(s, FIONBIO, &nb);
    {
        BOOL nodelay = TRUE;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));
    }

    conn_reset(c);
    c->s = s;
    c->ip = ntohl(sa.sin_addr.s_addr);
    c->state = CONN_READING;
    c->last_active = GetTickCount();
    ltm_buf_init(&c->head);
    ltm_buf_init(&c->body_owned);
}

static DWORD WINAPI server_thread(LPVOID arg)
{
    LTM_UNUSED(arg);

    while (InterlockedCompareExchange(&g_stop, 0, 0) == 0) {
        fd_set rd, wr;
        struct timeval tv;
        SOCKET maxfd = 0;
        int    i, ready;
        DWORD  now;

        FD_ZERO(&rd);
        FD_ZERO(&wr);

        FD_SET(g_listener, &rd);
        if (g_listener > maxfd) maxfd = g_listener;
        FD_SET(g_wakeup, &rd);
        if (g_wakeup > maxfd) maxfd = g_wakeup;

        for (i = 0; i < LTM_HTTP_MAX_CONNS; ++i) {
            conn *c = &g_conns[i];
            if (c->s == INVALID_SOCKET) {
                continue;
            }
            if (c->state == CONN_READING) {
                FD_SET(c->s, &rd);
            } else if (c->state == CONN_WRITING) {
                FD_SET(c->s, &wr);
            }
            if (c->s > maxfd) {
                maxfd = c->s;
            }
        }

        tv.tv_sec = 2;
        tv.tv_usec = 0;
        ready = select((int)maxfd + 1, &rd, &wr, NULL, &tv);
        if (ready == SOCKET_ERROR) {
            break;
        }

        if (InterlockedCompareExchange(&g_stop, 0, 0) != 0) {
            break;
        }

        if (FD_ISSET(g_wakeup, &rd)) {
            char drain[64];
            struct sockaddr_in from;
            int fromlen = (int)sizeof(from);
            recvfrom(g_wakeup, drain, (int)sizeof(drain), 0,
                     (struct sockaddr *)&from, &fromlen);
        }
        if (FD_ISSET(g_listener, &rd)) {
            accept_new();
        }

        now = GetTickCount();
        for (i = 0; i < LTM_HTTP_MAX_CONNS; ++i) {
            conn *c = &g_conns[i];
            if (c->s == INVALID_SOCKET) {
                continue;
            }
            if (c->state == CONN_READING && FD_ISSET(c->s, &rd)) {
                conn_on_readable(c);
            }
            if (c->state == CONN_WRITING && FD_ISSET(c->s, &wr)) {
                conn_on_writable(c);
            }
            if (c->state == CONN_DONE) {
                conn_reset(c);
                continue;
            }
            if (now - c->last_active > LTM_HTTP_IDLE_TIMEOUT) {
                conn_reset(c);
            }
        }
    }

    /* Drain everything before the thread goes away. */
    {
        int i;
        for (i = 0; i < LTM_HTTP_MAX_CONNS; ++i) {
            conn_reset(&g_conns[i]);
        }
    }
    InterlockedExchange(&g_running, 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static BOOL create_wakeup_socket(void)
{
    int len = (int)sizeof(g_wakeup_addr);

    g_wakeup = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_wakeup == INVALID_SOCKET) {
        return FALSE;
    }
    ZeroMemory(&g_wakeup_addr, sizeof(g_wakeup_addr));
    g_wakeup_addr.sin_family = AF_INET;
    g_wakeup_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    g_wakeup_addr.sin_port = 0;
    if (bind(g_wakeup, (struct sockaddr *)&g_wakeup_addr, sizeof(g_wakeup_addr)) != 0) {
        closesocket(g_wakeup);
        g_wakeup = INVALID_SOCKET;
        return FALSE;
    }
    if (getsockname(g_wakeup, (struct sockaddr *)&g_wakeup_addr, &len) != 0) {
        closesocket(g_wakeup);
        g_wakeup = INVALID_SOCKET;
        return FALSE;
    }
    return TRUE;
}

BOOL ltm_http_start(int port)
{
    WSADATA            wsa;
    struct sockaddr_in sa;
    u_long             nb = 1;
    BOOL               exclusive = TRUE;
    int                i;

    if (ltm_http_is_running()) {
        return TRUE;
    }
    g_last_error[0] = L'\0';

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        set_error_from_wsa(L"WSAStartup", WSAGetLastError());
        return FALSE;
    }

    for (i = 0; i < LTM_HTTP_MAX_CONNS; ++i) {
        g_conns[i].s = INVALID_SOCKET;
        ltm_buf_init(&g_conns[i].head);
        ltm_buf_init(&g_conns[i].body_owned);
    }

    g_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listener == INVALID_SOCKET) {
        set_error_from_wsa(L"socket", WSAGetLastError());
        goto fail;
    }

    /* Deliberately NOT SO_REUSEADDR: on Windows that would let a second
     * instance silently steal the port from a running one. */
    setsockopt(g_listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               (const char *)&exclusive, sizeof(exclusive));

    ZeroMemory(&sa, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((unsigned short)port);

    if (bind(g_listener, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        set_error_from_wsa(L"bind", WSAGetLastError());
        goto fail;
    }
    if (listen(g_listener, SOMAXCONN) != 0) {
        set_error_from_wsa(L"listen", WSAGetLastError());
        goto fail;
    }
    ioctlsocket(g_listener, FIONBIO, &nb);

    if (!create_wakeup_socket()) {
        set_error_from_wsa(L"wakeup socket", WSAGetLastError());
        goto fail;
    }

    InterlockedExchange(&g_stop, 0);
    InterlockedExchange(&g_running, 1);

    g_thread = CreateThread(NULL, 64 * 1024, server_thread, NULL, 0, NULL);
    if (g_thread == NULL) {
        InterlockedExchange(&g_running, 0);
        set_error_from_wsa(L"CreateThread", (int)GetLastError());
        goto fail;
    }

    ltm_log(L"server listening on port %d", port);
    return TRUE;

fail:
    if (g_listener != INVALID_SOCKET) {
        closesocket(g_listener);
        g_listener = INVALID_SOCKET;
    }
    if (g_wakeup != INVALID_SOCKET) {
        closesocket(g_wakeup);
        g_wakeup = INVALID_SOCKET;
    }
    WSACleanup();
    return FALSE;
}

void ltm_http_stop(void)
{
    if (g_thread == NULL) {
        return;
    }
    InterlockedExchange(&g_stop, 1);

    /* Poke the select() loop so it notices immediately. */
    if (g_wakeup != INVALID_SOCKET) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s != INVALID_SOCKET) {
            char one = 'x';
            sendto(s, &one, 1, 0, (struct sockaddr *)&g_wakeup_addr,
                   sizeof(g_wakeup_addr));
            closesocket(s);
        }
    }

    WaitForSingleObject(g_thread, 5000);
    CloseHandle(g_thread);
    g_thread = NULL;

    if (g_listener != INVALID_SOCKET) {
        closesocket(g_listener);
        g_listener = INVALID_SOCKET;
    }
    if (g_wakeup != INVALID_SOCKET) {
        closesocket(g_wakeup);
        g_wakeup = INVALID_SOCKET;
    }
    InterlockedExchange(&g_running, 0);
    WSACleanup();
    ltm_log(L"server stopped");
}
