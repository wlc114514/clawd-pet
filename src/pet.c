#include "pet.h"
#include <math.h>
#include <string.h>

#define SLEEP_AFTER_MS (5 * 60 * 1000)

// How long each send-away reaction lasts before the taskbar gives way.
static const int LEAVE_TICKS[LEAVE_KINDS] = {
    [LEAVE_SHOCK] = 24, [LEAVE_SHIVER] = 45, [LEAVE_CRY] = 55, [LEAVE_PANIC] = 70,
};

#define C_HEART 0xFF5A78u
#define C_EXCL  0xFFD23Fu
#define C_WHITE 0xFFFFFFu
#define C_DROP  0x5AB4FFu
#define C_TEAR  0x4FA8FFu

#define C_STONE   0xFF9C9A94u
#define C_STONE_L 0xFFC4C2BBu
#define C_MORTAR  0xFF7A7872u
#define C_OUTLINE 0xFF3E3C38u

#define HOP_TICKS 7

// ST_BUMP timeline, in ticks: splat against the wall, stagger back, land, see stars.
#define BUMP_HOP  4
#define BUMP_LAND 12
#define BUMP_DAZE 15
#define BUMP_END  52

// Pets together.
#define GREET_TICKS 30
#define CHAT_TURN   40      // ticks per thing said
#define SOCIAL_MS   45000   // between one game and the next, per pet
#define RIDE_HOP    10
#define C_NOTE      0x9B7BFFu

// The pet's solid box, in canvas columns: arm tip to arm tip. It runs from
// the top of the head (SPR_OY) down to the feet (FOOT_Y).
#define BOX_L (SPR_OX + 1)
#define BOX_R (SPR_OX + 17)

/* ---- helpers ---------------------------------------------------------- */

static unsigned rnd(Pet *p)
{
    p->rng ^= p->rng << 13;
    p->rng ^= p->rng >> 17;
    p->rng ^= p->rng << 5;
    return p->rng;
}

static int rr(Pet *p, int lo, int hi)
{
    return lo + (int)(rnd(p) % (unsigned)(hi - lo + 1));
}

// Physics constants are tuned for scale 4 and scaled from there.
static double unit(const Pet *p) { return p->scale / 4.0; }
static double mid_x(const Pet *p) { return p->x + (SPR_OX + 9) * p->scale; }
static double feet_y(const Pet *p) { return p->y + FOOT_Y * p->scale; }
/* ---- walls ------------------------------------------------------------ */

static RECT g_walls[MAX_WALLS];
static int g_nwalls;

void pet_set_walls(const RECT *walls, int n)
{
    if (n < 0) n = 0;
    if (n > MAX_WALLS) n = MAX_WALLS;
    if (n) memcpy(g_walls, walls, sizeof *walls * (size_t)n);
    g_nwalls = n;
}

// Screen y of what a pet at x stands on, looking down from feet height
// `from`: the taskbar, the floor it climbed up to, or the top of a wall
// under its legs, whichever comes first.
static double floor_at(const Pet *p, double x, double from)
{
    double f = p->area.bottom - p->perch;
    double l = x + (SPR_OX + 4) * p->scale, r = x + (SPR_OX + 14) * p->scale;
    for (int i = 0; i < g_nwalls; i++) {
        const RECT *w = &g_walls[i];
        if (l < w->right && r > w->left && w->top >= from - 1 && w->top < f) f = w->top;
    }
    return f;
}

static double ground_y(const Pet *p)
{
    return floor_at(p, p->x, p->y + FOOT_Y * p->scale) - FOOT_Y * p->scale;
}

// How far the pet can move sideways by dx before a wall stops it; *hit is
// the side of the wall it ran into (-1 / +1), or 0.
static double wall_dx(const Pet *p, double dx, int *hit)
{
    double k = p->scale, top = p->y + SPR_OY * k, bot = p->y + FOOT_Y * k;
    double l = p->x + BOX_L * k, r = p->x + BOX_R * k;
    *hit = 0;
    for (int i = 0; i < g_nwalls; i++) {
        const RECT *w = &g_walls[i];
        if (bot <= w->top + 0.5 || top >= w->bottom - 0.5) continue;   // passes over or under it
        if (dx > 0 && r <= w->left + 0.5 && r + dx > w->left) {
            dx = w->left - r;
            *hit = 1;
        }
        if (dx < 0 && l >= w->right - 0.5 && l + dx < w->right) {
            dx = w->right - l;
            *hit = -1;
        }
    }
    return dx;
}

static int move_x(Pet *p, double dx)
{
    int hit;
    p->x += wall_dx(p, dx, &hit);
    return hit;
}

// A wall within gap px in direction dir.
static int walled(const Pet *p, int dir, double gap)
{
    int hit;
    wall_dx(p, dir * gap, &hit);
    return hit;
}

// Rising: stops the head under a wall it would go through. Returns 1 if it did.
static int bump_head(Pet *p, double head_was)
{
    double k = p->scale, l = p->x + BOX_L * k, r = p->x + BOX_R * k;
    for (int i = 0; i < g_nwalls; i++) {
        const RECT *w = &g_walls[i];
        if (l < w->right - 0.5 && r > w->left + 0.5 && head_was >= w->bottom - 0.5 &&
            p->y + SPR_OY * k < w->bottom) {
            p->y = w->bottom - SPR_OY * k;
            return 1;
        }
    }
    return 0;
}
static double min_x(const Pet *p) { return p->area.left - (SPR_OX + 1) * p->scale; }
static double max_x(const Pet *p) { return p->area.right - (SPR_OX + 17) * p->scale; }

static DWORD idle_ms(void)
{
    return plat_idle_ms();
}

static void refresh_area(Pet *p)
{
    POINT feet = { (LONG)p->x + CW * p->scale / 2, (LONG)p->y + FOOT_Y * p->scale - 1 };
    plat_screen_rects(feet, &p->area, &p->screen);
}

static void set_state(Pet *p, int st, int dur)
{
    p->state = st;
    p->t = 0;
    p->dur = dur;
}

// States the pet is free to leave for something more interesting.
static int is_free(int st)
{
    return st == ST_IDLE || st == ST_WALK || st == ST_TYPE || st == ST_WAVE || st == ST_FLEE;
}

static int is_social(int st)
{
    return st >= ST_GREET;
}

static void clamp_walls(Pet *p, double bounce)
{
    if (p->x < min_x(p)) { p->x = min_x(p); p->vx = -p->vx * bounce; }
    if (p->x > max_x(p)) { p->x = max_x(p); p->vx = -p->vx * bounce; }
}

static Bubble *new_bubble(Pet *p)
{
    for (int i = 0; i < MAX_BUBBLES; i++)
        if (p->bub[i].life <= 0) return &p->bub[i];
    return NULL;
}

static void say(Pet *p, int glyph, uint32_t color)
{
    Bubble *b = new_bubble(p);
    if (b) *b = (Bubble){ glyph, 45, 45, SPR_OX + 9 - glyph_width(glyph) / 2, SPR_OY - 8, 0, -0.08, color };
}

static void spawn_z(Pet *p)
{
    Bubble *b = new_bubble(p);
    int g = (p->zcount++ % 2) ? G_ZBIG : G_Z;
    if (b) *b = (Bubble){ g, 75, 75, SPR_OX + 13, SPR_OY - 3, 0.05, -0.11, C_WHITE };
}

static void spawn_drop(Pet *p)
{
    Bubble *b = new_bubble(p);
    int side = rr(p, 0, 1);
    if (b) *b = (Bubble){ G_DROP, 24, 24, side ? SPR_OX + 16 : SPR_OX - 1, SPR_OY - 1, side ? 0.05 : -0.05, 0.08, C_DROP };
}

// A gust streaking from the fan across the pet.
static void spawn_wind(Pet *p)
{
    Bubble *b = new_bubble(p);
    double x = p->fan_side > 0 ? CW - FAN_W - 3 : FAN_W;
    if (b) *b = (Bubble){ G_WIND, 11, 11, x, SPR_OY + rr(p, 1, 6), -p->fan_side * 0.9, 0, 0xCFE8FFu };
}

// Stars flying off where it hit a wall: side -1 / +1, or 0 for overhead.
static void spawn_bonk(Pet *p, int side)
{
    double x = side > 0 ? SPR_OX + 17 : side < 0 ? SPR_OX - 2 : SPR_OX + 8;
    double y = side ? SPR_OY + 1 : SPR_OY - 4;
    Bubble *b;
    if ((b = new_bubble(p))) *b = (Bubble){ G_STAR, 10, 10, x, y, side * 0.2 - 0.25 * !side, -0.35, C_EXCL };
    if ((b = new_bubble(p))) *b = (Bubble){ G_STAR, 8, 8, x, y + 3, side * 0.3 + 0.25 * !side, 0.1, C_EXCL };
}

// Stars circling its head while it is dazed.
static void spawn_orbit(Pet *p)
{
    p->count++;
    for (int i = 0; i < 2; i++) {
        double a = p->count * 0.35 + i * 3.14159265;
        Bubble *b = new_bubble(p);
        if (b) *b = (Bubble){ G_STAR, 2, 2, SPR_OX + 8 + 7 * cos(a), SPR_OY - 5 + 2 * sin(a), 0, 0, C_EXCL };
    }
}

// Something said in pixel emoji, in a speech bubble over its head.
static void spawn_chat(Pet *p)
{
    static const int pick[] = { G_HEART, G_NOTE, G_SMILE, G_COFFEE, G_BULB, G_BOLT, G_QUEST, G_EXCL };
    static const uint32_t col[] = { C_HEART, C_NOTE, 0, 0xB8B8B8u, 0, 0xF2A007u, 0x3A3A3Au, 0xE0533Au };
    int i = rr(p, 0, 7), g = pick[i], w = glyph_width(g), h = glyph_height(g);
    Bubble *b = new_bubble(p);
    int y = SPR_OY - 4 - h - acc_above(p->wear);
    if (y < 2) y = 2;   // as high as it goes; the bubble covers the top of a tall hat
    if (b) *b = (Bubble){ g, 34, 34, SPR_OX + 9 - w / 2, y, 0, 0, col[i], 0, 1 };
}

