// Writes a multi-size .ico of Clawd for the exe resource (build step). The
// bigger sizes go in as PNG (fine in .ico since Vista), which takes them from
// most of the exe's size down to next to nothing.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

static const int SIZES[] = { 16, 24, 32, 48, 64, 128 };
#define NSIZES (int)(sizeof SIZES / sizeof SIZES[0])
#define PNG_FROM 48   // this size and up: PNG; smaller ones a plain bitmap

static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, unsigned v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

/* ---- a small PNG writer: filtered rows, deflated with fixed Huffman codes ---- */

typedef struct {
    unsigned char *buf;
    size_t n, cap;
    unsigned bits;
    int nbits;
} Out;

static void byte(Out *o, unsigned b)
{
    if (o->n == o->cap) o->buf = realloc(o->buf, o->cap = o->cap ? o->cap * 2 : 4096);
    o->buf[o->n++] = (unsigned char)b;
}

static void be32(Out *o, unsigned v)
{
    for (int i = 3; i >= 0; i--) byte(o, (v >> (i * 8)) & 255);
}

static void bits(Out *o, unsigned v, int n)   // deflate's order: least significant bit first
{
    o->bits |= v << o->nbits;
    o->nbits += n;
    while (o->nbits >= 8) {
        byte(o, o->bits & 255);
        o->bits >>= 8;
        o->nbits -= 8;
    }
}

static void code(Out *o, unsigned c, int len)   // Huffman codes go most significant bit first
{
    unsigned r = 0;
    for (int i = 0; i < len; i++) r |= ((c >> i) & 1) << (len - 1 - i);
    bits(o, r, len);
}

static void symbol(Out *o, int s)   // the fixed literal/length code
{
    if (s < 144) code(o, 0x30 + s, 8);
    else if (s < 256) code(o, 0x190 + s - 144, 9);
    else if (s < 280) code(o, s - 256, 7);
    else code(o, 0xC0 + s - 280, 8);
}

static const int LBASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
                               131, 163, 195, 227, 258 };
