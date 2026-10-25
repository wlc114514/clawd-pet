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

static void draw_wear(Canvas *c, int ox, int oy, const Pose *p, int layer);

/*
 * A school desk standing on the floor in front of the sprite (light wooden
 * top, green metal frame), and on it what the pet is busy with: a book held
 * up with its cover to us, an exercise book and a pencil, or a laptop with
 * its lid to us. The pet sits up behind it (DESK_DY), so its eyes show over
 * the book.
 */
#define DESK_TOP   0xFFD9A86Cu
#define DESK_EDGE  0xFFB07D45u
#define DESK_FRAME 0xFF5E7F6Bu
#define DESK_HOLE  0xFF3A2E26u
#define PAPER      0xFFF4F1E8u

void draw_desk(Canvas *c, int ox, int oy, int prop, int frame)
{
    int ty = oy + 4, ground = oy + FOOT_Y - SPR_OY, x0 = ox - 3, x1 = ox + 20;
    rect(c, x0, ty, x1, ty, DESK_TOP);
    rect(c, x0, ty + 1, x1, ty + 1, DESK_EDGE);
    rect(c, x0 + 2, ty + 2, x1 - 2, ty + 3, DESK_FRAME);   // the shelf under the top
    rect(c, x0 + 3, ty + 2, x1 - 3, ty + 2, DESK_HOLE);
    rect(c, x0 + 1, ty + 2, x0 + 1, ground - 1, DESK_FRAME);
    rect(c, x1 - 1, ty + 2, x1 - 1, ground - 1, DESK_FRAME);

    int bx = ox + 4, by = ty - 4;
    switch (prop) {
    case PROP_BOOK:
        rect(c, bx, by, bx + 9, by + 2, 0xFF3D6FB0u);
        rect(c, bx + 4, by, bx + 5, by + 2, 0xFF2A4F86u);   // spine
        rect(c, bx + 1, by + 1, bx + 3, by + 1, 0xFFE8C860u);   // title
        rect(c, bx + 6, by + 1, bx + 8, by + 1, 0xFFE8C860u);
        rect(c, bx, by + 3, bx + 9, by + 3, PAPER);   // page edges
        if (frame) rect(c, bx + 5, by - 1, bx + 7, by - 1, PAPER);   // a page turning over
        break;
    case PROP_HOMEWORK:   // the pencil is in the right hand, its tip on the paper (frame 0) or lifted
        rect(c, ox + 4, ty - 1, ox + 14, ty - 1, PAPER);
        rect(c, ox + 14, ty - 4 - frame, ox + 14, ty - 2 - frame, 0xFFE8B730u);
        put(c, ox + 14, ty - 1 - frame, 0xFF333333u);
        break;
    case PROP_LAPTOP:
        rect(c, bx, by, bx + 9, by + 2, 0xFFB8BCC2u);
        rect(c, bx + 4, by + 1, bx + 5, by + 1, 0xFFE8ECF0u);   // logo
        rect(c, bx - 1, ty - 1, bx + 10, ty - 1, 0xFF8C9096u);
        break;
    }
}

/*
 * The cut-out: the sprite's outline (body, and arms resting on the desk)
 * traced in dark crayon with rounded corners, coloured in with orange
 * strokes that miss a spot here and there, dot eyes not quite level,
 * leaning a little, with the white edge of the paper all round it and a
 * bit of tape at the top.
 */
#define CRAYON_LINE  0xFF3B302Au
#define CRAYON_LINE2 0xFF5E4C40u
#define CRAYON_FILL  0xFFE3804Fu
#define CRAYON_FILL2 0xFFF0A47Au
#define PAPER_EDGE   0xFFFBF8F0u
#define TAPE         0xFFE6DCC0u

