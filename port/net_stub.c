/* net_stub.c — the no-libcurl build of net.h: every call fails cleanly, so
 * `--sync` says so and the rest of the port is unaffected. */
#include <string.h>
#include "net.h"

static int nope(NetResp *r)
{
    memset(r, 0, sizeof *r);
    strcpy(r->error, "built without network support (libcurl not found at build time)");
    return 0;
}
void net_set_timeouts(int connect_s, int total_s) { (void)connect_s; (void)total_s; }
int net_supported(void) { return 0; }
int net_get(const char *url, const NetAuth *au, NetResp *r) { (void)url; (void)au; return nope(r); }
int net_put(const char *url, const NetAuth *au, const void *b, size_t n, long long m, NetResp *r)
{ (void)url; (void)au; (void)b; (void)n; (void)m; return nope(r); }
int net_delete(const char *url, const NetAuth *au, NetResp *r) { (void)url; (void)au; return nope(r); }