static const int LEXT[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const int DBASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025,
                               1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const int DEXT[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void match(Out *o, int len, int dist)
{
    int l = 28, d = 29;
    while (LBASE[l] > len) l--;
    while (DBASE[d] > dist) d--;
    symbol(o, 257 + l);
    bits(o, (unsigned)(len - LBASE[l]), LEXT[l]);
    code(o, (unsigned)d, 5);
    bits(o, (unsigned)(dist - DBASE[d]), DEXT[d]);
}

// zlib stream of in[0..n): one fixed-code block, greedy LZ77 over hash chains.
static void zlib(Out *o, const unsigned char *in, int n)
{
    enum { HASH = 1 << 15, WINDOW = 32768, CHAIN = 256, MAXLEN = 258 };
    int *head = malloc(sizeof(int) * HASH), *prev = malloc(sizeof(int) * (n + 1));
    for (int i = 0; i < HASH; i++) head[i] = -1;
    byte(o, 0x78);
    byte(o, 0x01);
    bits(o, 1, 1);   // the last block,
    bits(o, 1, 2);   // fixed codes
    for (int i = 0; i < n;) {
        int best = 0, at = 0;
        if (i + 2 < n) {
            unsigned h = ((unsigned)in[i] << 10 ^ (unsigned)in[i + 1] << 5 ^ in[i + 2]) & (HASH - 1);
            for (int j = head[h], c = 0; j >= 0 && i - j <= WINDOW && c < CHAIN; j = prev[j], c++) {
                int len = 0;
                while (len < MAXLEN && i + len < n && in[j + len] == in[i + len]) len++;
                if (len > best) best = len, at = j;
            }
        }
        int step = best >= 3 ? best : 1;
        if (best >= 3) match(o, best, i - at);
        else symbol(o, in[i]);
        for (int k = 0; k < step; k++, i++)   // every position goes into the chains
            if (i + 2 < n) {
                unsigned h = ((unsigned)in[i] << 10 ^ (unsigned)in[i + 1] << 5 ^ in[i + 2]) & (HASH - 1);
                prev[i] = head[h];
                head[h] = i;
            }
    }
    symbol(o, 256);
    if (o->nbits) bits(o, 0, 8 - o->nbits);
    unsigned a = 1, b = 0;
    for (int i = 0; i < n; i++) {
        a = (a + in[i]) % 65521;
        b = (b + a) % 65521;
    }
    be32(o, b << 16 | a);
    free(head);
    free(prev);
}

static unsigned crc(const unsigned char *p, size_t n)
{
    unsigned c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

static void chunk(Out *o, const char *type, const unsigned char *data, size_t n)
{
    be32(o, (unsigned)n);
    size_t from = o->n;
    for (int i = 0; i < 4; i++) byte(o, (unsigned char)type[i]);
    for (size_t i = 0; i < n; i++) byte(o, data[i]);
    be32(o, crc(o->buf + from, o->n - from));
}

// img: s x s straight ARGB. Each row filtered whichever way leaves the smallest numbers.
static Out png(const uint32_t *img, int s)
{
    int row = s * 4 + 1;
    unsigned char *raw = malloc((size_t)row * s), *cur = malloc((size_t)s * 4), *up = calloc((size_t)s * 4, 1);
    for (int y = 0; y < s; y++) {
        for (int x = 0; x < s; x++) {
            uint32_t p = img[y * s + x];
            cur[x * 4] = (p >> 16) & 255;
            cur[x * 4 + 1] = (p >> 8) & 255;
            cur[x * 4 + 2] = p & 255;
            cur[x * 4 + 3] = p >> 24;
        }
        unsigned char *r = raw + (size_t)y * row;
        long best = -1;
        for (int f = 0; f < 3; f++) {   // none, sub, up
            unsigned char t[4 * 512];
            long sum = 0;
            for (int i = 0; i < s * 4; i++) {
                int a = i >= 4 ? cur[i - 4] : 0;
                t[i] = (unsigned char)(cur[i] - (f == 1 ? a : f == 2 ? up[i] : 0));
                sum += t[i] < 128 ? t[i] : 256 - t[i];
            }
            if (best < 0 || sum < best) {
                best = sum;
                r[0] = (unsigned char)f;
                memcpy(r + 1, t, (size_t)s * 4);
            }
        }
        memcpy(up, cur, (size_t)s * 4);
    }
    Out o = { 0 }, z = { 0 };
    static const unsigned char SIG[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    for (int i = 0; i < 8; i++) byte(&o, SIG[i]);
    unsigned char ihdr[13] = { 0 };
    for (int i = 0; i < 2; i++) {
        ihdr[i * 4 + 2] = (unsigned char)(s >> 8);
        ihdr[i * 4 + 3] = (unsigned char)s;
    }
    ihdr[8] = 8;   // bits per channel
    ihdr[9] = 6;   // RGBA
    chunk(&o, "IHDR", ihdr, 13);
    zlib(&z, raw, row * s);
    chunk(&o, "IDAT", z.buf, z.n);
    chunk(&o, "IEND", NULL, 0);
    free(z.buf);
    free(raw);
    free(cur);
    free(up);
    return o;
}

/* ---- the .ico ---- */

static unsigned bitmap_bytes(int s)
{
    return 40 + (unsigned)s * s * 4 + (unsigned)((s + 31) / 32 * 4) * s;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    FILE *f = fopen(argv[1], "wb");
    if (!f) return 1;

    uint32_t lp[16 * 10];
    Canvas c = { lp, 16, 10 };
    Pose q = { 0 };
    q.eyes = EYE_OPEN;
    for (int i = 0; i < 4; i++) q.legs[i] = 2;
    cv_clear(&c);
    draw_clawd(&c, -1, 0, &q);

    uint32_t *img[NSIZES];
    Out pngs[NSIZES] = { { 0 } };
    for (int i = 0; i < NSIZES; i++) {
        int s = SIZES[i], k = s / 16 < 1 ? 1 : s / 16;
        int ox = (s - 16 * k) / 2, oy = (s - 10 * k) / 2;
        img[i] = calloc((size_t)s * s, 4);
        for (int y = 0; y < 10 * k; y++)
            for (int x = 0; x < 16 * k; x++)
                img[i][(oy + y) * s + ox + x] = lp[(y / k) * 16 + x / k];
        if (s >= PNG_FROM) pngs[i] = png(img[i], s);
    }

    put16(f, 0);
    put16(f, 1);
    put16(f, NSIZES);
    unsigned off = 6 + 16 * NSIZES;
    for (int i = 0; i < NSIZES; i++) {
        int s = SIZES[i];
        unsigned bytes = pngs[i].n ? (unsigned)pngs[i].n : bitmap_bytes(s);
        fputc(s, f);
        fputc(s, f);
        fputc(0, f);
        fputc(0, f);
        put16(f, 1);
        put16(f, 32);
        put32(f, bytes);
        put32(f, off);
        off += bytes;
    }

    for (int i = 0; i < NSIZES; i++) {
        int s = SIZES[i];
        if (pngs[i].n) {
            fwrite(pngs[i].buf, 1, pngs[i].n, f);
        } else {
            put32(f, 40);
            put32(f, s);
            put32(f, s * 2);   // XOR + AND mask
            put16(f, 1);
            put16(f, 32);
            for (int j = 0; j < 6; j++) put32(f, 0);
            for (int y = s - 1; y >= 0; y--)   // bottom-up
                fwrite(img[i] + y * s, 4, s, f);
            for (int j = 0; j < (s + 31) / 32 * 4 * s; j++) fputc(0, f);
        }
        free(pngs[i].buf);
        free(img[i]);
    }
    fclose(f);
    return 0;
}
