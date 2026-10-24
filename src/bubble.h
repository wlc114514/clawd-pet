#pragma once
#include <windows.h>

// A speech bubble with real (CJK-capable) text, in its own layered window
// that floats above a pet. Click it to dismiss.
typedef struct {
    HWND hwnd;
    wchar_t text[128];
    ULONGLONG until;   // GetTickCount64 deadline, 0 = not showing
    int shown;         // window currently visible
    int tail;          // tail column (in border units) of the drawn bitmap, -1 = redraw
    int w, h, u;       // window size and border unit, valid after drawing
    POINT pos;         // last window position
} TextBubble;

void tb_register(HINSTANCE inst);
void tb_show(TextBubble *b, const wchar_t *text, DWORD ms);
void tb_hide(TextBubble *b);
void tb_suspend(TextBubble *b);   // hide the window but keep the message
// anchor: screen point the tail should point at; area: rectangle to stay inside.
void tb_update(TextBubble *b, POINT anchor, RECT area, UINT dpi);
void tb_destroy(TextBubble *b);
