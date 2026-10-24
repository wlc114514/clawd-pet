#include "pet.h"
#include <math.h>

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
static double ground_y(const Pet *p) { return p->area.bottom - p->perch - FOOT_Y * p->scale; }
static double min_x(const Pet *p) { return p->area.left - (SPR_OX + 1) * p->scale; }
static double max_x(const Pet *p) { return p->area.right - (SPR_OX + 17) * p->scale; }

static DWORD idle_ms(void)
{
    LASTINPUTINFO li = { sizeof li };
    GetLastInputInfo(&li);
    return GetTickCount() - li.dwTime;
}

static void refresh_area(Pet *p)
{
    POINT feet = { (LONG)p->x + CW * p->scale / 2, (LONG)p->y + FOOT_Y * p->scale - 1 };
    MONITORINFO mi = { sizeof mi };
    if (GetMonitorInfoW(MonitorFromPoint(feet, MONITOR_DEFAULTTONEAREST), &mi)) {
        p->area = mi.rcWork;
        p->screen = mi.rcMonitor;
    }
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
// and no higher than half the screen.
static int stairs_room(const Pet *p, int dir)
{
    double k = p->scale, c = p->x + (SPR_OX + 9) * k;
    double room = dir > 0 ? p->area.right - 8 * k - c : c - (p->area.left + 8 * k);
    int n = room > 0 ? (int)(room / (STEP_W * k)) : 0;
    int tall = (int)((p->area.bottom - p->area.top) / 2 / (STEP_H * k));
    if (n > tall) n = tall;
    return n > MAX_STEPS ? MAX_STEPS : n;
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
    };
    all[1].eyes = EYE_CLOSED;
    all[2].look = -1;
    int n = (int)(sizeof all / sizeof all[0]);
    if (n > max) n = max;
    for (int i = 0; i < n; i++) out[i] = all[i];
    return n;
}

/* ---- public ----------------------------------------------------------- */

void pet_init(Pet *p, int scale, unsigned seed)
{
    ZeroMemory(p, sizeof *p);
    p->rng = seed | 1;
    p->scale = scale;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &p->area, 0);
    p->x = min_x(p) + rr(p, 0, (int)(max_x(p) - min_x(p)));
    p->y = ground_y(p);
    p->dir = 1;
    p->blink_t = rr(p, 30, 90);
    set_state(p, ST_IDLE, 45);
    p->pose = pose_stand();
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
    POINT h = { (LONG)(p->x + (SPR_OX + 9) * p->scale), (LONG)(p->y + (SPR_OY - 2) * p->scale) };
    return h;
}

