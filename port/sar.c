/* sar.c — .SAR reader and the 8-opcode RLE engine of STICK.BIN @0D9D (see
 * tools/sardec.py for the annotated Python original; the DX "remaining input"
 * counter is mirrored exactly so that malformed tails behave the same). */
#include "sar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

typedef struct { uint8_t *buf; size_t len, cap; } Out;

static int out_put(Out *o, uint8_t v, size_t n)
{
    if (o->len + n > o->cap) {
        size_t nc = o->cap ? o->cap : 4096;
        while (nc < o->len + n) nc *= 2;
        uint8_t *nb = realloc(o->buf, nc);
        if (!nb) return -1;
        o->buf = nb; o->cap = nc;
    }
    memset(o->buf + o->len, v, n);
    o->len += n;
    return 0;
}

uint8_t *sar_decompress(const uint8_t *d, size_t len, size_t *out_len)
{
    Out o = {0};
    size_t si = 0;
    long dx = (long)len;                        /* mirrors DX exactly */
    if (len == 0) return NULL;
#define LODSB(cnt) (si < len ? (cnt ? (dx--, d[si++]) : d[si++]) : (dx = 0, 0))
    int op = LODSB(1) & 7;

    if (op == 0) {                              /* 0DCC: stored */
        if (dx > 0 && si + (size_t)dx <= len) {
            if (out_put(&o, 0, (size_t)dx)) goto fail;
            memcpy(o.buf, d + si, (size_t)dx);
        }
    } else if (op == 1 || op == 3) {            /* 0DD1 / 0E34: table RLE */
        size_t table_at = si;
        while (LODSB(1) != 0xFF && dx > 0) LODSB(1);
        while (dx > 0 && si < len) {
            uint8_t b = d[si++];
            size_t cx = 1; uint8_t al = b;
            size_t bp = table_at;
            if (op == 1) {
                while (bp + 1 < len && (d[bp] & 0x0F) == 0) {
                    if (d[bp] == (b & 0xF0)) { cx = (b & 0x0F) + 2; al = d[bp + 1]; break; }
                    bp += 2;
                }
            } else {
                while (bp + 1 < len && (d[bp] & 0xF0) == 0) {
                    if (d[bp] == (b & 0x0F)) { cx = (b >> 4) + 2; al = d[bp + 1]; break; }
                    bp += 2;
                }
            }
            if (out_put(&o, al, cx)) goto fail;
            dx--;
        }
    } else if (op == 2 || op == 4) {            /* 0E13 / 0E73: marker RLE */
        uint8_t marker = LODSB(1);
        while (dx > 0 && si < len) {
            uint8_t b = d[si++];
            if (op == 2 && (b & 0xF0) == marker) {
                size_t cx = (b & 0x0F) + 3; uint8_t al = LODSB(1);
                if (out_put(&o, al, cx)) goto fail;
            } else if (op == 4 && (b & 0x0F) == marker) {
                size_t cx = (b >> 4) + 3; uint8_t al = LODSB(1);
                if (out_put(&o, al, cx)) goto fail;
            } else if (out_put(&o, b, 1)) goto fail;
            dx--;
        }
    } else if (op == 5) {                       /* 0E9C: doubled byte + count */
        while (dx > 0 && si < len) {
            uint8_t b = d[si++];
            size_t cx = 1;
            if (si + 1 < len && d[si] == b) { cx = d[si + 1] + 2; si += 2; dx -= 2; }
            if (out_put(&o, b, cx)) goto fail;
            dx--;
        }
    } else if (op == 6) {                       /* 0EBA: byte-keyed table, explicit count */
        size_t table_at = si;
        for (;;) {
            if (si + 1 >= len) goto fail;
            uint16_t w = rd16(d + si); si += 2; dx -= 2;
            if (w == 0xFFFF) break;
        }
        while (dx > 0 && si < len) {
            uint8_t b = d[si++];
            size_t cx = 1; uint8_t al = b;
            size_t bp = table_at;
            while (bp + 1 < len && rd16(d + bp) != 0xFFFF) {
                if (d[bp] == b) { cx = (size_t)LODSB(1) + 2; al = d[bp + 1]; break; }
                bp += 2;
            }
            if (out_put(&o, al, cx)) goto fail;
            dx--;
        }
    } else {                                    /* 0EF5: escape byte */
        uint8_t esc = LODSB(1);
        while (dx > 0 && si < len) {
            uint8_t b = d[si++];
            if (b == esc) {
                uint8_t val = si < len ? d[si++] : 0;
                uint8_t cnt = si < len ? d[si++] : 0;
                dx -= 2;
                if (out_put(&o, val, (size_t)cnt + 3)) goto fail;
            } else if (out_put(&o, b, 1)) goto fail;
            dx--;
        }
    }
#undef LODSB
    *out_len = o.len;
    if (!o.buf) o.buf = malloc(1);
    return o.buf;
fail:
    free(o.buf);
    return NULL;
}

uint8_t *sar_read_raw(const char *dir, int archive, int index, size_t *out_len)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/ZELRES%d.SAR", dir, archive + 1);
    FILE *f = fopen(path, "rb");
    if (!f) {
        /* the game files are upper-case on disk; try lower-case too */
        snprintf(path, sizeof path, "%s/zelres%d.sar", dir, archive + 1);
        f = fopen(path, "rb");
        if (!f) { fprintf(stderr, "sar: cannot open %s\n", path); return NULL; }
    }
    uint8_t hdr[4];
    if (fread(hdr, 1, 4, f) != 4) { fclose(f); return NULL; }
    uint32_t count = rd32(hdr) / 4;
    if ((uint32_t)index >= count) { fprintf(stderr, "sar: index %d out of range (%u)\n", index, count); fclose(f); return NULL; }
    fseek(f, (long)index * 4, SEEK_SET);
    if (fread(hdr, 1, 4, f) != 4) { fclose(f); return NULL; }
    uint32_t off = rd32(hdr);
    fseek(f, (long)off, SEEK_SET);
    if (fread(hdr, 1, 4, f) != 4) { fclose(f); return NULL; }
    uint32_t len = rd32(hdr);
    uint8_t *buf = malloc(len ? len : 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, len, f) != len) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = len;
    return buf;
}

uint8_t *sar_load(const char *dir, int archive, int index, int variant, size_t *out_len)
{
    size_t len;
    uint8_t *raw = sar_read_raw(dir, archive, index, &len);
    if (!raw) return NULL;
    uint8_t *out = NULL;
    if (len == 0) { *out_len = 0; return raw; }
    if (raw[0] == 0) {                                   /* plain: one stream */
        out = sar_decompress(raw + 1, len - 1, out_len);
    } else if (len >= 5) {
        size_t la = rd16(raw + 1), lb = rd16(raw + 3);
        if (5 + la + lb == len) {                        /* two per-video-mode variants */
            const uint8_t *s = variant == 0 ? raw + 5 : raw + 5 + la;
            out = sar_decompress(s, variant == 0 ? la : lb, out_len);
        }
    }
    if (!out) { *out_len = len; return raw; }            /* raw (code overlay etc.) */
    free(raw);
    return out;
}