void draw_paper(Canvas *c, int ox, int oy)
{
    enum { W = 24, H = 14, M = 3 };   // the sprite, M px of margin all round
    unsigned char m[H][W] = { 0 }, t[H][W] = { 0 };   // 1 colour, 2 line, 3 paper
    for (int y = 0; y < 8; y++)
        for (int x = 3; x <= 14; x++) m[y + M][x + M] = 1;
    for (int y = 5; y <= 6; y++)
        for (int x = 1; x <= 2; x++) m[y + M][x + M] = m[y + M][x + 14 + M] = 1;
    for (int ring = 2; ring <= 3; ring++) {   // the line round it (side by side only: rounded corners), then the paper
        for (int y = 1; y < H - 1; y++)
            for (int x = 1; x < W - 1; x++) {
                if (m[y][x]) continue;
                int side = 0, any = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int v = m[y + dy][x + dx];
                        if (v && v < ring) {
                            any = 1;
                            side |= !dx || !dy;
                        }
                    }
                if (ring == 2 ? side : any) t[y][x] = (unsigned char)ring;
            }
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (t[y][x]) m[y][x] = t[y][x];
    }
    for (int y = 0; y < H; y++)   // leaning: the top half one over to the right
        for (int x = W - 1; x > 0; x--)
            if (y < M + 4) m[y][x] = m[y][x - 1];

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t col = 0;
            switch (m[y][x]) {
            case 1:
                col = (x * 5 + y * 3) % 17 == 0 ? PAPER_EDGE   // missed a bit
                    : (x + y) % 4 == 0           ? CRAYON_FILL2
                                                 : CRAYON_FILL;
                break;
            case 2: col = (x + 2 * y) % 7 ? CRAYON_LINE : CRAYON_LINE2; break;
            case 3: col = PAPER_EDGE; break;
            }
            if (col) put(c, ox - M + x, oy - M + y, col);
        }
    put(c, ox + 6, oy + 3, COL_EYE);   // the eyes, one a little higher
    put(c, ox + 13, oy + 2, COL_EYE);
    rect(c, ox + 9, oy - 2, ox + 11, oy - 1, TAPE);
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

    for (int i = 0; i < 4; i++) {
        int lx = ox + LEG_X[i] + (i < 2 ? sl : sr);
        if (p->legs[i] > 0) rect(c, lx, bot + 1, lx, bot + p->legs[i], COL_BODY);
    }

    draw_wear(c, ox, oy, p, 0);   // scarf / bow tie: on the body, behind the keyboard

    if (p->prop == PROP_KEYBOARD) {
        int ky = oy + 6 + dy;
        rect(c, ox, ky, ox + 17, ky + 1, COL_KB);
        for (int x = ox + 1; x <= ox + 16; x += 2) put(c, x, ky, COL_KEY);
    }
    if (p->prop >= PROP_DESK) draw_desk(c, ox, oy, p->prop, p->prop_frame);

    int ay = oy + 4 + dy;
    rect(c, ox + 1 + sl, ay + p->arm_l, ox + 2 + sl, ay + p->arm_l + 1, COL_BODY);
    rect(c, ox + 15 + sr, ay + p->arm_r, ox + 16 + sr, ay + p->arm_r + 1, COL_BODY);

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
    draw_wear(c, ox, oy, p, 1);
}

/*
 * Accessories. Hats and moustaches go by the top row of the head (y
 * relative to it; moustaches also turn with the eyes), neckwear by the
 * body's unsquashed top, what it holds by the right hand; glasses are drawn
 * around the eyes in draw_wear(). Letters are colours, see acc_color().
 */
typedef struct { int slot; const char *name; int x, y, h; const char *rows[7]; } Acc;