void pet_begin_drag(Pet *p, POINT grab)
{
    if (p->state == ST_LEAVE) return;   // no rescuing it mid-fall
    if (p->state == ST_CLIMB) stairs_crumble(p);
    p->perch = 0;                       // let go and it lands on the taskbar
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
    } else if (is_free(p->state) || p->state == ST_LAND) {
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
    if (!is_free(p->state) && p->state != ST_LAND) return;
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
    if (!is_free(s) && s != ST_LAND && s != ST_SLEEP && s != ST_WAKE) return;
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
    return !p->perch && !p->stairs.n && !p->paused &&
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
    ZeroMemory(s, sizeof *s);
    s->n = n;
    s->dir = dir;
    s->scale = p->scale;
    s->crumble_t = -1;
    p->y = ground_y(p);
    s->x0 = p->x + (SPR_OX + 9) * k + dir * 5 * k;
    s->base = p->area.bottom;
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

void pet_update(Pet *p, const World *w)
{
    double u = unit(p);
    p->shake = 0;
    refresh_area(p);
    stairs_update(p);
    double gy = ground_y(p);

    // Grounded states stick to the floor; if the floor moved away, fall.
    if (p->state != ST_DRAG && p->state != ST_FALL && p->state != ST_JUMP &&
        p->state != ST_LEAVE && p->state != ST_STARTLE && p->state != ST_CLIMB) {
        if (p->y < gy - 0.5) {
            p->vx = p->vy = 0;
            set_state(p, ST_FALL, 0);
        } else {
            p->y = gy;
        }
        clamp_walls(p, 0);
    }

    // Cursor relative to the pet: horizontal offset from the sprite centre,
    // and height above the floor it stands on.
    double dxm = w->cursor.x - (p->x + (SPR_OX + 9) * p->scale);
    double dym = p->area.bottom - p->perch - w->cursor.y;
    int cursor_live = w->cursor_still_ms < 3000;
    int hover = fabs(dxm) < 9 * p->scale && dym >= 0 && dym < 12 * p->scale;

    if (is_free(p->state)) {
        int want = -1;
        if (w->cc == CLAUDE_WORKING || w->typing)
            want = ST_TYPE;
        else if (w->cc == CLAUDE_WAITING)
            want = ST_WAVE;
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
        if (p->paused) break;

        if (w->mouse_mode == MOUSE_FOLLOW && fabs(dxm) > 70 * u) {
            int dir = dxm > 0 ? 1 : -1;
            int at_wall = (dir < 0 && p->x <= min_x(p) + 1) || (dir > 0 && p->x >= max_x(p) - 1);
            if (!at_wall) {
                p->dir = dir;
                p->following = 1;
                set_state(p, ST_WALK, 0);
                break;
            }
        }
        if (w->cpu > 0.8 && p->t % 90 == 45) spawn_drop(p);
        if (++p->t >= p->dur) {
            if (p->perch && GetTickCount64() >= p->perch_until) {   // seen enough: jump down
                say(p, G_HEART, C_HEART);
                p->perch = 0;
                p->vx = p->dir * 2 * u;
                p->vy = -9 * u;
                set_state(p, ST_JUMP, 0);
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
                p->following = 0;
                set_state(p, ST_WALK, rr(p, 60, 240));
            } else if (r < walk_pct + 8) {
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
        p->x += p->dir * speed;
        if (!p->following) {
            if (p->x <= min_x(p)) p->dir = 1;
            if (p->x >= max_x(p)) p->dir = -1;
        }
        int at_wall = p->x <= min_x(p) || p->x >= max_x(p);
        clamp_walls(p, 0);

        int period = speed > 3.5 * u ? 2 : speed > 2.2 * u ? 3 : 5;
        q = pose_walk((p->t / period) % 4, p->dir);
        if (blinking) q.eyes = EYE_CLOSED;
        if (w->cpu > 0.8 && p->t % 60 == 30) spawn_drop(p);
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
        int cornered = (p->dir < 0 && p->x <= min_x(p) + 1) || (p->dir > 0 && p->x >= max_x(p) - 1);
        if (cornered) {
            // Nowhere left to run: leap over the cursor.
            p->vx = -p->dir * 9 * u;
            p->vy = -16 * u;
            say(p, G_EXCL, C_EXCL);
            set_state(p, ST_FALL, 0);
            q = pose_air(1);
            break;
        }
        p->x += p->dir * 4.5 * u;
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
        GetCursorPos(&c);
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
    case ST_STARTLE:
        if (p->state == ST_STARTLE && p->vy >= 0 && p->dur > 0) {
            p->dur--;   // cartoon freeze at the top of the leap
            q = pose_startle(p->look);
            break;
        }
        p->vy += 1.0 * u;
        if (p->vy > 40 * u) p->vy = 40 * u;
        if (p->thrown) {   // air drag, stronger the faster it goes
            double drag = 0.03 + 0.004 * fabs(p->vx) / u;
            p->vx *= 1 - (drag > 0.15 ? 0.15 : drag);
        }
        p->x += p->vx;
        p->y += p->vy;
        clamp_walls(p, 0.6);
        if (p->y < p->area.top - SPR_OY * p->scale) {
            p->y = p->area.top - SPR_OY * p->scale;
            p->vy = fabs(p->vy) * 0.3;
        }
        q = p->state == ST_JUMP    ? pose_hop(p->vy < 0)
          : p->state == ST_STARTLE ? pose_startle(p->look)
          : pose_air(p->vy < 0);
        if (p->y >= gy) {
            int hard = p->vy > 20 * u;
            p->y = gy;
            p->vy = 0;
            p->thrown = 0;
            set_state(p, ST_LAND, hard ? 10 : 5);
            q = pose_land(hard);
        }
        break;

    case ST_LAND:
        p->vx *= 0.65;
        p->x += p->vx;
        clamp_walls(p, 0);
        q = pose_land(p->dur > 5);
        if (++p->t >= p->dur) {
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
                    p->perch = s->base - (p->y + FOOT_Y * k);
                    p->perch_until = GetTickCount64() + (ULONGLONG)rr(p, 60, 180) * 1000;
                    stairs_crumble(p);
                    say(p, G_HEART, C_HEART);
                    set_state(p, ST_IDLE, rr(p, 40, 90));
                }
            }
        }
        break;
    }

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
                p->x += p->dir * 5 * u;
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
    p->pose = q;

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
    draw_clawd(c, SPR_OX + p->shake, SPR_OY, &p->pose);
    for (int i = 0; i < MAX_BUBBLES; i++) {
        const Bubble *b = &p->bub[i];
        if (b->life <= 0) continue;
        int fade = b->max < 30 ? b->max / 3 : 15;
        int alpha = b->life < fade ? b->life * 255 / fade : 255;
        draw_glyph(c, (int)floor(b->x), (int)floor(b->y), b->glyph, b->color, alpha);
    }
}
