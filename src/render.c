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
        case EYE_DIZZY:   // x x
            for (int d = -1; d <= 1; d += 2) {
                put(c, x + d, ey, COL_EYE);
                put(c, x + d, ey + 2, COL_EYE);
            }
            put(c, x, ey + 1, COL_EYE);
            break;
        }
    }
}

/*
 * Red brick laid in courses three rows deep (two of brick, one of mortar)
 * along the length of the wall, each brick eight long with the joints
 * staggered by half a brick, inside a dark outline.
 */
void draw_wall(Canvas *c)
{
    static const uint32_t shade[3] = { 0xFFB5482Fu, 0xFFA23E28u, 0xFFC0573Au };
    int along_x = c->w >= c->h;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            uint32_t col;
            if (x == 0 || y == 0 || x == c->w - 1 || y == c->h - 1) {
                col = 0xFF3E1A12u;
            } else {
                int a = (along_x ? x : y) - 1, b = (along_x ? y : x) - 1;   // along / across the courses
                int course = b / 3, row = b % 3, ia = a + (course % 2) * 4;
                if (row == 2 || ia % 8 == 7)
                    col = 0xFFD8CCB8u;   // mortar
                else if (row == 0)
                    col = 0xFFD06A4Au;   // lit edge of a brick
                else
                    col = shade[(unsigned)(course * 7 + ia / 8 * 13) % 3];
            }
            c->px[y * c->w + x] = col;
        }
}

/*
 * Desk fan, front view: a round cage with spinning blades on a stand.
 *   c = cage, b = blade, h = hub, s = stand
 */
static const char *const FAN[2][FAN_H] = {
    { "..ccc..", ".c...c.", "c..b..c", "c.bhb.c", "c..b..c", ".c...c.", "..ccc..", "...s...", "...s...", ".sssss." },
    { "..ccc..", ".c...c.", "c.b.b.c", "c..h..c", "c.b.b.c", ".c...c.", "..ccc..", "...s...", "...s...", ".sssss." },
};

void draw_fan(Canvas *c, int x, int y, int frame)
{
    for (int j = 0; j < FAN_H; j++)
        for (int i = 0; i < FAN_W; i++) {
            uint32_t col = 0;
            switch (FAN[frame & 1][j][i]) {
            case 'c': col = 0xFF4A5560u; break;
            case 'b': col = 0xFFBFE3FFu; break;
            case 'h': col = 0xFF2B2B2Bu; break;
            case 's': col = 0xFF6B6B6Bu; break;
            }
            if (col) put(c, x + i, y + j, col);
        }
}

// '#' takes the colour passed to draw_glyph; other letters are fixed colours
// (see glyph_color). Flat glyphs get no dark outline.
typedef struct { int w, h, flat; const char *rows[9]; } Glyph;