static void spawn_sob(Pet *p, int life)
{
    Bubble *b = new_bubble(p);
    if (b) *b = (Bubble){ G_SOB, life, life, SPR_OX + 5, SPR_OY - 11, 0, -0.02, 0 };
}

// A tear arcing out sideways from each (closed) eye of pose q.
static void spawn_tears(Pet *p, const Pose *q)
{
    double y = SPR_OY + q->body_dy + 3;
    Bubble *b;
    if ((b = new_bubble(p))) *b = (Bubble){ G_TEAR, 15, 15, SPR_OX + 4, y, -0.35, -0.3, C_TEAR, 0.05 };
    if ((b = new_bubble(p))) *b = (Bubble){ G_TEAR, 15, 15, SPR_OX + 13, y, 0.35, -0.3, C_TEAR, 0.05 };
}

/* ---- stairs ----------------------------------------------------------- */

// Screen x of the middle of step i, and y of its top surface.
static double step_cx(const Pet *p, int i)
{
    const Stairs *s = &p->stairs;
    return s->x0 + s->dir * (i * STEP_W + STEP_W / 2.0) * s->scale;
}

static double step_top(const Pet *p, int i)
{
    return p->stairs.base - (i + 1) * STEP_H * p->stairs.scale;
}

// How many steps fit between the pet and the edge of the screen in
// direction dir (the top step's middle ends up n * STEP_W from the pet's),
// no taller than half the screen, and not so high (counting the floor it
// is already perched on) that its head and bubbles would leave the screen.
static int stairs_room(const Pet *p, int dir)
{
    double k = p->scale, c = p->x + (SPR_OX + 9) * k, floor = p->y + FOOT_Y * k;
    double room = dir > 0 ? p->area.right - 8 * k - c : c - (p->area.left + 8 * k);
    int n = room > 0 ? (int)(room / (STEP_W * k)) : 0;
    double rise = (p->area.bottom - p->area.top) / 2.0;
    double headroom = floor - p->area.top - (FOOT_Y + 10) * k;
    if (rise > headroom) rise = headroom;
    int tall = rise > 0 ? (int)(rise / (STEP_H * k)) : 0;
    if (n > tall) n = tall;
    if (n > MAX_STEPS) n = MAX_STEPS;

    // Stop short of any wall: no step, nor the pet hopping onto it, may go
    // through one.
    for (int i = 0; i < n; i++) {
        double sx = c + dir * (5 + i * STEP_W + STEP_W / 2.0) * k, top = floor - (i + 1) * STEP_H * k;
        double l = sx - 9 * k, r = sx + 9 * k, t = top - (FOOT_Y - SPR_OY + 5) * k;
        for (int j = 0; j < g_nwalls; j++) {
            const RECT *w = &g_walls[j];
            if (l < w->right && r > w->left && t < w->bottom && floor - 0.5 > w->top) return i;
        }
    }
    return n;
}

static void stairs_crumble(Pet *p)
{
    if (p->stairs.n && p->stairs.crumble_t < 0) p->stairs.crumble_t = 0;
}

// Steps rise out of the taskbar; when crumbling they drop back through it,
// bottom step first.
static void stairs_update(Pet *p)
{
    Stairs *s = &p->stairs;
    if (!s->n) return;
    int gone = 0;
    for (int i = 0; i < s->shown; i++) {
        double depth = (i + 1) * STEP_H;   // this far down it is out of sight
        if (s->crumble_t >= i * 3) {
            s->vy[i] += 0.4;
            s->off[i] += s->vy[i];
        } else if (s->off[i] > 0) {
            s->off[i] -= depth / 4;
            if (s->off[i] < 0) s->off[i] = 0;
        }
        gone += s->off[i] >= depth;
    }
    if (s->crumble_t >= 0 && ++s->crumble_t > 0 && gone == s->shown) s->n = 0;
}

/* ---- poses ------------------------------------------------------------ */

Pose pose_stand(void)
{
    Pose q = { 0 };
    q.eyes = EYE_OPEN;
    for (int i = 0; i < 4; i++) q.legs[i] = 2;
    return q;
}

// 4-phase bouncy walk: on even phases one leg pair pushes the body up.
static Pose pose_walk(int phase, int dir)
{
    Pose q = pose_stand();
    q.look = dir;
    if (phase % 2 == 0) {
        q.body_dy = -1;
        for (int i = 0; i < 4; i++) q.legs[i] = ((i % 2 == 0) == (phase == 0)) ? 3 : 2;
    }
    return q;
}

static Pose pose_dangle(int phase)
{
    Pose q = pose_stand();
    q.eyes = EYE_WIDE;
    q.arm_l = q.arm_r = -3;
    for (int i = 0; i < 4; i++) q.legs[i] = ((i + phase) % 2) ? 3 : 2;
    return q;
}

static Pose pose_air(int rising)
{
    Pose q = pose_stand();
    q.eyes = EYE_WIDE;
    q.arm_l = q.arm_r = -3;
    if (rising) q.squash = -1;
    return q;
}

static Pose pose_hop(int rising)
{
    Pose q = pose_stand();
    q.eyes = EYE_HAPPY;
    q.arm_l = q.arm_r = rising ? -3 : -1;
    if (rising) q.squash = -1;
    return q;
}

static Pose pose_land(int hard)
{
    Pose q = pose_stand();
    q.body_dy = 1;
    q.squash = 1;
    q.eyes = hard ? EYE_CLOSED : EYE_OPEN;
    for (int i = 0; i < 4; i++) q.legs[i] = 1;
    return q;
}

static Pose pose_sleep(int breath)
{
    Pose q = pose_stand();
    q.body_dy = 2;
    q.squash = breath;
    q.eyes = EYE_CLOSED;
    for (int i = 0; i < 4; i++) q.legs[i] = 0;
    return q;
}

static Pose pose_stretch(void)
{
    Pose q = pose_stand();
    q.squash = -1;
    q.eyes = EYE_CLOSED;
    q.arm_l = q.arm_r = -3;
    return q;
}

// Sitting behind a keyboard, hands resting on it; `tap` is -1 / +1 for the
// arm lifted mid-keystroke, 0 for none.
static Pose pose_type(int tap)
{
    Pose q = pose_stand();
    q.body_dy = 1;
    q.prop = PROP_KEYBOARD;
    for (int i = 0; i < 4; i++) q.legs[i] = 1;
    if (tap < 0) q.arm_l = -2;
    if (tap > 0) q.arm_r = -2;
    return q;
}

// Startled: a jolt upward, then arms up and shaking, eyes darting about.
static Pose pose_shock(int t)
{
    Pose q = pose_stand();
    q.eyes = EYE_WIDE;
    q.squash = t < 4 ? -1 : 0;
    q.arm_l = (t / 2) % 2 ? -3 : -2;
    q.arm_r = (t / 2) % 2 ? -2 : -3;
    q.look = t < 6 ? 0 : ((t / 4) % 2 ? 1 : -1);
    return q;
}

// Hunched up small, arms hugged in; the shaking itself is Pet.shake.
static Pose pose_shiver(void)
{
    Pose q = pose_stand();
    q.eyes = EYE_WIDE;
    q.body_dy = 1;
    q.squash = 1;
    q.arm_l = q.arm_r = 1;
    for (int i = 0; i < 4; i++) q.legs[i] = 1;
    return q;
}

// Eyes squeezed shut, arms hanging, heaving with sobs.
static Pose pose_cry(int t)
{
    Pose q = pose_stand();
    q.eyes = EYE_CLOSED;
    q.arm_l = q.arm_r = 1;
    if ((t / 5) % 2) {
        q.body_dy = 1;
        for (int i = 0; i < 4; i++) q.legs[i] = 1;
    }
    return q;
}

// Running flat out, eyes wide, arms flailing.
static Pose pose_panic(int t, int dir)
{
    Pose q = pose_walk((t / 2) % 4, dir);
    q.eyes = EYE_WIDE;
    q.arm_l = (t / 3) % 2 ? -3 : -1;
    q.arm_r = (t / 3) % 2 ? -1 : -3;
    return q;
}

// Tom-style fright: body yanked out long away from the scare (`look` points
// back at it), a bit flatter for it, arms flung up, legs rigid.
static Pose pose_startle(int look)
{
    Pose q = pose_stand();
    q.eyes = EYE_WIDE;
    q.squash = 1;
    q.stretch_x = -3 * look;
    q.arm_l = q.arm_r = -4;
    q.look = look;
    for (int i = 0; i < 4; i++) q.legs[i] = 3;
    return q;
}

// Splat: just ran face first into a wall on `side`.
static Pose pose_splat(int side)
{
    Pose q = pose_stand();
    q.eyes = EYE_CLOSED;
    q.squash = 1;
    q.look = side;
    q.body_dy = 1;
    if (side > 0) q.arm_r = -3;
    else q.arm_l = -3;
    for (int i = 0; i < 4; i++) q.legs[i] = 1;
    return q;
}

// Slumped and seeing stars.
static Pose pose_dazed(int t)
{
    Pose q = pose_stand();
    q.eyes = EYE_DIZZY;
    q.body_dy = 1;
    q.arm_l = q.arm_r = 1;
    q.look = (t / 6) % 2 ? 1 : -1;
    for (int i = 0; i < 4; i++) q.legs[i] = 1;
    return q;
}

static Pose pose_wave(int phase)
{
    Pose q = pose_stand();
    q.eyes = EYE_HAPPY;
    q.arm_r = phase ? -2 : -4;
    return q;
}

