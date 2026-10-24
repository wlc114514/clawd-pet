#pragma once
#include <windows.h>

// Pomodoro time picker: a small pixel-art window above a pet with two
// wheels (focus / break minutes) and a start button. Scroll, drag or click
// a wheel to change it; arrow keys, Enter and Esc work too. Clicking
// anywhere else closes it. Only one is open at a time.
typedef void (*PickerDone)(int focus_min, int break_min);

#define FOCUS_LO 5    // minutes, in steps of FOCUS_STEP
#define FOCUS_HI 90
#define FOCUS_STEP 5
#define BREAK_LO 1
#define BREAK_HI 30

void picker_register(HINSTANCE inst);
// anchor: screen point the tail should point at; area: rectangle to stay inside.
void picker_open(POINT anchor, RECT area, UINT dpi, int focus_min, int break_min, PickerDone done);
