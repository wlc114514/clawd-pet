#pragma once
#include "platform.h"
#include "render.h"

enum {
    ST_IDLE, ST_WALK, ST_DRAG, ST_FALL, ST_LAND, ST_JUMP, ST_SLEEP, ST_WAKE,
    ST_TYPE,   // sitting at a keyboard (user typing / Claude Code working)
    ST_WAVE,   // Claude Code needs the user
    ST_FLEE,   // running away from the cursor
    ST_LEAVE,  // being sent away: a reaction (see LEAVE_*), then drops through the taskbar
    ST_STARTLE,// cartoon leap backwards, freezing at the top
    ST_CLIMB,  // hopping up a staircase that pops out of the taskbar
    ST_BUMP,   // walked into a wall: splat, stagger back, see stars
    ST_FLIP,   // backflip: crouch, then a full turn backwards in the air
    ST_TO_DESK,// focus mode: walking over to its seat (Pet.seat_x)
    ST_DESK,   // focus mode: sitting at a school desk, reading or writing (the typist: on a laptop)
    // Doing something with another pet (Pet.buddy); see pets_together().
    ST_GREET,  // face to face, waving
    ST_CHAT,   // taking turns to say something in pixel emoji
    ST_CHASE,  // tag: chasing (role 1) or running away (role 0)
    ST_FOLLOW, // walking in a line behind its buddy
    ST_GOTO,   // walking over to its buddy, to do `role` (GO_*) when there
    ST_RIDE,   // standing on its buddy's head (role 0: hopping up)
    ST_COMFORT,// patting a dazed buddy
    ST_YAWN,   // a sleeping buddy is catching
    ST_GOODBYE,// waving a buddy off as it leaves
};
enum { GO_RIDE, GO_COMFORT, GO_SLEEP, GO_GREET };
// Games that can be asked for (pets_play).
enum { PLAY_GREET, PLAY_CHASE, PLAY_LINE, PLAY_RIDE, PLAY_DAZE, PLAY_KINDS };
enum { MOUSE_OFF, MOUSE_FOLLOW, MOUSE_SHY };
enum { LEAVE_SHOCK, LEAVE_SHIVER, LEAVE_CRY, LEAVE_PANIC, LEAVE_KINDS };
enum { CLAUDE_IDLE, CLAUDE_WORKING, CLAUDE_WAITING };
enum { SEAT_NONE, SEAT_AT, SEAT_HERE };   // Pet.seat: none yet; walk to seat_x; sit wherever it is

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
    int focus;         // focus mode: everyone sits quietly at a desk
} World;

typedef struct {
    int glyph, life, max;
    double x, y, vx, vy;   // canvas coordinates
    uint32_t color;
    double ay;             // gravity
    int framed;            // in a white speech bubble
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

// Brick walls the user draws on the screen, in logical pixels. A wall is
// solid: the pet bumps into its sides and the bottom, and can stand on top.
#define WALL_T    7      // thickness
#define WALL_MIN  12     // shortest wall
#define WALL_MAX  1200   // longest wall
#define MAX_WALLS 32

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
    int typist;            // sits down to type along with the user / Claude Code (only the first pet)
    int leave_kind;        // LEAVE_*, picked at random when sent away
    int shake;             // sprite x offset this tick (shivering)
    int gone;              // finished leaving; the window can be destroyed
    int dropped;           // event: just fell through the taskbar (cleared by the caller)
    int thrown;            // flying after being let go mid-drag (gets air drag)
    int bonked;            // hit a wall in the air: dazed once it lands (+1 / -1 its side, 2 above)
    int bump_side;         // ST_BUMP: which side the wall is on (0: it was overhead)
    double spin;           // drawn turned by this many radians (backflip), 0 = upright
    int fan, fan_side, fan_t;   // desk fan out (too hot); which side (-1 left / +1 right); spin ticks
    int tap_t, tap_side;
    Stairs stairs;
    int climb_i, climb_ph;  // step being climbed to; 0 wait, 1 hop, 2 land
    double hop_x, hop_y;    // where the current hop started
    double perch;           // floor height above the taskbar, px (0 = on the taskbar)
    ULONGLONG perch_until;  // when to jump back down
    int id;                 // unique, so pets can refer to each other
    int buddy;              // id of the pet it is doing something with, 0 = none
    int role, lead, count;  // what the shared activity needs (see the states)
    double goal_x;          // where it is heading: screen x of the middle of the sprite
    int carrying;           // id of the pet standing on its head, 0 = none
    ULONGLONG social_at;    // no new games before this
    unsigned char wear[SLOTS];  // ACC_* per slot, rolled at random in pet_init
    int desk;               // has its desk out (focus mode), even while being carried about with it
    int desk_job;           // what it does there: PROP_BOOK or PROP_HOMEWORK
    int seat;               // SEAT_*, handed out by pets_together() in focus mode
    double seat_x;          // where: screen x of the middle of the sprite
    unsigned age;           // ticks since pet_init
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
void pet_flip(Pet *p);
void pet_sleep(Pet *p);
void pet_leave(Pet *p);
void pet_startle(Pet *p, double from_x);   // something scary happened at screen x from_x
void pet_dress(Pet *p, const unsigned char *wear);   // new outfit (SLOTS entries)
int  pet_can_climb(const Pet *p);
void pet_climb(Pet *p);
// Renders the stairs (if any) in STAIRS_* style into c, which must hold
// STAIRS_CW x STAIRS_CH; sets c->w, c->h and the screen position of the
// top-left corner.
int  pet_stairs(const Pet *p, int style, Canvas *c, POINT *origin);
// The walls every pet collides with, as rectangles in screen pixels.
void pet_set_walls(const RECT *walls, int n);
POINT pet_head(const Pet *p);   // screen point just above the head
// Everything between pets: meeting, games, comforting, ... Call once a tick
// after pet_update() for each of them, with the pets that are on screen.
void pets_together(Pet **pets, int n, const World *w);
// Starts PLAY_* right now (the menu), with `who` leading (NULL: whoever is
// free) and the nearest free pet joining in. 0 if nobody could.
int  pets_play(Pet **pets, int n, Pet *who, int what);

Pose pose_stand(void);
int  pet_demo_poses(Pose *out, int max);