int pet_demo_poses(Pose *out, int max)
{
    Pose all[] = {
        pose_stand(), pose_stand(), pose_stand(),
        pose_walk(0, 1), pose_walk(1, 1), pose_walk(2, 1),
        pose_dangle(0), pose_air(1), pose_hop(1), pose_land(0),
        pose_sleep(0), pose_sleep(1), pose_stretch(),
        pose_type(0), pose_type(-1), pose_type(1), pose_wave(0), pose_wave(1),
        pose_shock(0), pose_shock(8), pose_shock(14), pose_startle(-1),
        pose_shiver(), pose_cry(0), pose_cry(5), pose_panic(0, 1), pose_panic(3, 1),
        pose_splat(1), pose_dazed(0),
    };
    all[1].eyes = EYE_CLOSED;
    all[2].look = -1;
    int n = 0;
    for (int i = 0; i < (int)(sizeof all / sizeof all[0]) && n < max; i++) out[n++] = all[i];

    // Then each accessory on its own, and a few outfits in other poses.
    Pose more[] = {
        pose_walk(0, 1), pose_walk(2, -1), pose_type(1), pose_sleep(0), pose_wave(0),
        pose_dazed(0), pose_land(0), pose_stretch(), pose_startle(-1),
    };
    static const unsigned char outfits[][SLOTS + 1] = {   // index into `more`, then the slots
        { 0, ACC_BEANIE, 0, 0, ACC_SCARF, ACC_COFFEE },
        { 1, ACC_BEANIE, ACC_SHADES, ACC_GOATEE, ACC_SCARF, ACC_CHIPS },
        { 2, ACC_PHONES, ACC_GLASSES, ACC_PENCIL, ACC_BOWTIE, ACC_WATCH },
        { 2, ACC_BEANIE, 0, ACC_WALRUS, ACC_SCARF, ACC_FRAPPE },
        { 3, ACC_BOWLER, ACC_MONOCLE, ACC_HANDLEBAR, ACC_BOWTIE, 0 },
        { 4, ACC_CROWN, ACC_SHADES, 0, 0, ACC_FRAPPE },
        { 5, ACC_BOWLER, ACC_GLASSES, ACC_WALRUS, ACC_SCARF, 0 },
        { 6, ACC_CROWN, 0, ACC_HANDLEBAR, ACC_BOWTIE, ACC_COFFEE },
        { 7, ACC_PHONES, ACC_MONOCLE, 0, ACC_SCARF, ACC_CHIPS },
        { 8, ACC_BEANIE, ACC_GLASSES, ACC_PENCIL, ACC_BOWTIE, ACC_WATCH },
    };
    for (int a = 1; a < ACC_COUNT && n < max; a++) {
        out[n] = pose_stand();
        out[n++].wear[acc_slot(a)] = (unsigned char)a;
    }
    for (int i = 0; i < (int)(sizeof outfits / sizeof outfits[0]) && n < max; i++) {
        out[n] = more[outfits[i][0]];
        memcpy(out[n++].wear, outfits[i] + 1, SLOTS);
    }
    return n;
}

/* ---- public ----------------------------------------------------------- */

void pet_init(Pet *p, int scale, unsigned seed)
{
    static int next_id;
    memset(p, 0, sizeof *p);
    p->id = ++next_id;
    p->rng = seed | 1;
    p->scale = scale;
    plat_work_area(&p->area, &p->screen);
    p->x = min_x(p) + rr(p, 0, (int)(max_x(p) - min_x(p)));
    p->y = p->area.bottom - FOOT_Y * p->scale;
    p->dir = 1;
    p->blink_t = rr(p, 30, 90);
    set_state(p, ST_IDLE, 45);
    outfit_roll(p->wear, &p->rng);
    p->pose = pose_stand();
    memcpy(p->pose.wear, p->wear, sizeof p->wear);
}

void pet_dress(Pet *p, const unsigned char *wear)
{
    int added = 0;
    for (int s = 0; s < SLOTS; s++) added |= wear[s] && wear[s] != p->wear[s];
    memcpy(p->wear, wear, sizeof p->wear);
    memcpy(p->pose.wear, wear, sizeof p->wear);
    if (added && (is_free(p->state) || is_social(p->state))) say(p, G_HEART, C_HEART);   // likes it
}

void pet_set_scale(Pet *p, int scale)
{
    if (p->state == ST_CLIMB) {   // the stairs were built for the old size
        stairs_crumble(p);
        p->vx = p->vy = 0;
        set_state(p, ST_FALL, 0);
    }
    // Keep the feet where they are.
    p->x += (CW * p->scale - CW * scale) / 2.0;
    p->y += FOOT_Y * p->scale - FOOT_Y * scale;
    p->scale = scale;
}

POINT pet_head(const Pet *p)
{
    POINT h = { (LONG)(p->x + (SPR_OX + 9) * p->scale),
                (LONG)(p->y + (SPR_OY - 2 - acc_above(p->wear)) * p->scale) };
    return h;
}

void pet_begin_drag(Pet *p, POINT grab)
{
    if (p->state == ST_LEAVE) return;   // no rescuing it mid-fall
    if (p->state == ST_CLIMB) stairs_crumble(p);
    p->perch = 0;                       // let go and it lands on the taskbar
    p->bonked = 0;
    p->drag_dx = grab.x - (int)p->x;
    p->drag_dy = grab.y - (int)p->y;
    p->vx = p->vy = 0;
    p->forced_sleep = 0;
    p->following = 0;
    set_state(p, ST_DRAG, 0);
    say(p, G_EXCL, C_EXCL);
}

void pet_end_drag(Pet *p)
{
    if (p->state != ST_DRAG) return;
    double lim = 30 * unit(p);
    if (p->vx > lim) p->vx = lim;
    if (p->vx < -lim) p->vx = -lim;
    if (p->vy < -lim) p->vy = -lim;
    p->thrown = 1;
    set_state(p, ST_FALL, 0);
}

void pet_click(Pet *p)
{
    double u = unit(p);
    if (p->state == ST_SLEEP || p->state == ST_WAKE) {
        p->forced_sleep = 0;
        say(p, G_EXCL, C_EXCL);
        p->vx = 0;
        p->vy = -10 * u;
        set_state(p, ST_FALL, 0);
    } else if (is_free(p->state) || p->state == ST_LAND || is_social(p->state)) {
        if (rr(p, 0, 4)) say(p, G_HEART, C_HEART);
        else say(p, G_QUEST, C_WHITE);
        p->vx = 0;
        p->vy = -11 * u;
        p->following = 0;
        set_state(p, ST_JUMP, 0);
    }
}

void pet_cheer(Pet *p)
{
    if (!is_free(p->state) && p->state != ST_LAND && !is_social(p->state)) return;
    say(p, G_HEART, C_HEART);
    p->vx = 0;
    p->vy = -11 * unit(p);
    p->following = 0;
    set_state(p, ST_JUMP, 0);
}

void pet_sleep(Pet *p)
{
    if (p->state == ST_DRAG || p->state == ST_LEAVE || p->state == ST_CLIMB) return;
    p->forced_sleep = 1;
    set_state(p, ST_SLEEP, 0);
}

void pet_leave(Pet *p)
{
    if (p->state == ST_CLIMB) {   // react right where it is on the stairs
        stairs_crumble(p);
        p->perch = p->area.bottom - (p->y + FOOT_Y * p->scale);
        if (p->perch < 0) p->perch = 0;
    }
    p->forced_sleep = 0;
    p->following = 0;
    p->paused = 0;
    p->vx = p->vy = 0;
    p->leave_kind = rr(p, 0, LEAVE_KINDS - 1);
    switch (p->leave_kind) {
    case LEAVE_SHOCK:
        p->vy = -5 * unit(p);   // a little jolt
        say(p, G_EXCL, C_EXCL);
        break;
    case LEAVE_CRY:
        spawn_sob(p, LEAVE_TICKS[LEAVE_CRY] + 30);
        break;
    case LEAVE_PANIC:
        p->dir = rr(p, 0, 1) ? 1 : -1;
        break;
    }
    set_state(p, ST_LEAVE, 0);
}

void pet_startle(Pet *p, double from_x)
{
    int s = p->state;
    if (!is_free(s) && s != ST_LAND && s != ST_SLEEP && s != ST_WAKE && !is_social(s)) return;
    int dir = p->x >= from_x ? 1 : -1;   // leap away from it...
    double u = unit(p);
    p->forced_sleep = 0;
    p->following = 0;
    p->look = -dir;                      // ...while staring at it
    p->vx = dir * 6 * u;
    p->vy = -8 * u;
    say(p, G_EXCL, C_EXCL);
    set_state(p, ST_STARTLE, 6);         // dur = ticks frozen at the apex
}

int pet_can_climb(const Pet *p)
{
    int s = p->state;
    return !p->stairs.n && !p->paused && !p->carrying &&
           (s == ST_IDLE || s == ST_WALK || s == ST_TYPE || s == ST_WAVE || s == ST_LAND) &&
           (stairs_room(p, 1) >= 3 || stairs_room(p, -1) >= 3);
}

void pet_climb(Pet *p)
{
    if (!pet_can_climb(p)) return;
    double k = p->scale;
    int n = rr(p, 5, MAX_STEPS), dir = rr(p, 0, 1) ? 1 : -1;
    if (stairs_room(p, dir) < n && stairs_room(p, -dir) > stairs_room(p, dir)) dir = -dir;
    if (stairs_room(p, dir) < n) n = stairs_room(p, dir);

    Stairs *s = &p->stairs;
    memset(s, 0, sizeof *s);
    s->n = n;
    s->dir = dir;
    s->scale = p->scale;
    s->crumble_t = -1;
    p->y = ground_y(p);
    s->x0 = p->x + (SPR_OX + 9) * k + dir * 5 * k;
    s->base = p->y + FOOT_Y * k;   // the taskbar, a wall, or the height it already climbed to
    for (int i = 0; i < n; i++) s->off[i] = (i + 1) * STEP_H;
    s->shown = 1;

    p->dir = p->look = dir;
    p->vx = p->vy = 0;
    p->following = 0;
    p->forced_sleep = 0;
    p->climb_i = 0;
    p->climb_ph = 0;
    set_state(p, ST_CLIMB, 8);
}

static void stairs_fill(Canvas *c, int x0, int y0, int x1, int y1, uint32_t col)
{
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < c->h; y++)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < c->w; x++) c->px[y * c->w + x] = col;
}

// A row of bricks h tall with its top at y; `cap` lights up the top surface.
static void stairs_bricks(Canvas *c, int x0, int y, int h, int stagger, int cap)
{
    for (int j = 0; j < h; j++) {
        if (y + j < 0 || y + j >= c->h) continue;
        for (int x = x0; x < x0 + STEP_W; x++) {
            uint32_t col = C_STONE;
            if (j == 0 || (x - x0 + stagger * STEP_W / 2) % STEP_W == 0) col = C_MORTAR;
            else if (cap && j == 1) col = C_STONE_L;
            c->px[(y + j) * c->w + x] = col;
        }
    }
}

