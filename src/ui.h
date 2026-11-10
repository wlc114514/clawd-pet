#pragma once
#include <windows.h>
#include <stdint.h>

/*
 * What every one of the little pixel-art windows needs (the pets, their
 * bubbles, the picker, the wardrobe, the walls): a 32-bit picture to draw
 * into, the font, and boxes in the bubbles' colours.
 */
#define COL_INK    0xFF2B2B2Bu
#define COL_PAPER  0xFFFFFFFFu
#define COL_DIM    0xFFA8A8A8u
#define COL_LINE   0xFFE6E1D6u
#define COL_ACCENT 0xFFD77757u
#define COL_HOT    0xFFC86A4Bu
#define COL_PRESS  0xFFB25A3Cu

// A top-down 32-bit bitmap, w by h, with its pixels at *bits.
HBITMAP ui_dib(int w, int h, uint32_t **bits);
// The font everything is written in, px high.
HFONT ui_font(int px, int weight);
// Draws s px high with its left edge at x (or centred on x), vertically
// centred on cy. Returns its width.
int ui_text(HDC dc, const wchar_t *s, int px, int weight, uint32_t col, int x, int cy, int center);
// In a picture W by H: a rectangle from (x0, y0) up to but not including
// (x1, y1), clipped to the picture.
void ui_fill(uint32_t *px, int W, int H, int x0, int y0, int x1, int y1, uint32_t col);
// The same with a border u thick and its corners cut off, like the bubbles.
void ui_box(uint32_t *px, int W, int H, int u, int x0, int y0, int x1, int y1, uint32_t border, uint32_t inside);
