#pragma once
#include <windows.h>
#include "render.h"

enum {
    ST_IDLE, ST_WALK, ST_DRAG, ST_FALL, ST_LAND, ST_JUMP, ST_SLEEP, ST_WAKE,
    ST_TYPE,   // sitting at a keyboard (user typing / Claude Code working)
    ST_WAVE,   // Claude Code needs the user
    ST_FLEE,   // running away from the cursor
    ST_LEAVE,  // being sent away: a reaction (see LEAVE_*), then drops through the taskbar
    ST_STARTLE,// cartoon leap backwards, freezing at the top
    ST_CLIMB,  // hopping up a staircase that pops out of the taskbar
};
enum { MOUSE_OFF, MOUSE_FOLLOW, MOUSE_SHY };
enum { LEAVE_SHOCK, LEAVE_SHIVER, LEAVE_CRY, LEAVE_PANIC, LEAVE_KINDS };
enum { CLAUDE_IDLE, CLAUDE_WORKING, CLAUDE_WAITING };

// Everything outside the pet that it reacts to, sampled once per tick.
typedef struct {
    POINT cursor;
    DWORD cursor_still_ms;
    int keys;          // keystrokes since the last tick
    int typing;        // the user typed something recently
    double cpu;        // 0..1
    int mouse_mode;    // MOUSE_*
    int cc;            // CC_*
    int climb;         // may climb stairs on its own
    int heat;          // 0 fine, 1 warm: sweats, 2 hot: sweats more and fans itself
} World;

typedef struct {
    int glyph, life, max;
    double x, y, vx, vy;   // canvas coordinates
    uint32_t color;
    double ay;             // gravity
} Bubble;

#define MAX_BUBBLES 16

// Stairs, in logical pixels. Each step is STEP_W wide and STEP_H higher
// than the last; the whole thing is drawn by the caller in its own window.
#define STEP_W    10
#define STEP_H    5
#define MAX_STEPS 12
#define STAIRS_CW (MAX_STEPS * STEP_W + 2)
#define STAIRS_CH (MAX_STEPS * STEP_H + 1)

enum { STAIRS_SLAB, STAIRS_SOLID, STAIRS_OUTLINE, STAIRS_STYLES };

typedef struct {
    int n;                 // steps in this staircase, 0 = none
    int shown;             // steps that have started rising out of the taskbar
    int dir;               // +1 climbs rightwards, -1 leftwards
    int scale;
    double x0, base;       // screen x of the near edge of step 0; screen y of the floor
    double off[MAX_STEPS]; // logical px each step is below its resting place
    double vy[MAX_STEPS];  // falling speed while crumbling
    int crumble_t;         // ticks since it started crumbling, -1 = standing
} Stairs;

typedef struct {
    double x, y, vx, vy;   // window top-left, screen px; velocity in px/tick
    int state, t, dur;
    int dir, look, look_t, blink_t;
    int scale;
    int paused, forced_sleep, following;
    int leave_kind;        // LEAVE_*, picked at random when sent away
    int shake;             // sprite x offset this tick (shivering)
    int gone;              // finished leaving; the window can be destroyed
    int dropped;           // event: just fell through the taskbar (cleared by the caller)
    int thrown;            // flying after being let go mid-drag (gets air drag)
    int tap_t, tap_side;
    Stairs stairs;
    int climb_i, climb_ph;  // step being climbed to; 0 wait, 1 hop, 2 land
    double hop_x, hop_y;    // where the current hop started
    double perch;           // floor height above the taskbar, px (0 = on the taskbar)
    ULONGLONG perch_until;  // when to jump back down
    int drag_dx, drag_dy;
    int zcount;
    RECT area;             // work area of the monitor the pet is on
    RECT screen;           // the whole monitor
    Bubble bub[MAX_BUBBLES];
    unsigned rng;
    Pose pose;
} Pet;

void pet_init(Pet *p, int scale, unsigned seed);
void pet_set_scale(Pet *p, int scale);
void pet_update(Pet *p, const World *w);
void pet_render(const Pet *p, Canvas *c);
void pet_begin_drag(Pet *p, POINT grab);
void pet_end_drag(Pet *p);
void pet_click(Pet *p);
void pet_cheer(Pet *p);
void pet_sleep(Pet *p);
void pet_leave(Pet *p);
void pet_startle(Pet *p, double from_x);   // something scary happened at screen x from_x
int  pet_can_climb(const Pet *p);
void pet_climb(Pet *p);
// Renders the stairs (if any) in STAIRS_* style into c, which must hold
// STAIRS_CW x STAIRS_CH; sets c->w, c->h and the screen position of the
// top-left corner.
int  pet_stairs(const Pet *p, int style, Canvas *c, POINT *origin);
POINT pet_head(const Pet *p);   // screen point just above the head

Pose pose_stand(void);
int  pet_demo_poses(Pose *out, int max);