int pet_stairs(const Pet *p, int style, Canvas *c, POINT *origin)
{
    const Stairs *s = &p->stairs;
    if (!s->n) return 0;
    int W = s->n * STEP_W + 2, H = s->n * STEP_H + 1, k = s->scale;
    c->w = W;
    c->h = H;
    cv_clear(c);

    for (int i = 0; i < s->shown; i++) {
        int x0 = 1 + (s->dir > 0 ? i : s->n - 1 - i) * STEP_W, off = (int)floor(s->off[i]);
        int top = H - (i + 1) * STEP_H + off;
        switch (style) {
        case STAIRS_SOLID:   // a column of i + 1 staggered bricks
            for (int r = 0; r <= i; r++)
                stairs_bricks(c, x0, H - (r + 1) * STEP_H + off, STEP_H, r % 2, r == i);
            break;
        case STAIRS_OUTLINE: {   // tread, riser on the near side, back wall behind the top step
            int front = s->dir > 0 ? x0 : x0 + STEP_W - 1, back = s->dir > 0 ? x0 + STEP_W - 1 : x0;
            stairs_fill(c, x0, top, x0 + STEP_W, top + 1, C_STONE_L);
            stairs_fill(c, front, top, front + 1, top + STEP_H + 1, C_STONE_L);
            if (i == s->n - 1) stairs_fill(c, back, top, back + 1, H, C_STONE_L);
            break;
        }
        default:             // a thin floating slab
            stairs_bricks(c, x0, top, 4, i % 2, 1);
            break;
        }
    }

    if (style == STAIRS_OUTLINE) {
        // A dark shadow under the lines keeps them visible on light backgrounds.
        for (int y = H - 2; y >= 0; y--)
            for (int x = 0; x < W; x++)
                if (c->px[y * W + x] == C_STONE_L && !c->px[(y + 1) * W + x])
                    c->px[(y + 1) * W + x] = C_OUTLINE;
    } else {
        // Dark outline wherever stone meets air (the bottom edge continues
        // under the taskbar).
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint32_t *px = &c->px[y * W + x];
                if (!*px) continue;
                int air = y == 0 || x == 0 || x == W - 1 || !px[-W] || !px[-1] || !px[1] ||
                          (y < H - 1 && !px[W]);
                if (air) *px = C_OUTLINE;
            }
    }

    origin->x = (LONG)floor(s->x0 + 0.5) - (s->dir > 0 ? 1 : s->n * STEP_W + 1) * k;
    origin->y = (LONG)floor(s->base + 0.5) - H * k;
    return 1;
}

// Ran into a wall on `side` (0: overhead). Starts ST_BUMP at tick t, so a
// pet that hit one in the air can skip straight to seeing stars.
static void start_bump(Pet *p, int side, int t)
{
    p->vx = p->vy = 0;
    p->following = 0;
    p->forced_sleep = 0;
    p->bump_side = side;
    p->buddy = 0;   // free for someone to come and comfort it
    p->count = 0;
    if (side) p->dir = side;
    p->hop_x = p->x;
    p->hop_y = p->y;
    set_state(p, ST_BUMP, 0);
    p->t = t;
}

