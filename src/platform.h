#pragma once

/*
 * The core of the pet (pet.c, render.c) is plain C that talks to the OS
 * through the handful of types and calls below, so the same state machine
 * and physics run on Windows and on macOS.
 *
 * On Windows these are thin wrappers over Win32 (src/platform_win.c); on
 * macOS they live in mac/plat_mac.m.
 */
#ifdef _WIN32

#include <windows.h>

#else

#include <stdint.h>

typedef int32_t LONG;
typedef uint32_t DWORD;
typedef uint64_t ULONGLONG;

typedef struct { LONG x, y; } POINT;
typedef struct { LONG left, top, right, bottom; } RECT;

#endif

// How long the user has been away from the keyboard and mouse, in ms.
DWORD plat_idle_ms(void);

// Milliseconds since boot.
ULONGLONG plat_ticks_ms(void);

void plat_cursor(POINT *out);

// Work area (taskbar / Dock and menu bar excluded) and the whole frame of
// the screen holding pt. Points and rectangles are in top-left screen
// coordinates: x to the right, y down, (0, 0) at the top-left corner of the
// union of all screens.
void plat_screen_rects(POINT pt, RECT *work, RECT *frame);

// The same, for the main screen.
void plat_work_area(RECT *work, RECT *frame);
