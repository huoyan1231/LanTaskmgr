/*
 * http.h - a very small HTTP/1.1 server built directly on Winsock.
 *
 * One thread, one select() loop, non-blocking sockets and a per-connection
 * state machine. There are never more than a couple of clients (one phone,
 * polling every two seconds), so a thread per request would be pure overhead.
 *
 * Only what this application speaks is implemented: GET and POST, request
 * bodies up to 4 KiB, no chunked transfer, no keep-alive pipelining.
 */
#ifndef LTM_HTTP_H
#define LTM_HTTP_H

#include "common.h"

#define LTM_HTTP_MAX_HEADERS  32
#define LTM_HTTP_MAX_REQUEST  (16 * 1024)
#define LTM_HTTP_MAX_BODY     (4 * 1024)
#define LTM_HTTP_MAX_CONNS    32
#define LTM_HTTP_IDLE_TIMEOUT 20000 /* ms */

typedef struct ltm_http_header {
    const char *name;
    const char *value;
} ltm_http_header;

typedef struct ltm_http_request {
    char            method[8];
    char            path[512];  /* percent-decoded, query stripped */
    ltm_http_header headers[LTM_HTTP_MAX_HEADERS];
    int             header_count;
    const char     *body;
    size_t          body_len;
    unsigned long   client_ip;  /* host byte order */
    char            host[256];   /* value of the Host header, if present */
    char            origin[256];  /* value of the Origin header, if present */
    char            ua_hash[44];  /* SHA-256(User-Agent) fingerprint, base64 */
} ltm_http_request;

/* Case-insensitive lookup. Returns NULL when absent. */
const char *ltm_http_header_get(const ltm_http_request *r, const char *name);

typedef struct ltm_http_response {
    int         status;
    const char *content_type;
    ltm_buf     headers;      /* extra lines, each terminated with CRLF */

    /* Exactly one of these is used. A static body points into the mapped
     * executable image and is sent without being copied. */
    const void *body_static;
    size_t      body_static_len;
    ltm_buf     body;
    BOOL        is_static;
} ltm_http_response;

void ltm_http_response_init(ltm_http_response *res);
void ltm_http_response_free(ltm_http_response *res);
void ltm_http_add_header(ltm_http_response *res, const char *line);
void ltm_http_set_static(ltm_http_response *res, const void *data, size_t len,
                         const char *content_type);
void ltm_http_set_text(ltm_http_response *res, int status, const char *content_type,
                       const char *text);

/* Application callback. Implemented in api.c. */
void ltm_api_handle(const ltm_http_request *req, ltm_http_response *res);

/* Server lifecycle. ltm_http_start() binds, listens and spawns the network
 * thread; it returns FALSE and fills the last-error string on failure.
 * `bind_ip` may be NULL or empty to listen on all interfaces (INADDR_ANY);
 * otherwise it is an IPv4 literal (e.g. "192.168.1.50") the socket binds to. */
BOOL         ltm_http_start(int port, const WCHAR *bind_ip);
void         ltm_http_stop(void);
BOOL         ltm_http_is_running(void);
const WCHAR *ltm_http_last_error(void);

/* Percent-decoding helper, decodes in place and returns the new length. */
size_t ltm_url_decode(char *s);

#endif /* LTM_HTTP_H */