void pet_update(Pet *p, const World *w)
{
    double u = unit(p);
    p->shake = 0;
    refresh_area(p);
    stairs_update(p);
    double gy = ground_y(p);

    // Grounded states stick to the floor; if the floor moved away, fall.
    if (p->state != ST_DRAG && p->state != ST_FALL && p->state != ST_JUMP &&
        p->state != ST_LEAVE && p->state != ST_STARTLE && p->state != ST_CLIMB && p->state != ST_BUMP &&
        p->state != ST_RIDE) {
        if (p->y < gy - 0.5) {   // walked off the end of a wall: keep going a little
            p->vx = p->state == ST_WALK || p->state == ST_FLEE ? p->dir * 1.5 * u : 0;
            p->vy = 0;
            set_state(p, ST_FALL, 0);
        } else {
            p->y = gy;
        }
        clamp_walls(p, 0);
    }

    // Cursor relative to the pet: horizontal offset from the sprite centre,
    // and height above the floor it stands on.
    double dxm = w->cursor.x - (p->x + (SPR_OX + 9) * p->scale);
    double dym = gy + FOOT_Y * p->scale - w->cursor.y;
    int cursor_live = w->cursor_still_ms < 3000;
    int hover = fabs(dxm) < 9 * p->scale && dym >= 0 && dym < 12 * p->scale;

    if (is_free(p->state)) {
        int want = -1;
        if (p->typist && (w->cc == CLAUDE_WORKING || w->typing))
            want = ST_TYPE;
        else if (w->cc == CLAUDE_WAITING)
            want = ST_WAVE;
        else if (w->heat > 1)   // too hot to roam: stay put by the fan
            want = w->cc == CLAUDE_IDLE && idle_ms() > SLEEP_AFTER_MS ? ST_SLEEP : ST_IDLE;
        else if (w->mouse_mode == MOUSE_SHY && cursor_live && fabs(dxm) < 140 * u && dym > -20 * u && dym < 160 * u)
            want = ST_FLEE;
        else if (w->cc == CLAUDE_IDLE && idle_ms() > SLEEP_AFTER_MS)
            want = ST_SLEEP;

        if (want >= 0 && p->state != want) {
            p->following = 0;
            p->forced_sleep = 0;
            set_state(p, want, 0);
            if (want == ST_FLEE) say(p, G_EXCL, C_EXCL);
        } else if (want < 0 && (p->state == ST_TYPE || p->state == ST_WAVE)) {
            set_state(p, ST_IDLE, rr(p, 30, 90));
        }
    }

    p->blink_t--;
    if (p->blink_t < -4) p->blink_t = rr(p, 60, 160);
    int blinking = p->blink_t < 0;

    Pose q = pose_stand();
    switch (p->state) {
    case ST_IDLE:
        if (cursor_live && fabs(dxm) < 600 * u) {
            p->look = dxm < -25 * u ? -1 : dxm > 25 * u ? 1 : 0;
        } else if (--p->look_t <= 0) {
            p->look = rr(p, 0, 3) ? 0 : (rr(p, 0, 1) ? 1 : -1);
            p->look_t = rr(p, 30, 120);
        }
        q.look = p->look;
        q.eyes = hover ? EYE_HAPPY : blinking ? EYE_CLOSED : EYE_OPEN;
        if (p->fan) {   // face into the breeze, eyes shut in bliss
            q.look = p->fan_side;
            q.eyes = blinking ? EYE_OPEN : EYE_HAPPY;
        }
        if (p->paused) break;

        if (w->mouse_mode == MOUSE_FOLLOW && w->heat < 2 && fabs(dxm) > 70 * u) {
            int dir = dxm > 0 ? 1 : -1;
            int at_wall = (dir < 0 && p->x <= min_x(p) + 1) || (dir > 0 && p->x >= max_x(p) - 1) ||
                          walled(p, dir, 2);
            if (!at_wall) {
                p->dir = dir;
                p->following = 1;
                set_state(p, ST_WALK, 0);
                break;
            }
        }
        if (w->heat == 1 && p->t % 90 == 15) spawn_drop(p);
        if (++p->t >= p->dur) {
            if (p->perch && plat_ticks_ms() >= p->perch_until) {   // seen enough: jump down
                say(p, G_HEART, C_HEART);
                p->perch = 0;
                p->vx = p->dir * 2 * u;
                p->vy = -9 * u;
                set_state(p, ST_JUMP, 0);
                break;
            }
            if (w->heat > 1) {   // no wandering off from the fan
                set_state(p, ST_IDLE, rr(p, 60, 180));
                break;
            }
            if (w->climb && pet_can_climb(p) && rr(p, 0, 99) < 3) {
                pet_climb(p);
                break;
            }
            int walk_pct = w->mouse_mode == MOUSE_FOLLOW ? 0 : 60 + (int)(30 * w->cpu);
            int r = rr(p, 0, 99);
            if (r < walk_pct) {
                double span = max_x(p) - min_x(p), pos = (p->x - min_x(p)) / span;
                p->dir = pos < 0.15 ? 1 : pos > 0.85 ? -1 : (rr(p, 0, 1) ? 1 : -1);
                if (walled(p, p->dir, 40 * p->scale) && rr(p, 0, 2)) p->dir = -p->dir;   // mostly
                p->following = 0;
                set_state(p, ST_WALK, rr(p, 60, 240));
            } else if (r < walk_pct + 8 && !p->carrying) {
                p->vx = 0;
                p->vy = -7 * u;
                set_state(p, ST_JUMP, 0);
            } else {
                set_state(p, ST_IDLE, rr(p, 60, 180));
            }
        }
        break;

    case ST_WALK: {
        // Busier CPU, faster Clawd (up to 3x).
        double speed = (p->following ? 2.6 : 1.6) * (1 + 2 * w->cpu) * u;
        if (p->following) p->dir = dxm > 0 ? 1 : -1;
        int hit = move_x(p, p->dir * speed);
        if (hit) {
            spawn_bonk(p, hit);
            start_bump(p, hit, 0);
            q = pose_splat(hit);
            break;
        }
        if (!p->following) {
            if (p->x <= min_x(p)) p->dir = 1;
            if (p->x >= max_x(p)) p->dir = -1;
        }
        int at_wall = p->x <= min_x(p) || p->x >= max_x(p);
        clamp_walls(p, 0);

        int period = speed > 3.5 * u ? 2 : speed > 2.2 * u ? 3 : 5;
        q = pose_walk((p->t / period) % 4, p->dir);
        if (blinking) q.eyes = EYE_CLOSED;
        if (w->heat && p->t % (w->heat > 1 ? 20 : 60) == 10) spawn_drop(p);
        p->t++;

        int done = p->following
            ? (fabs(dxm) < 20 * u || at_wall || w->mouse_mode != MOUSE_FOLLOW)
            : p->t >= p->dur;
        if (done || p->paused) {
            p->following = 0;
            set_state(p, ST_IDLE, rr(p, 40, 160));
        }
        break;
    }

    case ST_FLEE: {
        p->dir = dxm > 0 ? -1 : 1;
        int cornered = (p->dir < 0 && p->x <= min_x(p) + 1) || (p->dir > 0 && p->x >= max_x(p) - 1) ||
                       walled(p, p->dir, 2);
        if (cornered) {
            // Nowhere left to run: leap over the cursor.
            p->vx = -p->dir * 9 * u;
            p->vy = -16 * u;
            say(p, G_EXCL, C_EXCL);
            set_state(p, ST_FALL, 0);
            q = pose_air(1);
            break;
        }
        move_x(p, p->dir * 4.5 * u);
        clamp_walls(p, 0);
        q = pose_walk((p->t / 2) % 4, p->dir);
        q.eyes = EYE_WIDE;
        p->t++;
        if (fabs(dxm) > 280 * u || dym > 240 * u || w->mouse_mode != MOUSE_SHY)
            set_state(p, ST_IDLE, rr(p, 30, 90));
        break;
    }

    case ST_TYPE:
        // Real keystrokes drive the arms; for Claude Code we fake a steady patter.
        if (w->keys > 0 || (w->cc == CLAUDE_WORKING && !w->typing && rr(p, 0, 3) == 0)) {
            p->tap_side = p->tap_side > 0 ? -1 : 1;
            p->tap_t = 3;
        }
        q = pose_type(p->tap_t > 0 ? p->tap_side : 0);
        if (p->tap_t > 0) p->tap_t--;
        if (p->wear[SLOT_HEAD] == ACC_PHONES) q.squash = (p->t / 6) % 2;   // nodding along to the music
        if (blinking) q.eyes = EYE_CLOSED;
        p->t++;
        break;

    case ST_WAVE:
        q = pose_wave((p->t / 6) % 2);
        if (p->t % 75 == 0) say(p, G_EXCL, C_EXCL);
        p->t++;
        break;

    case ST_DRAG: {
        POINT c;
        plat_cursor(&c);
        double nx = c.x - p->drag_dx, ny = c.y - p->drag_dy;
        p->vx = p->vx * 0.5 + (nx - p->x) * 0.5;
        p->vy = p->vy * 0.5 + (ny - p->y) * 0.5;
        p->x = nx;
        p->y = ny;
        q = pose_dangle((p->t++ / 6) % 2);
        break;
    }

    case ST_FALL:
    case ST_JUMP:
    case ST_STARTLE: {
        if (p->state == ST_STARTLE && p->vy >= 0 && p->dur > 0) {
            p->dur--;   // cartoon freeze at the top of the leap
            q = pose_startle(p->look);
            break;
        }
        double k = p->scale, feet_was = p->y + FOOT_Y * k, head_was = p->y + SPR_OY * k;
        p->vy += 1.0 * u;
        if (p->vy > 40 * u) p->vy = 40 * u;
        if (p->thrown) p->vx *= 0.965;   // gentle air drag: half the speed gone in ~1 s
        int hit = move_x(p, p->vx);
        if (hit) {   // bounce off the side of a wall; dazed after landing if it was hard
            if (fabs(p->vx) > 6 * u) {
                p->bonked = hit;
                spawn_bonk(p, hit);
            }
            p->vx = -p->vx * 0.5;
        }
        p->y += p->vy;
        if (p->vy < 0 && bump_head(p, head_was)) {
            if (p->vy < -6 * u) {
                p->bonked = 2;
                spawn_bonk(p, 0);
            }
            p->vy = 1.0 * u;
        }
        clamp_walls(p, 0.6);
        if (p->y < p->area.top - SPR_OY * p->scale) {
            p->y = p->area.top - SPR_OY * p->scale;
            p->vy = fabs(p->vy) * 0.3;
        }
        q = p->state == ST_JUMP    ? pose_hop(p->vy < 0)
          : p->state == ST_STARTLE ? pose_startle(p->look)
          : pose_air(p->vy < 0);
        if (p->bonked) q.eyes = EYE_DIZZY;
        double fy = floor_at(p, p->x, feet_was) - FOOT_Y * k;
        if (p->y >= fy) {
            int hard = p->vy > 20 * u;
            p->y = fy;
            p->vy = 0;
            p->thrown = 0;
            if (p->bonked) {
                start_bump(p, p->bonked == 2 ? 0 : p->bonked, BUMP_LAND);
                p->bonked = 0;
                q = pose_land(1);
                q.eyes = EYE_DIZZY;
                break;
            }
            set_state(p, ST_LAND, hard ? 10 : 5);
            q = pose_land(hard);
        }
        break;
    }

    case ST_LAND:
        // Squash on impact, then skid to a stop rather than halting dead.
        p->vx *= 0.8;
        {
            int hit = move_x(p, p->vx);
            if (hit && fabs(p->vx) > 4 * u) {   // skidded into a wall
                spawn_bonk(p, hit);
                start_bump(p, hit, 0);
                q = pose_splat(hit);
                break;
            }
            if (hit) p->vx = 0;
        }
        clamp_walls(p, 0);
        q = p->t < p->dur ? pose_land(p->dur > 5) : pose_stand();
        if (++p->t >= p->dur && fabs(p->vx) < 0.5 * u) {
            p->vx = 0;
            set_state(p, ST_IDLE, rr(p, 40, 120));
        }
        break;

    case ST_CLIMB: {
        const Stairs *s = &p->stairs;
        double k = s->scale;
        int i = p->climb_i;
        q.look = s->dir;
        if (p->climb_ph == 0) {          // eyeing the step coming up
            if (blinking) q.eyes = EYE_CLOSED;
            if (s->off[i] <= 0 && ++p->t >= p->dur) {
                p->hop_x = p->x;
                p->hop_y = p->y;
                p->climb_ph = 1;
                p->t = 0;
                if (p->stairs.shown < s->n) p->stairs.shown++;   // the next one starts rising
            }
        } else if (p->climb_ph == 1) {   // hop, one step up and along
            double f = (double)++p->t / HOP_TICKS;
            double tx = step_cx(p, i) - (SPR_OX + 9) * k, ty = step_top(p, i) - FOOT_Y * k;
            p->x = p->hop_x + (tx - p->hop_x) * f;
            p->y = p->hop_y + (ty - p->hop_y) * f - 16 * k * f * (1 - f);
            q = pose_hop(f < 0.5);
            q.look = s->dir;
            if (p->t >= HOP_TICKS) {
                p->climb_ph = 2;
                p->t = 0;
            }
        } else {                         // landed
            q = pose_land(0);
            q.look = s->dir;
            if (++p->t >= 3) {
                if (i + 1 < s->n) {
                    p->climb_i++;
                    p->climb_ph = 0;
                    p->t = 0;
                    p->dur = 1;
                } else {                 // the top: stairs crumble away, the floor stays
                    p->perch = p->area.bottom - (p->y + FOOT_Y * k);
                    p->perch_until = plat_ticks_ms() + (ULONGLONG)rr(p, 60, 180) * 1000;
                    stairs_crumble(p);
                    say(p, G_HEART, C_HEART);
                    set_state(p, ST_IDLE, rr(p, 40, 90));
                }
            }
        }
        break;
    }

    case ST_BUMP: {
        double k = p->scale;
        int side = p->bump_side;
        if (p->t < BUMP_HOP) {            // splat
            q = pose_splat(side);
            p->shake = side && p->t % 2 ? -side : 0;
        } else if (p->t < BUMP_LAND) {    // stagger back off it in a little hop
            double f = (double)(p->t - BUMP_HOP + 1) / (BUMP_LAND - BUMP_HOP);
            if (side) move_x(p, -side * 1.25 * k);
            clamp_walls(p, 0);
            p->y = p->hop_y - 20 * k * f * (1 - f);
            q = pose_air(f < 0.5);
            q.eyes = EYE_DIZZY;
            q.look = side;
        } else {
            p->y = p->hop_y;
            if (p->y < ground_y(p) - 0.5) {   // staggered off the end of a wall
                p->vx = p->vy = 0;
                p->bonked = side ? side : 2;
                set_state(p, ST_FALL, 0);
                q = pose_air(0);
                q.eyes = EYE_DIZZY;
                break;
            }
            if (p->t < BUMP_DAZE) {
                q = pose_land(1);
                q.eyes = EYE_DIZZY;
            } else if (p->t < BUMP_END - 10) {
                q = pose_dazed(p->count);
                int sway = (p->count / 4) % 4;
                p->shake = sway == 1 ? 1 : sway == 3 ? -1 : 0;
                spawn_orbit(p);
            } else {                      // shake it off
                q = pose_stand();
                q.eyes = EYE_CLOSED;
                q.look = (p->t / 2) % 2 ? 1 : -1;
            }
        }
        if (p->buddy && p->t >= BUMP_END - 11) p->t = BUMP_END - 12;   // being comforted: stay put
        if (++p->t >= BUMP_END) {         // and off it goes, the other way
            p->dir = side ? -side : (rr(p, 0, 1) ? 1 : -1);
            if (p->paused || w->heat > 1) set_state(p, ST_IDLE, rr(p, 40, 120));
            else set_state(p, ST_WALK, rr(p, 30, 90));
        }
        break;
    }

    case ST_GREET:
    case ST_GOODBYE:
        q = pose_wave((p->t / (p->state == ST_GREET ? 5 : 7)) % 2);
        if (p->dir < 0) {   // wave with the arm on its side
            q.arm_l = q.arm_r;
            q.arm_r = 0;
        }
        q.look = p->dir;
        if (p->state == ST_GOODBYE && p->t > 20) {
            q.eyes = EYE_CLOSED;
            if (p->t % 6 == 0) spawn_tears(p, &q);
        }
        p->t++;
        break;

    case ST_CHAT: {
        int turn = p->t / CHAT_TURN, at = p->t % CHAT_TURN, mine = (turn % 2 == 0) == (p->lead != 0);
        q.look = p->dir;
        if (mine) {
            if (at == 3) spawn_chat(p);
            if (at >= 3 && at < 27) {   // talking with its hands
                int up = (at / 4) % 2 ? -2 : 0;
                if (p->dir > 0) q.arm_r = up;
                else q.arm_l = up;
            }
        } else if (at == 14) {          // how it takes it
            p->role = rr(p, 0, 3);
            if (p->role == 2) {
                static const int g[3] = { G_EXCL, G_QUEST, G_HEART };
                static const uint32_t c[3] = { C_EXCL, C_WHITE, C_HEART };
                int i = rr(p, 0, 2);
                say(p, g[i], c[i]);
            }
        } else if (at > 14 && at < 30) {
            if (p->role == 0) q.body_dy = (at / 3) % 2;   // nodding
            if (p->role == 1) q.eyes = EYE_HAPPY;
        }
        if (blinking && q.eyes == EYE_OPEN) q.eyes = EYE_CLOSED;
        p->t++;
        break;
    }

    case ST_CHASE: {
        double d = p->goal_x - mid_x(p);
        if (p->dur > 0) {                // just tagged: "it" counts before giving chase
            p->dur--;
            q.eyes = EYE_HAPPY;
            q.arm_l = q.arm_r = (p->dur / 4) % 2 ? -3 : -1;
            q.look = d > 0 ? 1 : -1;
        } else {
            p->dir = (d > 0) == (p->role != 0) ? 1 : -1;
            move_x(p, p->dir * (p->role ? 3.4 : 3.0) * u);
            clamp_walls(p, 0);
            if (p->role) {
                q = pose_walk((p->t / 2) % 4, p->dir);
                q.eyes = EYE_HAPPY;
            } else {
                q = pose_panic(p->t, p->dir);
            }
        }
        p->t++;
        break;
    }

    case ST_FOLLOW: {
        double d = p->goal_x - mid_x(p), step = d * 0.25, lim = 6 * u;
        if (step > lim) step = lim;
        if (step < -lim) step = -lim;
        if (fabs(d) < 1) step = d;
        move_x(p, step);
        clamp_walls(p, 0);
        if (fabs(step) > 0.4 * u) {
            p->dir = step > 0 ? 1 : -1;
            q = pose_walk((p->t / 3) % 4, p->dir);
        } else {
            q.look = p->role;            // the way the line faces
        }
        if (blinking) q.eyes = EYE_CLOSED;
        p->t++;
        break;
    }

    case ST_GOTO: {
        double d = p->goal_x - mid_x(p);
        p->lead = fabs(d) <= 2;          // there
        if (!p->lead) {
            p->dir = d > 0 ? 1 : -1;
            int hit = move_x(p, p->dir * fmin(fabs(d), 2.6 * u));
            if (hit) {
                spawn_bonk(p, hit);
                start_bump(p, hit, 0);
                q = pose_splat(hit);
                break;
            }
            clamp_walls(p, 0);
            q = pose_walk((p->t / 3) % 4, p->dir);
        }
        if (blinking) q.eyes = EYE_CLOSED;
        p->t++;
        break;
    }

    case ST_RIDE:   // pets_together() moves it about
        if (p->role == 0) {
            q = pose_hop(p->t < RIDE_HOP / 2);
        } else {
            q.look = p->dir;
            q.eyes = blinking ? EYE_CLOSED : EYE_HAPPY;
            if ((p->t / 30) % 4 == 3) q.arm_l = q.arm_r = (p->t / 4) % 2 ? -3 : -2;   // look how tall I am
        }
        p->t++;
        break;

    case ST_COMFORT: {
        int pat = (p->t / 5) % 2 ? -2 : 0;
        q.eyes = EYE_HAPPY;
        q.look = p->dir;
        if (p->dir > 0) q.arm_r = pat;
        else q.arm_l = pat;
        p->t++;
        break;
    }

    case ST_YAWN:
        if (p->t < 14) {
            q = pose_stretch();
            if (p->t == 4) spawn_z(p);
        } else {
            q.eyes = p->t < 24 ? EYE_CLOSED : EYE_OPEN;
        }
        p->t++;
        break;

    case ST_SLEEP:
        q = pose_sleep((p->t / 40) % 2);
        if (p->t % 45 == 0) spawn_z(p);
        p->t++;
        if (!p->forced_sleep && idle_ms() < 1000) set_state(p, ST_WAKE, 40);
        break;

    case ST_WAKE:
        if (p->t < 14) {
            q = pose_sleep(0);
            q.eyes = EYE_OPEN;
        } else {
            q = pose_stretch();
        }
        if (++p->t >= p->dur) set_state(p, ST_IDLE, 60);
        break;

    case ST_LEAVE: {
        int react = LEAVE_TICKS[p->leave_kind];
        p->vy += 1.0 * u;
        p->y += p->vy;
        if (p->t < react) {
            // Reacting on the spot first...
            if (p->y >= gy) {
                p->y = gy;
                p->vy = 0;
            }
            switch (p->leave_kind) {
            case LEAVE_SHOCK:
                if (p->t == 10) say(p, G_QUEST, C_WHITE);
                q = pose_shock(p->t);
                break;
            case LEAVE_SHIVER:
                p->shake = p->t % 2 ? 1 : -1;
                if (p->t % 10 == 5) spawn_drop(p);
                q = pose_shiver();
                break;
            case LEAVE_CRY:
                q = pose_cry(p->t);
                if (p->t % 3 == 0) spawn_tears(p, &q);
                break;
            case LEAVE_PANIC:
                if (rr(p, 0, 14) == 0) p->dir = -p->dir;
                if (move_x(p, p->dir * 5 * u)) p->dir = -p->dir;
                if (p->x <= min_x(p)) p->dir = 1;
                if (p->x >= max_x(p)) p->dir = -1;
                clamp_walls(p, 0);
                if (p->t % 18 == 0) say(p, G_EXCL, C_EXCL);
                if (p->t % 12 == 6) spawn_drop(p);
                q = pose_panic(p->t, p->dir);
                break;
            }
        } else {
            // ...then the taskbar gives way and down it goes.
            if (p->t == react) {
                p->dropped = 1;
                if (p->leave_kind == LEAVE_PANIC) p->vx = p->dir * 5 * u;   // still running
            }
            p->x += p->vx;
            clamp_walls(p, 0);
            q = pose_dangle((p->t / 3) % 2);
            if (p->leave_kind == LEAVE_CRY) {   // and still crying
                q.eyes = EYE_CLOSED;
                if (p->t % 3 == 0) spawn_tears(p, &q);
            }
            if (p->y > p->screen.bottom) p->gone = 1;
        }
        p->t++;
        break;
    }
    }
    memcpy(q.wear, p->wear, sizeof q.wear);
    q.tick = (int)p->age++;
    p->pose = q;

    // The fan comes out when it's hot and the pet is staying in one place,
    // on the side facing the middle of the screen.
    int still = p->state == ST_IDLE || p->state == ST_TYPE || p->state == ST_WAVE ||
                p->state == ST_SLEEP || p->state == ST_WAKE;
    int fan = w->heat > 1 && still;
    if (fan && !p->fan)
        p->fan_side = p->x + (SPR_OX + 9) * p->scale < (p->area.left + p->area.right) / 2.0 ? 1 : -1;
    p->fan = fan;
    if (fan && ++p->fan_t % 7 == 0) spawn_wind(p);

    for (int i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &p->bub[i];
        if (b->life <= 0) continue;
        b->vy += b->ay;
        b->x += b->vx;
        b->y += b->vy;
        b->life--;
    }
}

