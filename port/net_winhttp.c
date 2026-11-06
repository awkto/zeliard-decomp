/* net_winhttp.c — net.h over WinHTTP (#57), the Windows build's backend so the
 * release needs no libcurl DLLs.  Same contract as net_curl.c: headers sent
 * (X-Client-Id, Authorization, and for PUT X-Save-Modified / Content-Type),
 * X-Save-Modified read back, status 0 + r->error on transport failure. */
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net.h"

#define NET_MAX_BODY (64u * 1024u * 1024u)

static int t_connect = 10, t_total = 120;

static void fail(NetResp *r, const char *what, DWORD code)
{
    const char *m = NULL;
    switch (code) {
    case ERROR_WINHTTP_TIMEOUT:               m = "timed out"; break;
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:     m = "could not resolve host"; break;
    case ERROR_WINHTTP_CANNOT_CONNECT:        m = "could not connect"; break;
    case ERROR_WINHTTP_CONNECTION_ERROR:     m = "connection error"; break;
    case ERROR_WINHTTP_SECURE_FAILURE:        m = "TLS failure"; break;
    case ERROR_WINHTTP_INVALID_URL:           m = "invalid URL"; break;
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:   m = "unsupported URL scheme"; break;
    }
    if (m) snprintf(r->error, sizeof r->error, "%s: %s", what, m);
    else   snprintf(r->error, sizeof r->error, "%s failed (Windows error %lu)", what, (unsigned long)code);
}

static WCHAR *widen(const char *s, int *n)
{
    int k = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (k <= 0) return NULL;
    WCHAR *w = malloc((size_t)k * sizeof *w);
    if (w && MultiByteToWideChar(CP_UTF8, 0, s, -1, w, k) <= 0) { free(w); return NULL; }
    if (n) *n = k - 1;
    return w;
}

static int perform(const wchar_t *method, const char *url, const NetAuth *au,
                   const void *body, size_t bn, int has_body, long long modified, NetResp *r)
{
    memset(r, 0, sizeof *r);
    HINTERNET ses = NULL, con = NULL, req = NULL;
    WCHAR *wurl = NULL, *host = NULL, *path = NULL, *wh = NULL;
    unsigned char *buf = NULL; size_t n = 0, cap = 0;
    DWORD err = 0; const char *step = "request";

    wurl = widen(url, NULL);
    if (!wurl) { snprintf(r->error, sizeof r->error, "bad URL encoding"); return 0; }
    URL_COMPONENTS uc; memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) { err = GetLastError(); step = "URL"; goto done; }
    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) {
        err = ERROR_WINHTTP_UNRECOGNIZED_SCHEME; step = "URL"; goto done;
    }
    host = calloc(uc.dwHostNameLength + 1, sizeof *host);
    path = calloc(uc.dwUrlPathLength + uc.dwExtraInfoLength + 2, sizeof *path);
    if (!host || !path) { err = ERROR_OUTOFMEMORY; goto done; }
    memcpy(host, uc.lpszHostName, uc.dwHostNameLength * sizeof *host);
    if (uc.dwUrlPathLength) memcpy(path, uc.lpszUrlPath, uc.dwUrlPathLength * sizeof *path);
    else path[0] = L'/';
    size_t pl = wcslen(path);
    if (uc.dwExtraInfoLength) memcpy(path + pl, uc.lpszExtraInfo, uc.dwExtraInfoLength * sizeof *path);

    ses = WinHttpOpen(L"zeliard-port-sync", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!ses) ses = WinHttpOpen(L"zeliard-port-sync", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!ses) { err = GetLastError(); step = "WinHttpOpen"; goto done; }
    int ct = t_connect * 1000, tt = t_total * 1000;
    WinHttpSetTimeouts(ses, ct, ct, tt, tt);   /* resolve, connect, send, receive; whole-request cap below */
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)tt;

    con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (!con) { err = GetLastError(); step = "connect"; goto done; }
    req = WinHttpOpenRequest(con, method, path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { err = GetLastError(); step = "request"; goto done; }

    char h[900]; int hl = 0;
    if (au && au->key && *au->key)     hl += snprintf(h + hl, sizeof h - hl, "X-Client-Id: %s\r\n", au->key);
    if (au && au->token && *au->token) hl += snprintf(h + hl, sizeof h - hl, "Authorization: Bearer %s\r\n", au->token);
    if (has_body) hl += snprintf(h + hl, sizeof h - hl,
                                 "X-Save-Modified: %lld\r\nContent-Type: application/octet-stream\r\n", modified);
    if (hl >= (int)sizeof h) { snprintf(r->error, sizeof r->error, "request headers too long"); goto done; }
    if (hl) {
        wh = widen(h, NULL);
        if (!wh || !WinHttpAddRequestHeaders(req, wh, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
            err = GetLastError(); step = "headers"; goto done;
        }
    }
    if (bn > 0xFFFFFFFEu) { snprintf(r->error, sizeof r->error, "request body too large"); goto done; }
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            has_body && bn ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA,
                            has_body ? (DWORD)bn : 0, has_body ? (DWORD)bn : 0, 0)) {
        err = GetLastError(); step = "send"; goto done;
    }
    if (!WinHttpReceiveResponse(req, NULL)) { err = GetLastError(); step = "receive"; goto done; }

    DWORD code = 0, sz = sizeof code;
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX)) {
        err = GetLastError(); step = "status"; goto done;
    }
    WCHAR mv[48]; sz = sizeof mv;
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, L"X-Save-Modified", mv, &sz, WINHTTP_NO_HEADER_INDEX)) {
        wchar_t *e; long long v = _wcstoi64(mv, &e, 10);
        if (e != mv) { r->modified = v; r->has_modified = 1; }
    }
    for (;;) {
        DWORD avail = 0, got = 0;
        if (GetTickCount64() > deadline) { err = ERROR_WINHTTP_TIMEOUT; step = "download"; goto done; }
        if (!WinHttpQueryDataAvailable(req, &avail)) { err = GetLastError(); step = "download"; goto done; }
        if (!avail) break;
        if (n + avail > NET_MAX_BODY) { snprintf(r->error, sizeof r->error, "response too large"); goto done; }
        if (n + avail + 1 > cap) {
            size_t c = cap ? cap : 4096;
            while (c < n + avail + 1) c *= 2;
            unsigned char *q = realloc(buf, c);
            if (!q) { err = ERROR_OUTOFMEMORY; goto done; }
            buf = q; cap = c;
        }
        if (!WinHttpReadData(req, buf + n, avail, &got)) { err = GetLastError(); step = "download"; goto done; }
        if (!got) break;
        n += got;
    }
    if (buf) buf[n] = 0;
    r->status = (int)code; r->body = buf; r->len = n; buf = NULL;
    err = 0;
done:
    if (err) fail(r, step, err);
    if (r->error[0] || err) r->status = 0;
    free(buf); free(wurl); free(host); free(path); free(wh);
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return r->status;
}

void net_set_timeouts(int connect_s, int total_s) { t_connect = connect_s; t_total = total_s; }
int net_supported(void) { return 1; }
int net_get(const char *url, const NetAuth *au, NetResp *r) { return perform(L"GET", url, au, NULL, 0, 0, 0, r); }
int net_put(const char *url, const NetAuth *au, const void *b, size_t n, long long m, NetResp *r)
{ return perform(L"PUT", url, au, b, n, 1, m, r); }
int net_delete(const char *url, const NetAuth *au, NetResp *r) { return perform(L"DELETE", url, au, NULL, 0, 0, 0, r); }
