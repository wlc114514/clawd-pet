#pragma once
#include <stdint.h>

// Logical canvas (one logical pixel = `scale` screen pixels).
// The sprite sits at the bottom; the space above is for bubbles / zzz.
#define CW 30
#define CH 26
#define SPR_OX 6
#define SPR_OY 14
#define FOOT_Y 24   // first canvas row below the feet when standing

#define COL_BODY 0xFFD77757u
#define COL_EYE  0xFF000000u
#define COL_KB   0xFF5C5C5Cu
#define COL_KEY  0xFFC8C8C8u

enum { EYE_OPEN, EYE_CLOSED, EYE_HAPPY, EYE_WIDE, EYE_DIZZY };
// What it has in front of it. The rest are at a school desk (focus mode),
// with the pet sitting up DESK_DY rows behind it.
enum { PROP_NONE, PROP_KEYBOARD, PROP_DESK, PROP_BOOK, PROP_HOMEWORK, PROP_LAPTOP };
#define DESK_DY -4
enum { G_Z, G_ZBIG, G_EXCL, G_QUEST, G_HEART, G_DROP, G_TEAR, G_SOB, G_WIND, G_STAR,
       G_NOTE, G_SMILE, G_COFFEE, G_BULB, G_BOLT, G_COUNT };

// Accessories, at most one per slot (Pose.wear), drawn on top of the sprite.
enum { SLOT_HEAD, SLOT_FACE, SLOT_MOUTH, SLOT_NECK, SLOT_HAND, SLOTS };
enum { ACC_NONE, ACC_BOWLER, ACC_CROWN, ACC_BEANIE, ACC_PHONES,   // head
       ACC_MONOCLE, ACC_GLASSES, ACC_SHADES,                     // face
       ACC_HANDLEBAR, ACC_PENCIL, ACC_WALRUS, ACC_GOATEE,        // mouth
       ACC_SCARF, ACC_BOWTIE,                                    // neck
       ACC_COFFEE, ACC_FRAPPE, ACC_CHIPS, ACC_WATCH,             // hand
       ACC_COUNT };

#define FAN_W 7    // desk fan prop, drawn standing on the floor beside the pet
#define FAN_H 10

typedef struct { uint32_t *px; int w, h; } Canvas;   // straight ARGB

typedef struct {
    int body_dy;      // shift body + arms down (+) / up (-)
    int squash;       // +1 top row lowered, -1 top row raised
    int eyes;         // EYE_*
    int look;         // eye offset -1 / 0 / +1
    int arm_l, arm_r; // vertical arm offset
    int legs[4];      // leg length below the body, 0..3
    int prop;         // PROP_*, drawn in front of the body, behind the arms
    int prop_frame;   // its animation: a page turning, the pencil lifted
    int stretch_x;    // body pulled sideways by n px (+ right, - left); eyes stay put
    unsigned char wear[SLOTS];   // ACC_* in each slot
    int tick;         // clock for the accessories' own animation (steam)
} Pose;

void cv_clear(Canvas *c);
// The sprite with its top-left at (ox, oy), and whatever it wears.
void draw_clawd(Canvas *c, int ox, int oy, const Pose *p);
// Only what it wears, where draw_clawd would put it.
void draw_outfit(Canvas *c, int ox, int oy, const Pose *p);
int  acc_slot(int a);
const char *acc_name(int a);     // UTF-8
const char *acc_slot_name(int s);    // UTF-8
int  acc_above(const unsigned char *wear);   // rows the hat adds above the head
// Dresses at random: each slot has its own chance of getting something.
void outfit_roll(unsigned char *wear, unsigned *rng);
// Fan with its top-left at (x, y); frame alternates the blades (+ / x).
void draw_fan(Canvas *c, int x, int y, int frame);
// Fills all of c (c->w by c->h) with red brick, courses along its longer side.
void draw_wall(Canvas *c);
int  glyph_width(int g);
void draw_glyph(Canvas *c, int x, int y, int g, uint32_t rgb, int alpha);
// The same inside a white speech bubble with its tail at the bottom middle.
void draw_speech(Canvas *c, int x, int y, int g, uint32_t rgb, int alpha);
int  glyph_height(int g);
// Nearest-neighbour upscale by k into dst (c->w*k by c->h*k), premultiplied ARGB.
void cv_scale_premul(const Canvas *c, uint32_t *dst, int k);
