#include "render.h"
#include <string.h>

/*
 * Clawd as drawn in the Claude Code banner, with the terminal's
 * 1:2 quadrant pixels doubled vertically into square pixels:
 *
 *   x: 0123456789abcdefgh
 *   0  ...############...
 *   1  ...############...
 *   2  ...##E######E##...      E = eye
 *   3  ...##E######E##...
 *   4  .################.      arms
 *   5  .################.
 *   6  ...############...
 *   7  ...############...
 *   8  ....#.#....#.#....      legs
 *   9  ....#.#....#.#....
 */

static const int LEG_X[4] = { 4, 6, 11, 13 };

static void put(Canvas *c, int x, int y, uint32_t col)
{
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
    c->px[y * c->w + x] = col;
}

static void rect(Canvas *c, int x0, int y0, int x1, int y1, uint32_t col)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) put(c, x, y, col);
}

void cv_clear(Canvas *c)
{
    memset(c->px, 0, sizeof(uint32_t) * c->w * c->h);
}

void draw_clawd(Canvas *c, int ox, int oy, const Pose *p)
{
    int dy = p->body_dy;
    int top = oy + dy + (p->squash > 0) - (p->squash < 0);
    int bot = oy + 7 + dy;
    // Sideways stretch pulls one side (its arm and legs too) further out.
    int sl = p->stretch_x < 0 ? p->stretch_x : 0, sr = p->stretch_x > 0 ? p->stretch_x : 0;
    rect(c, ox + 3 + sl, top, ox + 14 + sr, bot, COL_BODY);

    if (p->prop == PROP_KEYBOARD) {
        int ky = oy + 6 + dy;
        rect(c, ox, ky, ox + 17, ky + 1, COL_KB);
        for (int x = ox + 1; x <= ox + 16; x += 2) put(c, x, ky, COL_KEY);
    }

    int ay = oy + 4 + dy;
    rect(c, ox + 1 + sl, ay + p->arm_l, ox + 2 + sl, ay + p->arm_l + 1, COL_BODY);
    rect(c, ox + 15 + sr, ay + p->arm_r, ox + 16 + sr, ay + p->arm_r + 1, COL_BODY);

    for (int i = 0; i < 4; i++) {
        int lx = ox + LEG_X[i] + (i < 2 ? sl : sr);
        if (p->legs[i] > 0) rect(c, lx, bot + 1, lx, bot + p->legs[i], COL_BODY);
    }

    int ey = top + 2;
    int ex[2] = { ox + 5 + p->look, ox + 12 + p->look };
    for (int i = 0; i < 2; i++) {
        int x = ex[i], out = i ? 1 : -1;   // side facing away from the centre
        switch (p->eyes) {
        case EYE_OPEN:
            rect(c, x, ey, x, ey + 1, COL_EYE);
            break;
        case EYE_CLOSED:
            rect(c, i ? x : x - 1, ey + 1, i ? x + 1 : x, ey + 1, COL_EYE);
            break;
        case EYE_HAPPY:
            put(c, x, ey, COL_EYE);
            put(c, x - 1, ey + 1, COL_EYE);
            put(c, x + 1, ey + 1, COL_EYE);
            break;
        case EYE_WIDE:
            rect(c, out < 0 ? x - 1 : x, ey, out < 0 ? x : x + 1, ey + 1, COL_EYE);
            break;
        }
    }
}

typedef struct { int w, h; const char *rows[5]; } Glyph;

static const Glyph GLYPHS[G_COUNT] = {
    [G_Z]     = { 4, 4, { "####", "..#.", ".#..", "####" } },
    [G_ZBIG]  = { 5, 5, { "#####", "...#.", "..#..", ".#...", "#####" } },
    [G_EXCL]  = { 1, 5, { "#", "#", "#", ".", "#" } },
    [G_QUEST] = { 3, 5, { "###", "..#", ".##", "...", ".#." } },
    [G_HEART] = { 5, 5, { ".#.#.", "#####", "#####", ".###.", "..#.." } },
    [G_DROP]  = { 3, 4, { ".#.", "###", "###", ".#." } },
};

int glyph_width(int g)
{
    return GLYPHS[g].w;
}

void draw_glyph(Canvas *c, int x, int y, int g, uint32_t rgb, int alpha)
{
    const Glyph *gl = &GLYPHS[g];
    uint32_t a = (uint32_t)alpha << 24;
    uint32_t outline = a | 0x202020u, fill = a | (rgb & 0xFFFFFFu);

    for (int pass = 0; pass < 2; pass++)
        for (int j = 0; j < gl->h; j++)
            for (int i = 0; i < gl->w; i++) {
                if (gl->rows[j][i] != '#') continue;
                if (pass == 0)
                    rect(c, x + i - 1, y + j - 1, x + i + 1, y + j + 1, outline);
                else
                    put(c, x + i, y + j, fill);
            }
}

void cv_scale_premul(const Canvas *c, uint32_t *dst, int k)
{
    int dw = c->w * k;
    for (int y = 0; y < c->h; y++) {
        uint32_t *row = dst + (size_t)y * k * dw;
        for (int x = 0; x < c->w; x++) {
            uint32_t s = c->px[y * c->w + x], a = s >> 24;
            uint32_t r = ((s >> 16) & 255) * a / 255;
            uint32_t g = ((s >> 8) & 255) * a / 255;
            uint32_t b = (s & 255) * a / 255;
            uint32_t d = (a << 24) | (r << 16) | (g << 8) | b;
            for (int i = 0; i < k; i++) row[x * k + i] = d;
        }
        for (int j = 1; j < k; j++) memcpy(row + (size_t)j * dw, row, (size_t)dw * 4);
    }
}