static const Acc ACCS[ACC_COUNT] = {
    [ACC_BOWLER]  = { SLOT_HEAD, "圆顶礼帽", 2, -5, 5, {
        "....ffffff....",
        "...fhhfffff...",
        "...ffffffff...",
        "f..bbbbbbbb..f",
        ".ffffffffffff.",
    } },
    [ACC_CROWN]   = { SLOT_HEAD, "小皇冠", 5, -4, 4, {
        "Y..YY..Y",
        "YY.YY.YY",
        "YYYYYYYY",
        "YoYrrYoY",
    } },
    [ACC_BEANIE]  = { SLOT_HEAD, "毛线帽", 2, -5, 7, {
        "......ww......",   // the bobble: swings the other way to where it looks
        "....rrrrrr....",
        "..rrrrrrrrrr..",
        ".cccccccccccc.",
        ".rrrrrrrrrrrr.",
        "cCcCcCcCcCcCcC",
        "cCcCcCcCcCcCcC",
    } },
    [ACC_PHONES]  = { SLOT_HEAD, "头戴耳机", 2, -2, 6, {
        "....gggggg....",
        "..gg......gg..",
        ".g..........g.",
        "pp..........pp",
        "pP..........Pp",
        "pp..........pp",
    } },
    [ACC_MONOCLE] = { SLOT_FACE, "单片眼镜" },
    [ACC_GLASSES] = { SLOT_FACE, "圆框眼镜" },
    [ACC_SHADES]  = { SLOT_FACE, "墨镜" },
    [ACC_HANDLEBAR] = { SLOT_MOUTH, "八字胡", 4, 3, 3, {
        "m........m",
        "mm..mm..mm",
        ".mmmmmmmm.",
    } },
    [ACC_PENCIL]  = { SLOT_MOUTH, "一字胡", 6, 4, 1, { "mmmmmm" } },
    [ACC_WALRUS]  = { SLOT_MOUTH, "大浓胡", 4, 4, 3, {
        ".mmmmmmmm.",
        "mmmmmmmmmm",
        "m.mm..mm.m",
    } },
    [ACC_GOATEE]  = { SLOT_MOUTH, "山羊胡", 7, 6, 3, {   // the tip hangs below the chin
        "mmmm",
        ".mm.",
        ".mm.",
    } },
    [ACC_SCARF]   = { SLOT_NECK, "围巾", 3, 4, 6, {   // knitted stripes, the end hanging down the front
        "rrccrrccrrcc",
        "rccrrccrrccr",
        ".rcc........",
        ".ccr........",
        ".rcc........",
        ".r.c........",
    } },
    [ACC_BOWTIE]  = { SLOT_NECK, "蝴蝶结", 6, 5, 3, {
        "rr..rr",
        "rrRRrr",
        "rr..rr",
    } },
    [ACC_COFFEE]  = { SLOT_HAND, "咖啡", 0, -2, 5, {
        "llll",
        "wwww",
        "ssss",
        "ssss",
        ".ww.",
    } },
    [ACC_FRAPPE]  = { SLOT_HAND, "星巴克", 0, -4, 7, {   // iced, in a clear cup with a green straw
        "..e.",
        ".wew",
        "wwww",
        "dddd",
        "deed",
        "dddd",
        ".dd.",
    } },
    [ACC_CHIPS]   = { SLOT_HAND, "薯片", 0, -3, 6, {
        "RrRr",
        "rrrr",
        "rYYr",
        "rYYr",
        "rrrr",
        "RrRr",
    } },
    [ACC_WATCH]   = { SLOT_HAND, "手表", -2, -2, 5, {   // face on the wrist, strap showing above and below
        ".k.",
        "YYY",
        "YwY",
        "YYY",
        ".k.",
    } },
};

static uint32_t acc_color(char ch)
{
    switch (ch) {
    case 'k': return 0xFF2B2B2Bu;   // frames
    case 'f': return 0xFF3D3B40u;   // felt
    case 'h': return 0xFF66636Bu;   // its sheen
    case 'b': return 0xFF9A3F2Eu;   // hat band
    case 'Y': return 0xFFF2C23Au;   // gold
    case 'o': return 0xFFC8902Au;   // darker gold
    case 'r': return 0xFFD94848u;   // red
    case 'R': return 0xFFA83232u;   // dark red
    case 'c': return 0xFFF2EDE4u;   // cream wool
    case 'C': return 0xFFD5CBBAu;   // its ribbing
    case 'w': return 0xFFFFFFFFu;   // white
    case 'g': return 0xFF55555Au;   // headband
    case 'p': return 0xFF35B5A8u;   // ear cups
    case 'P': return 0xFF23857Bu;
    case 'l': return 0xFFE4E4E4u;   // lid
    case 's': return 0xFFB07A4Au;   // cardboard sleeve
    case 'm': return 0xFF3B2A20u;   // moustache
    case 'e': return 0xFF00704Au;   // green: straw, logo
    case 'd': return 0xFFD9B48Au;   // iced coffee
    }
    return 0;
}

int acc_slot(int a)
{
    return ACCS[a].slot;
}

const char *acc_name(int a)
{
    return a > ACC_NONE && a < ACC_COUNT ? ACCS[a].name : "";
}

const char *acc_slot_name(int s)
{
    static const char *const N[SLOTS] = { "头", "脸", "嘴", "脖子", "手" };
    return N[s];
}

int acc_above(const unsigned char *wear)
{
    int a = wear[SLOT_HEAD];
    return a ? -ACCS[a].y : 0;
}

void outfit_roll(unsigned char *wear, unsigned *rng)
{
    static const int chance[SLOTS] = { 45, 30, 20, 30, 30 };   // percent
    for (int s = 0; s < SLOTS; s++) {
        int pick[ACC_COUNT], n = 0;
        for (int a = 1; a < ACC_COUNT; a++)
            if (ACCS[a].slot == s) pick[n++] = a;
        unsigned r[2];
        for (int i = 0; i < 2; i++) {
            *rng ^= *rng << 13;
            *rng ^= *rng >> 17;
            *rng ^= *rng << 5;
            r[i] = *rng;
        }
        wear[s] = r[0] % 100 < (unsigned)chance[s] ? (unsigned char)pick[r[1] % (unsigned)n] : ACC_NONE;
    }
}