void pet_render(const Pet *p, Canvas *c)
{
    cv_clear(c);
    if (p->fan) draw_fan(c, p->fan_side > 0 ? CW - FAN_W : 0, FOOT_Y - FAN_H, (p->fan_t / 2) % 2);
    draw_clawd(c, SPR_OX + p->shake, SPR_OY, &p->pose);
    for (int i = 0; i < MAX_BUBBLES; i++) {
        const Bubble *b = &p->bub[i];
        if (b->life <= 0) continue;
        int fade = b->max < 30 ? b->max / 3 : 15;
        int alpha = b->life < fade ? b->life * 255 / fade : 255;
        if (b->framed) draw_speech(c, (int)floor(b->x), (int)floor(b->y), b->glyph, b->color, alpha);
        else draw_glyph(c, (int)floor(b->x), (int)floor(b->y), b->glyph, b->color, alpha);
    }
}

/* ---- together --------------------------------------------------------- */

static Pet *pet_by_id(Pet **ps, int n, int id)
{
    for (int i = 0; id && i < n; i++)
        if (ps[i]->id == id) return ps[i];
    return NULL;
}

// On the same floor of the same screen.
static int side_by_side(const Pet *a, const Pet *b)
{
    return fabs(feet_y(a) - feet_y(b)) < 2 && a->scale == b->scale &&
           a->area.left == b->area.left && a->area.bottom == b->area.bottom;
}

// Free to start something with another pet.
static int g_busy, g_hot;   // the user is typing / Claude Code is on; too hot to play (set each tick)
static int g_asked;         // pets_play(): never mind the wait between games, or how far apart they are

// Keeps out of games for now: it's too hot, or it is the one at the keyboard.
static int quiet(const Pet *p)
{
    return g_hot || (g_busy && p->typist);
}

// Free to start something with another pet.
static int sociable(const Pet *p, ULONGLONG now)
{
    return (p->state == ST_IDLE || p->state == ST_WALK) && !p->paused && !p->following &&
           !p->buddy && !p->carrying && !quiet(p) && (g_asked || now >= p->social_at);
}

// Close enough to play with, `px` at scale 4.
static int within(const Pet *a, const Pet *b, double px)
{
    return g_asked || fabs(mid_x(a) - mid_x(b)) < px * unit(a);
}

// Doing something together right now, so they may overlap.
static int linked(const Pet *a, const Pet *b)
{
    int line_a = a->state == ST_FOLLOW || a->state == ST_WALK, line_b = b->state == ST_FOLLOW || b->state == ST_WALK;
    return a->buddy == b->id || b->buddy == a->id || a->carrying == b->id || b->carrying == a->id ||
           (line_a && line_b && (a->state == ST_FOLLOW || b->state == ST_FOLLOW));   // in the same line
}

// Steady enough to have someone standing on its head.
static int can_carry(const Pet *p)
{
    return p->state == ST_IDLE || p->state == ST_WALK;
}

static int in_wall(const Pet *p)
{
    double k = p->scale, l = p->x + BOX_L * k, r = p->x + BOX_R * k, t = p->y + SPR_OY * k, b = feet_y(p);
    for (int i = 0; i < g_nwalls; i++) {
        const RECT *w = &g_walls[i];
        if (l < w->right - 0.5 && r > w->left + 0.5 && t < w->bottom - 0.5 && b > w->top + 0.5) return 1;
    }
    return 0;
}

