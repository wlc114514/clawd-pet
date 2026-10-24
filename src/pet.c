#include "pet.h"
#include <math.h>

#define SLEEP_AFTER_MS (5 * 60 * 1000)
#define LEAVE_SHOCK_TICKS 24

#define C_HEART 0xFF5A78u
#define C_EXCL  0xFFD23Fu
#define C_WHITE 0xFFFFFFu
#define C_DROP  0x5AB4FFu

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
static double ground_y(const Pet *p) { return p->area.bottom - FOOT_Y * p->scale; }
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
        pose_shock(0), pose_shock(8), pose_shock(14),
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
    if (p->state == ST_DRAG || p->state == ST_LEAVE) return;
    p->forced_sleep = 1;
    set_state(p, ST_SLEEP, 0);
}

void pet_leave(Pet *p)
{
    p->forced_sleep = 0;
    p->following = 0;
    p->paused = 0;
    p->vx = 0;
    p->vy = -5 * unit(p);
    say(p, G_EXCL, C_EXCL);
    set_state(p, ST_LEAVE, 0);
}

void pet_update(Pet *p, const World *w)
{
    double u = unit(p);
    refresh_area(p);
    double gy = ground_y(p);

    // Grounded states stick to the floor; if the floor moved away, fall.
    if (p->state != ST_DRAG && p->state != ST_FALL && p->state != ST_JUMP && p->state != ST_LEAVE) {
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
    double dym = p->area.bottom - w->cursor.y;
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
        p->vy += 1.0 * u;
        if (p->vy > 40 * u) p->vy = 40 * u;
        p->x += p->vx;
        p->y += p->vy;
        clamp_walls(p, 0.6);
        if (p->y < p->area.top - SPR_OY * p->scale) {
            p->y = p->area.top - SPR_OY * p->scale;
            p->vy = fabs(p->vy) * 0.3;
        }
        q = p->state == ST_JUMP ? pose_hop(p->vy < 0) : pose_air(p->vy < 0);
        if (p->y >= gy) {
            int hard = p->vy > 20 * u;
            p->y = gy;
            p->vy = 0;
            set_state(p, ST_LAND, hard ? 10 : 5);
            q = pose_land(hard);
        }
        break;

    case ST_LAND:
        p->vx *= 0.8;
        p->x += p->vx;
        clamp_walls(p, 0);
        q = pose_land(p->dur > 5);
        if (++p->t >= p->dur) {
            p->vx = 0;
            set_state(p, ST_IDLE, rr(p, 40, 120));
        }
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

    case ST_LEAVE:
        p->vy += 1.0 * u;
        p->y += p->vy;
        if (p->t < LEAVE_SHOCK_TICKS) {
            // Startled on the spot (pet_leave gave it a little jolt)...
            if (p->y >= gy) {
                p->y = gy;
                p->vy = 0;
            }
            if (p->t == 10) say(p, G_QUEST, C_WHITE);
            q = pose_shock(p->t);
        } else {
            // ...then the taskbar gives way and down it goes.
            q = pose_dangle((p->t / 3) % 2);
            if (p->y > p->screen.bottom) p->gone = 1;
        }
        p->t++;
        break;
    }
    p->pose = q;

    for (int i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &p->bub[i];
        if (b->life <= 0) continue;
        b->x += b->vx;
        b->y += b->vy;
        b->life--;
    }
}

void pet_render(const Pet *p, Canvas *c)
{
    cv_clear(c);
    draw_clawd(c, SPR_OX, SPR_OY, &p->pose);
    for (int i = 0; i < MAX_BUBBLES; i++) {
        const Bubble *b = &p->bub[i];
        if (b->life <= 0) continue;
        int alpha = b->life < 15 ? b->life * 255 / 15 : 255;
        draw_glyph(c, (int)floor(b->x), (int)floor(b->y), b->glyph, b->color, alpha);
    }
}
