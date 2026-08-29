#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];
static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static uint32_t crc32b(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static int chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t hdr[8];
    put32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    uint8_t *tmp = malloc(len + 4);
    if (!tmp) return -1;
    memcpy(tmp, type, 4);
    if (len) memcpy(tmp + 4, data, len);
    uint32_t crc = crc32b(tmp, len + 4);
    free(tmp);
    uint8_t c[4]; put32(c, crc);
    return fwrite(hdr, 1, 8, f) != 8 || (len && fwrite(data, 1, len, f) != len) || fwrite(c, 1, 4, f) != 4;
}

int png_write_rgb(const char *path, const uint8_t *rgb, int w, int h)
{
    static int init = 0;
    if (!init) { crc_init(); init = 1; }
    size_t stride = (size_t)w * 3 + 1;
    size_t rawlen = stride * (size_t)h;
    uint8_t *raw = malloc(rawlen);
    if (!raw) return -1;
    for (int y = 0; y < h; y++) {
        raw[y * stride] = 0;                             /* filter: none */
        memcpy(raw + y * stride + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    /* zlib stream with stored blocks of <= 65535 bytes */
    size_t nblocks = (rawlen + 65534) / 65535;
    size_t zlen = 2 + rawlen + nblocks * 5 + 4;
    uint8_t *z = malloc(zlen);
    if (!z) { free(raw); return -1; }
    size_t zp = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawlen; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < rawlen || nblocks == 0; ) {
        size_t n = rawlen - off; if (n > 65535) n = 65535;
        int last = off + n >= rawlen;
        z[zp++] = (uint8_t)last;
        z[zp++] = n & 0xFF; z[zp++] = n >> 8;
        z[zp++] = ~n & 0xFF; z[zp++] = (~n >> 8) & 0xFF;
        memcpy(z + zp, raw + off, n); zp += n; off += n;
        if (last) break;
    }
    put32(z + zp, (b << 16) | a); zp += 4;
    free(raw);

    FILE *f = fopen(path, "wb");
    if (!f) { free(z); return -1; }
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w); put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    int err = chunk(f, "IHDR", ihdr, 13) || chunk(f, "IDAT", z, zp) || chunk(f, "IEND", NULL, 0);
    free(z);
    fclose(f);
    return err ? -1 : 0;
}
