/* net_curl.c — net.h over libcurl's easy API (#55).  Only linked into the
 * zeliard binary, and only when the Makefile found libcurl via pkg-config. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <curl/curl.h>
#include "net.h"

#define NET_MAX_BODY (64u * 1024u * 1024u)      /* a save bundle is ~1 MB; refuse nonsense */

static int g_init;

typedef struct Sink { unsigned char *p; size_t n, cap; int over; } Sink;

static size_t on_body(char *ptr, size_t sz, size_t nm, void *ud)
{
    Sink *s = ud; size_t n = sz * nm;
    if (s->n + n > NET_MAX_BODY) { s->over = 1; return 0; }
    if (s->n + n + 1 > s->cap) {
        size_t c = s->cap ? s->cap : 4096;
        while (c < s->n + n + 1) c *= 2;
        unsigned char *q = realloc(s->p, c);
        if (!q) return 0;
        s->p = q; s->cap = c;
    }
    memcpy(s->p + s->n, ptr, n); s->n += n; s->p[s->n] = 0;
    return n;
}

static size_t on_header(char *ptr, size_t sz, size_t nm, void *ud)
{
    NetResp *r = ud; size_t n = sz * nm;
    static const char h[] = "x-save-modified:";
    if (n > sizeof h - 1 && !strncasecmp(ptr, h, sizeof h - 1)) {
        char tmp[40]; size_t m = n - (sizeof h - 1);
        if (m >= sizeof tmp) m = sizeof tmp - 1;
        memcpy(tmp, ptr + sizeof h - 1, m); tmp[m] = 0;
        char *e; long long v = strtoll(tmp, &e, 10);
        if (e != tmp) { r->modified = v; r->has_modified = 1; }
    }
    return n;
}

static volatile int t_connect = 10, t_total = 120;   /* net_set_timeouts; the desktop client waits 120 s */

static int perform(const char *method, const char *url, const NetAuth *au,
                   const void *body, size_t bn, int has_body, long long modified, NetResp *r)
{
    memset(r, 0, sizeof *r);
    if (!g_init) { curl_global_init(CURL_GLOBAL_DEFAULT); g_init = 1; }
    CURL *c = curl_easy_init();
    if (!c) { snprintf(r->error, sizeof r->error, "curl_easy_init failed"); return 0; }
    struct curl_slist *hd = NULL;
    char line[320];
    if (au && au->key && *au->key)     { snprintf(line, sizeof line, "X-Client-Id: %s", au->key); hd = curl_slist_append(hd, line); }
    if (au && au->token && *au->token) { snprintf(line, sizeof line, "Authorization: Bearer %s", au->token); hd = curl_slist_append(hd, line); }
    if (has_body) {
        snprintf(line, sizeof line, "X-Save-Modified: %lld", modified); hd = curl_slist_append(hd, line);
        hd = curl_slist_append(hd, "Content-Type: application/octet-stream");
        hd = curl_slist_append(hd, "Expect:");
    }
    Sink s; memset(&s, 0, sizeof s);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hd);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, r);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, (long)t_connect);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)t_total);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "zeliard-port-sync");
    if (has_body) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body ? (const char *)body : "");
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)bn);
    }
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    if (rc == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    else snprintf(r->error, sizeof r->error, "%s", s.over ? "response too large" : curl_easy_strerror(rc));
    curl_slist_free_all(hd);
    curl_easy_cleanup(c);
    r->status = (int)code; r->body = s.p; r->len = s.n;
    return r->status;
}

void net_set_timeouts(int connect_s, int total_s) { t_connect = connect_s; t_total = total_s; }
int net_supported(void) { return 1; }
int net_get(const char *url, const NetAuth *au, NetResp *r) { return perform("GET", url, au, NULL, 0, 0, 0, r); }
int net_put(const char *url, const NetAuth *au, const void *b, size_t n, long long m, NetResp *r)
{ return perform("PUT", url, au, b, n, 1, m, r); }
int net_delete(const char *url, const NetAuth *au, NetResp *r) { return perform("DELETE", url, au, NULL, 0, 0, 0, r); }
