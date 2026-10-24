#pragma once
#include <windows.h>
#include "render.h"

enum {
    ST_IDLE, ST_WALK, ST_DRAG, ST_FALL, ST_LAND, ST_JUMP, ST_SLEEP, ST_WAKE,
    ST_TYPE,   // sitting at a keyboard (user typing / Claude Code working)
    ST_WAVE,   // Claude Code needs the user
    ST_FLEE,   // running away from the cursor
    ST_LEAVE,  // being sent away: startled, then drops through the taskbar
};
enum { MOUSE_OFF, MOUSE_FOLLOW, MOUSE_SHY };
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
} World;

typedef struct {
    int glyph, life, max;
    double x, y, vx, vy;   // canvas coordinates
    uint32_t color;
} Bubble;

#define MAX_BUBBLES 8

typedef struct {
    double x, y, vx, vy;   // window top-left, screen px; velocity in px/tick
    int state, t, dur;
    int dir, look, look_t, blink_t;
    int scale;
    int paused, forced_sleep, following;
    int gone;              // finished leaving; the window can be destroyed
    int tap_t, tap_side;
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
POINT pet_head(const Pet *p);   // screen point just above the head

Pose pose_stand(void);
int  pet_demo_poses(Pose *out, int max);