static void drop_out(Pet *p, ULONGLONG now)
{
    p->buddy = 0;
    p->social_at = now + SOCIAL_MS;
    set_state(p, ST_IDLE, rr(p, 30, 90));
}

// Done together: each walks off its own way.
static void part(Pet *a, Pet *b, ULONGLONG now)
{
    Pet *two[2] = { a, b };
    for (int i = 0; i < 2; i++) {
        Pet *p = two[i];
        p->buddy = 0;
        p->social_at = now + SOCIAL_MS;
        p->dir = mid_x(p) < mid_x(two[!i]) ? -1 : 1;
        set_state(p, ST_WALK, rr(p, 30, 80));
    }
}

// Whoever is standing on its head comes down.
static void knock_off(Pet *r, Pet *carrier)
{
    if (carrier) carrier->carrying = 0;
    r->buddy = 0;
    r->vx = (rr(r, 0, 1) ? 2 : -2) * unit(r);
    r->vy = -3 * unit(r);
    say(r, G_EXCL, C_EXCL);
    set_state(r, ST_FALL, 0);
}

// Screen y for the feet of a pet standing on c's head (which bobs as it walks).
static double ride_y(const Pet *c)
{
    const Pose *q = &c->pose;
    int top = SPR_OY + q->body_dy + (q->squash > 0) - (q->squash < 0) - acc_above(c->wear);   // on top of the hat
    return c->y + top * c->scale - FOOT_Y * c->scale;
}

static void greet(Pet *a, Pet *b)
{
    double k = a->scale, gap = 3 * k;
    Pet *l = mid_x(a) < mid_x(b) ? a : b, *r = l == a ? b : a;
    double short_by = gap - ((r->x + BOX_L * k) - (l->x + BOX_R * k));
    if (short_by > 0) {   // a step apart, face to face
        move_x(l, -short_by / 2);
        move_x(r, short_by / 2);
    }
    int turns = rr(a, 0, 9) < 4 ? 0 : rr(a, 3, 6);
    Pet *two[2] = { a, b };
    for (int i = 0; i < 2; i++) {
        Pet *p = two[i];
        p->buddy = two[!i]->id;
        p->lead = i == 0;
        p->count = turns;
        p->following = 0;
        p->vx = 0;
        p->dir = p == l ? 1 : -1;
        set_state(p, ST_GREET, 0);
    }
    say(a, G_HEART, C_HEART);
    say(b, rr(b, 0, 1) ? G_EXCL : G_HEART, rr(b, 0, 1) ? C_EXCL : C_HEART);
}

static int start_chase(Pet **ps, int n, Pet *a, ULONGLONG now)
{
    for (int i = 0; i < n; i++) {
        Pet *b = ps[i];
        if (b == a || !sociable(b, now) || !side_by_side(a, b) || !within(a, b, 900)) continue;
        Pet *two[2] = { a, b };
        for (int j = 0; j < 2; j++) {
            two[j]->buddy = two[!j]->id;
            two[j]->role = j == 0;      // a is "it"
            two[j]->following = 0;
            set_state(two[j], ST_CHASE, j == 0 ? 15 : 0);
        }
        a->count = rr(a, 2, 4);         // tags before they have had enough
        say(a, G_EXCL, C_EXCL);
        say(b, G_EXCL, C_EXCL);
        return 1;
    }
    return 0;
}

// A walks off humming, everyone else nearby falls in behind.
static int start_line(Pet **ps, int n, Pet *a, ULONGLONG now)
{
    Pet *line[8];
    int m = 0;
    for (int i = 0; i < n && m < 8; i++)
        if (ps[i] != a && sociable(ps[i], now) && side_by_side(a, ps[i]) && within(a, ps[i], 1200))
            line[m++] = ps[i];
    if (!m) return 0;
    for (int i = 1; i < m; i++)   // nearest first
        for (int j = i; j > 0 && fabs(mid_x(line[j]) - mid_x(a)) < fabs(mid_x(line[j - 1]) - mid_x(a)); j--) {
            Pet *t = line[j];
            line[j] = line[j - 1];
            line[j - 1] = t;
        }
    double pos = (mid_x(a) - a->area.left) / (double)(a->area.right - a->area.left);
    a->dir = pos < 0.5 ? 1 : -1;      // the side with more room
    a->following = 0;
    a->social_at = now + SOCIAL_MS;
    set_state(a, ST_WALK, rr(a, 250, 450));
    say(a, G_NOTE, C_NOTE);
    for (int i = 0; i < m; i++) {
        Pet *p = line[i];
        p->buddy = (i ? line[i - 1] : a)->id;
        p->role = a->dir;
        p->following = 0;
        p->social_at = now + SOCIAL_MS;
        set_state(p, ST_FOLLOW, 0);
    }
    return 1;
}

// A goes over to b and climbs onto its head.
static int start_ride(Pet **ps, int n, Pet *a, ULONGLONG now)
{
    for (int i = 0; i < n; i++) {
        Pet *b = ps[i];
        if (b == a || !sociable(b, now) || !side_by_side(a, b) || !within(a, b, 600)) continue;
        Pet top = *a;   // room up there?
        top.x = b->x;
        top.y = b->y - (FOOT_Y - SPR_OY) * b->scale;
        if (top.y + (SPR_OY - 12) * top.scale < b->area.top || in_wall(&top)) continue;
        a->buddy = b->id;
        a->role = GO_RIDE;
        a->following = 0;
        a->social_at = b->social_at = now + SOCIAL_MS;
        set_state(a, ST_GOTO, 0);
        b->carrying = a->id;
        b->following = 0;
        set_state(b, ST_IDLE, 60);   // wait for it
        return 1;
    }
    return 0;
}

// A walks over to b to say hello.
static int start_greet(Pet **ps, int n, Pet *a, ULONGLONG now)
{
    for (int i = 0; i < n; i++) {
        Pet *b = ps[i];
        if (b == a || !sociable(b, now) || !side_by_side(a, b)) continue;
        a->buddy = b->id;
        a->role = GO_GREET;
        a->following = 0;
        set_state(a, ST_GOTO, 0);
        b->following = 0;
        b->social_at = now + SOCIAL_MS;   // nobody else's game meanwhile
        set_state(b, ST_IDLE, 900);       // wait for it (as long as a GOTO may take)
        return 1;
    }
    return 0;
}

int pets_play(Pet **ps, int n, Pet *who, int what)
{
    ULONGLONG now = plat_ticks_ms();
    Pet *by[16];
    int ok = 0;
    if (n > 16) n = 16;
    g_asked = 1;
    for (int i = 0; i < n && !ok; i++) {
        Pet *a = who ? who : ps[i];
        if (sociable(a, now)) {
            if (what == PLAY_DAZE) {   // keels over; the nearest free one, however far, comes to pat it
                start_bump(a, 0, BUMP_LAND);
                Pet *best = NULL;
                for (int j = 0; j < n; j++) {
                    Pet *c = ps[j];
                    if (c != a && sociable(c, now) && side_by_side(a, c) &&
                        (!best || fabs(mid_x(c) - mid_x(a)) < fabs(mid_x(best) - mid_x(a))))
                        best = c;
                }
                if (best) {
                    best->buddy = a->id;
                    best->role = GO_COMFORT;
                    best->following = 0;
                    set_state(best, ST_GOTO, 0);
                    a->buddy = best->id;
                }
                ok = 1;
                break;
            }
            int m = 0;   // the others, nearest first
            for (int j = 0; j < n; j++)
                if (ps[j] != a) by[m++] = ps[j];
            for (int j = 1; j < m; j++)
                for (int k = j; k > 0 && fabs(mid_x(by[k]) - mid_x(a)) < fabs(mid_x(by[k - 1]) - mid_x(a)); k--) {
                    Pet *t = by[k];
                    by[k] = by[k - 1];
                    by[k - 1] = t;
                }
            ok = what == PLAY_GREET ? start_greet(by, m, a, now)
               : what == PLAY_CHASE ? start_chase(by, m, a, now)
               : what == PLAY_LINE  ? start_line(by, m, a, now)
                                    : start_ride(by, m, a, now);
        }
        if (who) break;
    }
    g_asked = 0;
    return ok;
}

