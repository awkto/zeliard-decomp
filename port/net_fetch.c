/* net_fetch.c -- net.h over the browser's fetch(), for the Emscripten build (#55).
 *
 * Why EM_ASYNC_JS and not emscripten_fetch: the web build is single threaded
 * (ASYNCIFY, no pthreads), and emscripten_fetch's EMSCRIPTEN_FETCH_SYNCHRONOUS
 * needs a pthread worker.  Its async mode would mean polling with
 * emscripten_sleep, a worse copy of what an ASYNCIFY import does natively:
 * EM_ASYNC_JS makes the C call look blocking (the wasm stack is unwound while
 * the promise runs, the browser keeps running) with the real fetch API, so
 * request headers, the response header X-Save-Modified, AbortController
 * timeouts and binary bodies need no glue.  Every function between here and
 * the caller must be instrumented, i.e. not in asyncify_remove.txt (sync_push,
 * sync_pull, sync_health, sync_remote_auto, ... are listed in asyncify_only.txt).
 *
 * The callers (synclife) run this from frame_wait, never from deep inside the
 * engine, and every request is aborted after min(total timeout, 8 s). */
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net.h"

#define NET_MAX_BODY (64u * 1024u * 1024u)
#define WEB_CAP_S 8                          /* the longest the frame loop is ever held for one request */

/* Returns the HTTP status, 0 on a transport failure (text in err).  *outp is a
 * malloc'd NUL-terminated copy of the body (NULL when empty), *lenp its length,
 * *modp / *hasmodp the X-Save-Modified header. */
EM_ASYNC_JS(int, zel_fetch, (const char *method, const char *url, const char *key, const char *token,
                             const void *body, int bn, int has_body, double modified, int timeout_s,
                             unsigned char **outp, int *lenp, double *modp, int *hasmodp,
                             char *err, int errn, unsigned max_body), {
    const put = (p, v) => { HEAPU32[p >> 2] = v; };
    const setErr = (m) => { stringToUTF8(String(m), err, errn); };
    const ctl = new AbortController();
    const timer = setTimeout(() => ctl.abort(), timeout_s * 1000);
    try {
        const h = {};
        const k = UTF8ToString(key), t = UTF8ToString(token);
        if (k) h['X-Client-Id'] = k;
        if (t) h['Authorization'] = 'Bearer ' + t;
        const init = { method: UTF8ToString(method), headers: h, signal: ctl.signal, cache: 'no-store', credentials: 'same-origin' };
        if (has_body) {
            h['X-Save-Modified'] = String(Math.round(modified));
            h['Content-Type'] = 'application/octet-stream';
            init.body = HEAPU8.slice(body, body + bn);      /* a copy: the heap may grow while we wait */
        }
        const r = await fetch(UTF8ToString(url), init);
        const m = r.headers.get('X-Save-Modified');
        if (m !== null && m !== '' && isFinite(Number(m))) { HEAPF64[modp >> 3] = Number(m); put(hasmodp, 1); }
        const buf = new Uint8Array(await r.arrayBuffer());
        if (buf.length > max_body) { setErr('response too large'); return 0; }
        if (buf.length) {
            const p = _malloc(buf.length + 1);
            if (!p) { setErr('out of memory'); return 0; }
            HEAPU8.set(buf, p); HEAPU8[p + buf.length] = 0;
            put(outp, p); put(lenp, buf.length);
        }
        return r.status;
    } catch (e) {
        setErr(e && e.name === 'AbortError' ? 'timed out' : (e && e.message) || e);
        return 0;
    } finally { clearTimeout(timer); }
});

static int t_connect = 10, t_total = 120;

static int perform(const char *method, const char *url, const NetAuth *au,
                   const void *body, size_t bn, int has_body, long long modified, NetResp *r)
{
    memset(r, 0, sizeof *r);
    int to = t_total < WEB_CAP_S ? t_total : WEB_CAP_S;
    if (to < 1) to = 1;
    unsigned char *out = NULL; int len = 0, has = 0; double mod = 0;
    int st = zel_fetch(method, url, au && au->key ? au->key : "", au && au->token ? au->token : "",
                       body, (int)bn, has_body, (double)modified, to, &out, &len, &mod, &has,
                       r->error, (int)sizeof r->error, NET_MAX_BODY);
    r->status = st; r->body = out; r->len = (size_t)len;
    if (has) { r->modified = (long long)mod; r->has_modified = 1; }
    if (!st && !r->error[0]) snprintf(r->error, sizeof r->error, "network error");
    return st;
}

void net_set_timeouts(int connect_s, int total_s) { t_connect = connect_s; t_total = total_s; (void)t_connect; }
int net_supported(void) { return 1; }
int net_get(const char *url, const NetAuth *au, NetResp *r) { return perform("GET", url, au, NULL, 0, 0, 0, r); }
int net_put(const char *url, const NetAuth *au, const void *b, size_t n, long long m, NetResp *r)
{ return perform("PUT", url, au, b, n, 1, m, r); }
int net_delete(const char *url, const NetAuth *au, NetResp *r) { return perform("DELETE", url, au, NULL, 0, 0, 0, r); }
