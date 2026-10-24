#pragma once
#include <windows.h>

// Brick walls the user draws on the screen (menu: 放置围墙). Each wall is a
// window of its own that stays until it is right-clicked away; they are
// saved in the registry and handed to the pets with pet_set_walls().
void walls_register(HINSTANCE inst, HWND owner);   // owner: hosts the right-click menu
void walls_load(void);
int  walls_count(void);
int  walls_drawing(void);
// Turns the screen into a canvas until one wall is drawn (drag), or Esc /
// right-click; then calls done. scale: logical pixel size for the bricks.
void walls_draw(int scale, void (*done)(void));
void walls_clear(void);
void walls_raise(void);                // back above the taskbar (pets go above walls)
void walls_hide_on(HMONITOR m);        // hide those on a fullscreen monitor; NULL shows all
