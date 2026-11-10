#pragma once
#include <windows.h>

// A speech bubble with real (CJK-capable) text, in its own layered window
// that floats above a pet. Click it to dismiss.
//
// What it says is of some `kind`. Kind 0 is said once: its time runs out
// whether anyone saw it or not. Any other kind (the caller's own numbers) is
// a reminder, which is not lost: its time stands still while the bubble is
// out of sight, and it comes back after a newer message that cut it short.
typedef struct {
    HWND hwnd;
    wchar_t text[128];
    ULONGLONG until;   // GetTickCount64 deadline, 0 = not showing
    int kind;
    DWORD paused;      // a reminder out of sight: the ms it still has, 0 = running
    wchar_t back[128]; // the reminder a newer message cut short, to be shown after it
    int back_kind;     // 0 = none
    DWORD back_ms;
    int shown;         // window currently visible
    int tail;          // tail column (in border units) of the drawn bitmap, -1 = redraw
    int w, h, u;       // window size and border unit, valid after drawing
    POINT pos;         // last window position
} TextBubble;

void tb_register(HINSTANCE inst);
void tb_show(TextBubble *b, const wchar_t *text, DWORD ms, int kind);
void tb_hide(TextBubble *b);              // done with what it says now
void tb_drop(TextBubble *b, int kind);    // takes back a reminder, showing or waiting
void tb_suspend(TextBubble *b);           // out of sight for now (call it for as long as it is)
// anchor: screen point the tail should point at; area: rectangle to stay inside.
void tb_update(TextBubble *b, POINT anchor, RECT area, UINT dpi);
void tb_destroy(TextBubble *b);