static const Glyph GLYPHS[G_COUNT] = {
    [G_Z]     = { 4, 4, 0, { "####", "..#.", ".#..", "####" } },
    [G_ZBIG]  = { 5, 5, 0, { "#####", "...#.", "..#..", ".#...", "#####" } },
    [G_EXCL]  = { 1, 5, 0, { "#", "#", "#", ".", "#" } },
    [G_QUEST] = { 3, 5, 0, { "###", "..#", ".##", "...", ".#." } },
    [G_HEART] = { 5, 5, 0, { ".#.#.", "#####", "#####", ".###.", "..#.." } },
    [G_DROP]  = { 3, 4, 0, { ".#.", "###", "###", ".#." } },
    [G_TEAR]  = { 1, 1, 1, { "#" } },
    [G_WIND]  = { 3, 1, 1, { "###" } },
    [G_STAR]  = { 3, 3, 0, { ".#.", "###", ".#." } },
    [G_NOTE]  = { 5, 6, 0, { "..##.", "..#.#", "..#..", "..#..", "###..", "###.." } },
    [G_SMILE] = { 7, 7, 0, {
        ".YYYYY.",
        "YYYYYYY",
        "YKYYYKY",
        "YYYYYYY",
        "YKYYYKY",
        "YYKKKYY",
        ".YYYYY.",
    } },
    [G_COFFEE] = { 7, 7, 0, {
        ".#..#..",
        "..#..#.",
        "WWWWW..",
        "WCCCWWW",
        "WCCCW.W",
        "WCCCWWW",
        ".WWW...",
    } },
    [G_BULB]  = { 5, 7, 0, {
        ".YYY.",
        "YYWYY",
        "YYYYY",
        "YYYYY",
        ".YYY.",
        ".GGG.",
        "..G..",
    } },
    [G_BOLT]  = { 5, 7, 0, { "...##", "..##.", ".##..", "#####", "..##.", ".##..", "##..." } },
    [G_SOB]   = { 9, 9, 0, {   // loudly crying face
        "..YYYYY..",
        ".YYYYYYY.",
        "YYYYYYYYY",
        "YKKYYYKKY",
        "YBYYYYYBY",
        "YBYKKKYBY",
        "YBYKRKYBY",
        ".BYKKKYB.",
        "..BYYYB..",
    } },
};

static uint32_t glyph_color(char ch, uint32_t fill)
{
    switch (ch) {
    case '#': return fill;
    case 'Y': return 0xFFC93C;   // face
    case 'K': return 0x3A2A1A;   // eyes, mouth
    case 'R': return 0xB0303A;   // inside of the mouth
    case 'B': return 0x4FA8FF;   // tears
    case 'W': return 0xF4F4F4;   // cup
    case 'C': return 0x6B3E26;   // coffee
    case 'G': return 0x8A8A8A;   // lamp base
    }
    return 0;
}

int glyph_width(int g)
{
    return GLYPHS[g].w;
}

int glyph_height(int g)
{
    return GLYPHS[g].h;
}

void draw_glyph(Canvas *c, int x, int y, int g, uint32_t rgb, int alpha)
{
    const Glyph *gl = &GLYPHS[g];
    uint32_t a = (uint32_t)alpha << 24;
    uint32_t outline = a | 0x202020u;

    for (int pass = gl->flat; pass < 2; pass++)
        for (int j = 0; j < gl->h; j++)
            for (int i = 0; i < gl->w; i++) {
                char ch = gl->rows[j][i];
                if (ch == '.') continue;
                if (pass == 0)
                    rect(c, x + i - 1, y + j - 1, x + i + 1, y + j + 1, outline);
                else
                    put(c, x + i, y + j, a | glyph_color(ch, rgb & 0xFFFFFFu));
            }
}

void draw_speech(Canvas *c, int x, int y, int g, uint32_t rgb, int alpha)
{
    const Glyph *gl = &GLYPHS[g];
    uint32_t a = (uint32_t)alpha << 24, ink = a | 0x202020u, paper = a | 0xFFFFFFu;
    int x0 = x - 2, y0 = y - 2, x1 = x + gl->w + 1, y1 = y + gl->h + 1, mid = (x0 + x1) / 2;
    rect(c, x0 + 1, y0, x1 - 1, y1, ink);   // outline, corners cut
    rect(c, x0, y0 + 1, x1, y1 - 1, ink);
    rect(c, x0 + 1, y0 + 1, x1 - 1, y1 - 1, paper);
    rect(c, mid - 1, y1, mid, y1, paper);   // the tail
    put(c, mid - 2, y1 + 1, ink);
    rect(c, mid - 1, y1 + 1, mid - 1, y1 + 1, paper);
    put(c, mid, y1 + 1, ink);
    put(c, mid - 1, y1 + 2, ink);
    for (int j = 0; j < gl->h; j++)
        for (int i = 0; i < gl->w; i++) {
            char ch = gl->rows[j][i];
            if (ch != '.') put(c, x + i, y + j, a | glyph_color(ch, rgb & 0xFFFFFFu));
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
