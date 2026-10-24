// Writes a multi-size .ico of Clawd for the exe resource (build step).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

static const int SIZES[] = { 16, 24, 32, 48, 64, 128 };
#define NSIZES (int)(sizeof SIZES / sizeof SIZES[0])

static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, unsigned v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

static unsigned image_bytes(int s)
{
    return 40 + (unsigned)s * s * 4 + (unsigned)((s + 31) / 32 * 4) * s;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    FILE *f = fopen(argv[1], "wb");
    if (!f) return 1;

    put16(f, 0);
    put16(f, 1);
    put16(f, NSIZES);
    unsigned off = 6 + 16 * NSIZES;
    for (int i = 0; i < NSIZES; i++) {
        int s = SIZES[i];
        fputc(s, f);
        fputc(s, f);
        fputc(0, f);
        fputc(0, f);
        put16(f, 1);
        put16(f, 32);
        put32(f, image_bytes(s));
        put32(f, off);
        off += image_bytes(s);
    }

    uint32_t lp[16 * 10];
    Canvas c = { lp, 16, 10 };
    Pose q = { 0 };
    q.eyes = EYE_OPEN;
    for (int i = 0; i < 4; i++) q.legs[i] = 2;
    cv_clear(&c);
    draw_clawd(&c, -1, 0, &q);

    for (int i = 0; i < NSIZES; i++) {
        int s = SIZES[i], k = s / 16 < 1 ? 1 : s / 16;
        int ox = (s - 16 * k) / 2, oy = (s - 10 * k) / 2;
        uint32_t *img = calloc((size_t)s * s, 4);
        for (int y = 0; y < 10 * k; y++)
            for (int x = 0; x < 16 * k; x++)
                img[(oy + y) * s + ox + x] = lp[(y / k) * 16 + x / k];

        put32(f, 40);
        put32(f, s);
        put32(f, s * 2);   // XOR + AND mask
        put16(f, 1);
        put16(f, 32);
        for (int j = 0; j < 6; j++) put32(f, 0);
        for (int y = s - 1; y >= 0; y--)   // bottom-up
            fwrite(img + y * s, 4, s, f);
        for (int j = 0; j < (s + 31) / 32 * 4 * s; j++) fputc(0, f);
        free(img);
    }
    fclose(f);
    return 0;
}