void pets_together(Pet **ps, int n, const World *w)
{
    static ULONGLONG next_game;
    ULONGLONG now = plat_ticks_ms();
    g_busy = w->typing || w->cc != CLAUDE_IDLE;   // the one at the keyboard stays there
    g_hot = w->heat > 1;

    // Keep everything that was going on in step, or call it off.
    for (int i = 0; i < n; i++) {
        Pet *p = ps[i], *b = pet_by_id(ps, n, p->buddy);
        int ok = 1;
        if (p->carrying) {
            Pet *r = pet_by_id(ps, n, p->carrying);
            if (!r || r->buddy != p->id || (r->state != ST_RIDE && r->state != ST_GOTO)) p->carrying = 0;
        }
        switch (p->state) {
        case ST_GREET:
        case ST_CHAT:
        case ST_CHASE:
            ok = b && b->state == p->state && b->buddy == p->id;
            break;
        case ST_FOLLOW:
            ok = b && (b->state == ST_WALK || b->state == ST_FOLLOW) && side_by_side(p, b);
            break;
        case ST_GOTO:
            ok = b && p->t < 900 &&
                 (p->role == GO_RIDE    ? b->carrying == p->id && can_carry(b) && side_by_side(p, b)
                : p->role == GO_GREET   ? can_carry(b) && side_by_side(p, b) && !quiet(b)
                : p->role == GO_COMFORT ? b->state == ST_BUMP && b->buddy == p->id
                                        : b->state == ST_SLEEP);
            break;
        case ST_RIDE:
            if (!b || b->carrying != p->id || !can_carry(b) || quiet(p) || quiet(b)) {
                knock_off(p, b && b->carrying == p->id ? b : NULL);
                continue;
            }
            break;
        case ST_COMFORT:
            ok = b && b->state == ST_BUMP && b->buddy == p->id;
            break;
        case ST_YAWN:
            break;
        case ST_GOODBYE:
            ok = b && b->state == ST_LEAVE;
            break;
        case ST_SLEEP:   // dozed off next to b: wakes up with it
            if (p->buddy && !(b && b->state == ST_SLEEP)) {
                p->buddy = 0;
                p->forced_sleep = 0;
                set_state(p, ST_WAKE, 40);
            }
            continue;
        case ST_BUMP:    // b is coming to comfort it
            if (p->buddy && !(b && (b->state == ST_GOTO || b->state == ST_COMFORT) && b->buddy == p->id))
                p->buddy = 0;
            continue;
        default:
            p->buddy = 0;
            continue;
        }
        if (quiet(p) || (b && quiet(b))) {
            drop_out(p, now);
        } else if (!ok) {
            say(p, G_QUEST, C_WHITE);
            drop_out(p, now);
        }
    }

    // Carry on with it.
    for (int i = 0; i < n; i++) {
        Pet *p = ps[i], *b = pet_by_id(ps, n, p->buddy);
        if (!b || !is_social(p->state)) continue;
        double k = p->scale, u = unit(p);
        int side = mid_x(p) < mid_x(b) ? -1 : 1;   // which side of b it is on
        switch (p->state) {
        case ST_GREET:
            if (p->lead && p->t >= GREET_TICKS) {
                if (p->count) {
                    set_state(p, ST_CHAT, 0);
                    set_state(b, ST_CHAT, 0);
                } else {
                    part(p, b, now);
                }
            }
            break;

        case ST_CHAT:
            if (p->lead && p->t >= p->count * CHAT_TURN) {
                if (rr(p, 0, 1)) {
                    say(p, G_HEART, C_HEART);
                    say(b, G_HEART, C_HEART);
                }
                part(p, b, now);
            }
            break;

        case ST_CHASE:
            p->goal_x = mid_x(b);
            if (p->t > 900) {            // can't get at each other (a wall?): never mind
                part(p, b, now);
                break;
            }
            if (p->role && !p->dur && fabs(mid_x(p) - mid_x(b)) < 13 * k) {   // tag!
                say(p, G_EXCL, C_EXCL);
                if (--p->count <= 0) {   // that's enough: a happy hop each
                    Pet *two[2] = { p, b };
                    for (int j = 0; j < 2; j++) {
                        two[j]->buddy = 0;
                        two[j]->social_at = now + SOCIAL_MS;
                        two[j]->vx = 0;
                        two[j]->vy = -9 * u;
                        say(two[j], G_HEART, C_HEART);
                        set_state(two[j], ST_JUMP, 0);
                    }
                } else {
                    b->count = p->count;
                    p->role = 0;
                    b->role = 1;
                    b->dur = 20;
                }
            }
            break;

        case ST_FOLLOW:
            p->goal_x = mid_x(b) - b->dir * 22 * k;
            p->role = b->dir;
            if (b->state == ST_WALK && b->t % 40 == 20) say(b, G_NOTE, C_NOTE);   // the leader hums along
            break;

        case ST_GOTO:
            p->goal_x = mid_x(b) + side * (p->role == GO_RIDE ? 16 : 20) * k;
            if (!p->lead) break;
            p->dir = -side;
            p->t = 0;
            if (p->role == GO_RIDE) {
                p->role = 0;
                p->hop_x = p->x;
                p->hop_y = p->y;
                set_state(p, ST_RIDE, 0);
            } else if (p->role == GO_COMFORT) {
                set_state(p, ST_COMFORT, 0);
            } else if (p->role == GO_GREET) {   // asked for (pets_play): they always get to chat
                p->buddy = 0;
                greet(p, b);
                if (!p->count) p->count = b->count = rr(p, 3, 6);
            } else {
                p->forced_sleep = 1;
                set_state(p, ST_SLEEP, 0);
            }
            break;

        case ST_RIDE: {
            double tx = b->x, ty = ride_y(b);
            if (p->role == 0) {          // hopping up
                double f = (double)p->t / RIDE_HOP;
                if (f > 1) f = 1;
                p->x = p->hop_x + (tx - p->hop_x) * f;
                p->y = p->hop_y + (ty - p->hop_y) * f - 24 * k * f * (1 - f);
                if (p->t >= RIDE_HOP) {
                    p->role = 1;
                    p->count = rr(p, 150, 400);
                    p->t = 0;
                    say(p, G_HEART, C_HEART);
                }
                break;
            }
            p->x = tx;
            p->y = ty;
            p->dir = b->dir;
            if (in_wall(p)) {            // scraped off by a wall
                spawn_bonk(p, 0);
                knock_off(p, b);
                p->bonked = 2;
            } else if (p->t >= p->count) {   // had enough: jump down
                b->carrying = 0;
                p->buddy = 0;
                p->vx = (rr(p, 0, 1) ? 3 : -3) * u;
                p->vy = -7 * u;
                set_state(p, ST_JUMP, 0);
            }
            break;
        }

        case ST_COMFORT:
            p->dir = -side;
            if (p->t % 25 == 10) say(b, G_HEART, C_HEART);
            if (p->t >= 60) {            // there, there
                b->buddy = 0;
                drop_out(p, now);
            }
            break;

        case ST_YAWN:
            if (p->t < 30) break;
            if (p->role && b->state == ST_SLEEP) {   // and goes to lie down next to it
                p->role = GO_SLEEP;
                set_state(p, ST_GOTO, 0);
            } else {
                drop_out(p, now);
            }
            break;

        case ST_GOODBYE:
            p->dir = -side;
            break;
        }
    }

    if (g_hot || n < 2) return;

    for (int i = 0; i < n; i++) {
        Pet *a = ps[i];
        double k = a->scale, u = unit(a);

        // Thrown at another pet: knocks it silly.
        if (a->state == ST_FALL && a->thrown && hypot(a->vx, a->vy) > 8 * u) {
            for (int j = 0; j < n; j++) {
                Pet *b = ps[j];
                if (b == a || b->state == ST_DRAG || b->state == ST_LEAVE || b->state == ST_BUMP || b->state == ST_RIDE ||
                    b->state == ST_CLIMB)
                    continue;
                double ox = fmin(a->x + (SPR_OX + 15) * k, b->x + (SPR_OX + 15) * k) -
                            fmax(a->x + (SPR_OX + 3) * k, b->x + (SPR_OX + 3) * k);
                double oy = fmin(feet_y(a), feet_y(b)) - fmax(a->y + SPR_OY * k, b->y + SPR_OY * k);
                if (ox <= 0 || oy <= 0) continue;
                int side = mid_x(a) > mid_x(b) ? 1 : -1;   // where it came from, seen from b
                spawn_bonk(b, side);
                start_bump(b, side, 0);
                a->vx = -a->vx * 0.4;
                if (a->vy > -4 * u) a->vy = -4 * u;
                say(a, G_EXCL, C_EXCL);
                break;
            }
        }

        // Walking into another pet: say hello if both are free, else walk on through.
        int moving = a->state == ST_WALK || a->state == ST_FLEE || a->state == ST_GOTO ||
                     a->state == ST_CHASE || a->state == ST_FOLLOW;
        for (int j = 0; moving && j < n; j++) {
            Pet *b = ps[j];
            if (b == a || b->state == ST_RIDE || !side_by_side(a, b) || linked(a, b)) continue;
            if ((mid_x(b) - mid_x(a)) * a->dir <= 0) continue;   // not ahead of it
            double gap = a->dir > 0 ? (b->x + BOX_L * k) - (a->x + BOX_R * k) : (a->x + BOX_L * k) - (b->x + BOX_R * k);
            if (gap > 3 * k || gap < -6 * k) continue;           // not there yet, or already past
            if (sociable(a, now) && sociable(b, now)) greet(a, b);
            break;
        }

        // Someone is dazed: the nearest one free goes over to comfort it.
        if (a->state == ST_BUMP && a->t >= BUMP_DAZE && a->t < BUMP_DAZE + 5 && !a->buddy) {
            Pet *best = NULL;
            for (int j = 0; j < n; j++) {
                Pet *c = ps[j];
                if (c == a || !(c->state == ST_IDLE || c->state == ST_WALK) || c->paused || c->buddy ||
                    c->carrying || !side_by_side(a, c) || fabs(mid_x(c) - mid_x(a)) > 700 * u)
                    continue;
                if (!best || fabs(mid_x(c) - mid_x(a)) < fabs(mid_x(best) - mid_x(a))) best = c;
            }
            if (best) {
                best->buddy = a->id;
                best->role = GO_COMFORT;
                best->following = 0;
                set_state(best, ST_GOTO, 0);
                a->buddy = best->id;
            }
        }

        // Sleeping: yawns are catching.
        if (a->state == ST_SLEEP && a->t % 60 == 30)
            for (int j = 0; j < n; j++) {
                Pet *c = ps[j];
                if (c == a || !sociable(c, now) || !side_by_side(a, c) || fabs(mid_x(c) - mid_x(a)) > 500 * u ||
                    rr(c, 0, 2))
                    continue;
                c->buddy = a->id;
                c->role = rr(c, 0, 1);   // and then lies down next to it
                c->social_at = now + SOCIAL_MS;
                c->following = 0;
                set_state(c, ST_YAWN, 0);
            }

        // Being sent away: everyone waves goodbye.
        if (a->state == ST_LEAVE && a->t < LEAVE_TICKS[a->leave_kind] - 5)
            for (int j = 0; j < n; j++) {
                Pet *c = ps[j];
                int s = c->state;
                if (c == a || c->buddy == a->id || c->carrying || c->paused || c->area.left != a->area.left ||
                    !(s == ST_IDLE || s == ST_WALK || s == ST_GREET || s == ST_CHAT || s == ST_CHASE ||
                      s == ST_FOLLOW || s == ST_YAWN))
                    continue;
                c->buddy = a->id;
                c->dir = mid_x(a) > mid_x(c) ? 1 : -1;
                c->following = 0;
                set_state(c, ST_GOODBYE, 0);
            }
    }

    // Now and then, a game.
    if (!next_game) next_game = now + 20000;
    if (now < next_game) return;
    next_game = now + (ULONGLONG)rr(ps[0], 30, 90) * 1000;
    Pet *a = ps[rr(ps[0], 0, n - 1)];
    if (!sociable(a, now)) return;
    int first = rr(a, 0, 2);
    for (int g = 0; g < 3; g++) {
        int game = (first + g) % 3;
        if (game == 0 ? start_chase(ps, n, a, now) : game == 1 ? start_line(ps, n, a, now) : start_ride(ps, n, a, now))
            break;
    }
}