// rows of ACCS[a] with their top-left at (x, y); `bob` shifts the first row.
static void draw_art(Canvas *c, int a, int x, int y, int bob, int floor_y)
{
    const Acc *ac = &ACCS[a];
    for (int j = 0; j < ac->h && y + j < floor_y; j++)
        for (int i = 0; ac->rows[j][i]; i++) {
            uint32_t col = acc_color(ac->rows[j][i]);
            if (col) put(c, x + i + (j ? 0 : bob), y + j, col);
        }
}

// One lens around the eye at (x, ey): a ring, or a dark shade.
static void lens(Canvas *c, int x, int ey, uint32_t col, int filled)
{
    if (filled) {
        rect(c, x - 2, ey, x + 2, ey, col);
        rect(c, x - 1, ey + 1, x + 1, ey + 1, col);
        put(c, x - 1, ey, 0xFF5A5A5Au);   // glint
        return;
    }
    rect(c, x - 1, ey - 1, x + 1, ey - 1, col);
    rect(c, x - 1, ey + 2, x + 1, ey + 2, col);
    rect(c, x - 2, ey, x - 2, ey + 1, col);
    rect(c, x + 2, ey, x + 2, ey + 1, col);
}

// Layer 0 is neckwear, worn on the body under any keyboard and the hands;
// layer 1 is everything else, on top.
static void draw_wear(Canvas *c, int ox, int oy, const Pose *p, int layer)
{
    int dy = p->body_dy, base = oy + dy;
    int top = base + (p->squash > 0) - (p->squash < 0);
    int sr = p->stretch_x > 0 ? p->stretch_x : 0;
    int floor_y = oy + 10;   // nothing hangs below the feet
    int a;

    if (layer == 0) {   // what the keyboard goes in front of
        if ((a = p->wear[SLOT_NECK])) draw_art(c, a, ox + ACCS[a].x, base + ACCS[a].y, 0, floor_y);
        if ((a = p->wear[SLOT_MOUTH])) draw_art(c, a, ox + ACCS[a].x + p->look, top + ACCS[a].y, 0, floor_y);
        return;
    }

    int ey = top + 2, ex[2] = { ox + 5 + p->look, ox + 12 + p->look };
    switch (p->wear[SLOT_FACE]) {
    case ACC_GLASSES:
        lens(c, ex[0], ey, 0xFF2B2B2Bu, 0);
        lens(c, ex[1], ey, 0xFF2B2B2Bu, 0);
        rect(c, ex[0] + 3, ey, ex[1] - 3, ey, 0xFF2B2B2Bu);   // bridge
        break;
    case ACC_MONOCLE:
        lens(c, ex[1], ey, 0xFFF2C23Au, 0);
        put(c, ex[1] + 2, ey + 3, 0xFFC8902Au);   // chain
        put(c, ex[1] + 2, ey + 5, 0xFFC8902Au);
        break;
    case ACC_SHADES:
        rect(c, ex[0] - 2, ey - 1, ex[1] + 2, ey - 1, 0xFF1E1E1Eu);
        lens(c, ex[0], ey, 0xFF1E1E1Eu, 1);
        lens(c, ex[1], ey, 0xFF1E1E1Eu, 1);
        break;
    }

    if ((a = p->wear[SLOT_HEAD])) draw_art(c, a, ox + ACCS[a].x, top + ACCS[a].y, a == ACC_BEANIE ? -p->look : 0, floor_y);

    if ((a = p->wear[SLOT_HAND])) {
        int hx = ox + 17 + sr, hy = oy + 4 + dy + p->arm_r;
        draw_art(c, a, hx + ACCS[a].x, hy + ACCS[a].y, 0, floor_y);
        if (a == ACC_COFFEE) {   // a wisp of steam
            int f = (p->tick / 8) % 2;
            put(c, hx + 1 + f, hy - 3, 0xA0D8D8D8u);
            put(c, hx + 2 - f, hy - 4, 0x80D8D8D8u);
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

void draw_outfit(Canvas *c, int ox, int oy, const Pose *p)
{
    draw_wear(c, ox, oy, p, 0);
    draw_wear(c, ox, oy, p, 1);
}
