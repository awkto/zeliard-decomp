/* net.h — the smallest HTTP surface the save sync needs (#55, docs/SYNC.md 6.3).
 *
 * Three verbs, one response struct.  The real backend is net_curl.c (libcurl,
 * easy API); builds without libcurl link net_stub.c instead, which reports
 * "built without network support"; test_sync.c supplies an in-memory fake.
 * Nothing here knows about saves. */
#ifndef ZEL_NET_H
#define ZEL_NET_H
#include <stddef.h>
#include <stdlib.h>

typedef struct NetResp {
    int            status;          /* HTTP status; 0 = no response (see error) */
    unsigned char *body;            /* malloc'd, NUL-terminated (len excludes it); NULL if empty */
    size_t         len;
    long long      modified;        /* X-Save-Modified response header, valid if has_modified */
    int            has_modified;
    char           error[160];      /* transport failure text when status == 0 */
} NetResp;

typedef struct NetAuth {            /* both optional ("" or NULL = not sent) */
    const char *key;                /* X-Client-Id */
    const char *token;              /* Authorization: Bearer */
} NetAuth;

/* 1 when this build can talk HTTP at all. */
int net_supported(void);

/* Seconds to wait for a connection / for a whole request, for the calls that
 * follow (defaults 10 / 120, the desktop client's).  The in-game lifecycle sets
 * a short pair so a dead server never holds up play.  Not thread-safe: set it
 * while no request is in flight. */
void net_set_timeouts(int connect_s, int total_s);

/* Each returns the HTTP status (0 on a transport failure) and fills *r.  The
 * caller owns r->body (net_resp_free).  `modified` goes out as X-Save-Modified. */
int net_get(const char *url, const NetAuth *au, NetResp *r);
int net_put(const char *url, const NetAuth *au, const void *body, size_t n,
            long long modified, NetResp *r);
int net_delete(const char *url, const NetAuth *au, NetResp *r);

static inline void net_resp_free(NetResp *r) { free(r->body); r->body = NULL; r->len = 0; }

#endif
