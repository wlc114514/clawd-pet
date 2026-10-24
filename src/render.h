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

enum { EYE_OPEN, EYE_CLOSED, EYE_HAPPY, EYE_WIDE };
enum { PROP_NONE, PROP_KEYBOARD };
enum { G_Z, G_ZBIG, G_EXCL, G_QUEST, G_HEART, G_DROP, G_COUNT };

typedef struct { uint32_t *px; int w, h; } Canvas;   // straight ARGB

typedef struct {
    int body_dy;      // shift body + arms down (+) / up (-)
    int squash;       // +1 top row lowered, -1 top row raised
    int eyes;         // EYE_*
    int look;         // eye offset -1 / 0 / +1
    int arm_l, arm_r; // vertical arm offset
    int legs[4];      // leg length below the body, 0..3
    int prop;         // PROP_*, drawn in front of the body, behind the arms
} Pose;

void cv_clear(Canvas *c);
void draw_clawd(Canvas *c, int ox, int oy, const Pose *p);
int  glyph_width(int g);
void draw_glyph(Canvas *c, int x, int y, int g, uint32_t rgb, int alpha);
// Nearest-neighbour upscale by k into dst (c->w*k by c->h*k), premultiplied ARGB.
void cv_scale_premul(const Canvas *c, uint32_t *dst, int k);
