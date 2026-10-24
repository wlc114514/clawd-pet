#include "picker.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PK_CLASS   L"ClawdPicker"
#define COL_INK    0xFF2B2B2Bu
#define COL_PAPER  0xFFFFFFFFu
#define COL_DIM    0xFFA8A8A8u
#define COL_LINE   0xFFE6E1D6u
#define COL_ACCENT 0xFFD77757u
#define COL_HOT    0xFFC86A4Bu
#define COL_PRESS  0xFFB25A3Cu

/*
 * Layout at 96 dpi (scaled by D()):
 *
 *     .BBBBBBBBBBBBBBBBBBBB.
 *     B   专注        休息   B     labels      y 10..28
 *     B    20          4    B     previous    y 32..56
 *     B  ──────      ────── B
 *     B   25 分        5 分  B     current     y 58..86
 *     B  ──────      ────── B
 *     B    30          6    B     next        y 88..112
 *     B  [   开始专注    ]   B     button      y 120..146
 *     .BBBBBBBB.BBBBBBBBBBBB.
 *              B.B
 *               B
 */
#define PK_W     184
#define PK_BH    156
#define ROW_PREV 44
#define ROW_CUR  72
#define ROW_NEXT 100
#define LINE_TOP 58
#define LINE_BOT 86
#define BTN_TOP  120
#define BTN_BOT  146

typedef struct { const wchar_t *label; int lo, hi, step, val; } Wheel;

static struct {
    HWND hwnd;
    PickerDone done;
    Wheel wh[2];
    int sel;                  // wheel the arrow keys control
    UINT dpi;
    int u, W, H, tail;        // border unit, window size, tail column (units)
    int hot, pressed;         // mouse over / pressing the button
    int drag, drag_y, moved;  // dragging a wheel
    int wheel_acc;
} pk;

static int D(int v)
{
    return MulDiv(v, (int)pk.dpi, 96);
}

static void fill(uint32_t *px, int x0, int y0, int x1, int y1, uint32_t col)
{
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < pk.H; y++)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < pk.W; x++) px[y * pk.W + x] = col;
}

// Chamfered box with a one-unit border, like the text bubbles.
static void box(uint32_t *px, int x0, int y0, int x1, int y1, uint32_t border, uint32_t inside)
{
    int u = pk.u;
    fill(px, x0 + u, y0, x1 - u, y0 + u, border);
    fill(px, x0 + u, y1 - u, x1 - u, y1, border);
    fill(px, x0, y0 + u, x0 + u, y1 - u, border);
    fill(px, x1 - u, y0 + u, x1, y1 - u, border);
    fill(px, x0 + u, y0 + u, x1 - u, y1 - u, inside);
}

static int snap(int v)
{
    return v / pk.u * pk.u;
}

static int col_x(int c)
{
    return c ? pk.W * 3 / 4 - pk.u : pk.W / 4 + pk.u;
}

static int in_button(int x, int y)
{
    return x >= snap(D(16)) && x < pk.W - snap(D(16)) && y >= snap(D(BTN_TOP)) && y < snap(D(BTN_BOT));
}

static HFONT font(int size, int weight)
{
    return CreateFontW(-D(size), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       0, 0, ANTIALIASED_QUALITY, 0, L"Microsoft YaHei UI");
}

// Draws s with its left edge at x (or centred on x), vertically centred on cy.
// Returns the width.
static int text(HDC dc, const wchar_t *s, int size, int weight, uint32_t col, int x, int cy, int center)
{
    HFONT f = font(size, weight), of = SelectObject(dc, f);
    int n = (int)wcslen(s);
    SIZE sz;
    TEXTMETRICW tm;
    GetTextExtentPoint32W(dc, s, n, &sz);
    GetTextMetricsW(dc, &tm);
    SetTextColor(dc, RGB((col >> 16) & 255, (col >> 8) & 255, col & 255));
    if (center) x -= sz.cx / 2;
    TextOutW(dc, x, cy + (tm.tmAscent - tm.tmInternalLeading) / 2, s, n);
    SelectObject(dc, of);
    DeleteObject(f);
    return sz.cx;
}

static void num_text(HDC dc, int v, int size, int weight, uint32_t col, int cx, int cy)
{
    wchar_t s[8];
    _snwprintf(s, 8, L"%d", v);
    text(dc, s, size, weight, col, cx, cy, 1);
}

static void draw(POINT *pos)
{
    int u = pk.u, W = pk.W, H = pk.H, BH = H - 2 * u;
    HDC dc = CreateCompatibleDC(NULL);
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    uint32_t *px;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0), obmp = SelectObject(dc, bmp);
    memset(px, 0, (size_t)W * H * 4);

    box(px, 0, 0, W, BH, COL_INK, COL_PAPER);
    int t = pk.tail * u;
    fill(px, t, BH - u, t + u, BH, COL_PAPER);
    fill(px, t - u, BH, t, BH + u, COL_INK);
    fill(px, t, BH, t + u, BH + u, COL_PAPER);
    fill(px, t + u, BH, t + 2 * u, BH + u, COL_INK);
    fill(px, t, BH + u, t + u, BH + 2 * u, COL_INK);

    for (int c = 0; c < 2; c++) {
        int cx = col_x(c), half = snap(D(32));
        uint32_t lc = c == pk.sel ? COL_ACCENT : COL_LINE;
        fill(px, cx - half, snap(D(LINE_TOP)), cx + half, snap(D(LINE_TOP)) + u, lc);
        fill(px, cx - half, snap(D(LINE_BOT)), cx + half, snap(D(LINE_BOT)) + u, lc);
    }

    uint32_t bc = pk.pressed && pk.hot ? COL_PRESS : pk.hot ? COL_HOT : COL_ACCENT;
    box(px, snap(D(16)), snap(D(BTN_TOP)), W - snap(D(16)), snap(D(BTN_BOT)), COL_INK, bc);

    // GDI text on top. It blends with the opaque pixels below but zeroes
    // their alpha, which is put back afterwards.
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_BASELINE | TA_LEFT);
    for (int c = 0; c < 2; c++) {
        const Wheel *w = &pk.wh[c];
        int cx = col_x(c);
        text(dc, w->label, 13, FW_SEMIBOLD, COL_INK, cx, D(19), 1);
        if (w->val - w->step >= w->lo) num_text(dc, w->val - w->step, 15, FW_NORMAL, COL_DIM, cx, D(ROW_PREV));
        if (w->val + w->step <= w->hi) num_text(dc, w->val + w->step, 15, FW_NORMAL, COL_DIM, cx, D(ROW_NEXT));

        // The number stays centred; "分" hangs off its right side.
        wchar_t s[8];
        _snwprintf(s, 8, L"%d", w->val);
        HFONT f = font(24, FW_BOLD), of = SelectObject(dc, f);
        SIZE sz;
        GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
        SelectObject(dc, of);
        DeleteObject(f);
        text(dc, s, 24, FW_BOLD, COL_ACCENT, cx, D(ROW_CUR), 1);
        text(dc, L"分", 12, FW_NORMAL, COL_DIM, cx + sz.cx / 2 + D(3), D(ROW_CUR) + D(3), 0);
    }
    text(dc, L"开始专注", 14, FW_BOLD, COL_PAPER, W / 2, (snap(D(BTN_TOP)) + snap(D(BTN_BOT))) / 2, 1);
    GdiFlush();
    for (int i = 0; i < W * H; i++)
        if (px[i] && !(px[i] >> 24)) px[i] |= 0xFF000000u;

    SIZE size = { W, H };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(pk.hwnd, NULL, pos, &size, dc, &src, 0, &bf, ULW_ALPHA);
    SelectObject(dc, obmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

static void redraw(void)
{
    draw(NULL);
}

static void step(int c, int n)
{
    Wheel *w = &pk.wh[c];
    int v = w->val + n * w->step;
    if (v < w->lo) v = w->lo;
    if (v > w->hi) v = w->hi;
    pk.sel = c;
    w->val = v;
    redraw();
}

static void start(void)
{
    PickerDone done = pk.done;
    int f = pk.wh[0].val, b = pk.wh[1].val;
    DestroyWindow(pk.hwnd);
    if (done) done(f, b);
}

static LRESULT CALLBACK pk_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    switch (msg) {
    case WM_MOUSEWHEEL: {
        POINT p = { x, y };
        ScreenToClient(hwnd, &p);
        int c = p.x >= pk.W / 2;
        if (c != pk.sel) pk.wheel_acc = 0;
        pk.sel = c;
        pk.wheel_acc += GET_WHEEL_DELTA_WPARAM(wp);
        int n = 0;
        for (; pk.wheel_acc >= WHEEL_DELTA; pk.wheel_acc -= WHEEL_DELTA) n--;   // away from you: smaller
        for (; pk.wheel_acc <= -WHEEL_DELTA; pk.wheel_acc += WHEEL_DELTA) n++;
        step(c, n);
        return 0;
    }

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        if (in_button(x, y)) {
            pk.pressed = 1;
        } else if (y >= D(32) && y < D(114)) {
            pk.drag = 1;
            pk.drag_y = y;
            pk.moved = 0;
            pk.sel = x >= pk.W / 2;
        }
        redraw();
        return 0;

    case WM_MOUSEMOVE: {
        int hot = in_button(x, y);
        if (hot != pk.hot) {
            pk.hot = hot;
            TRACKMOUSEEVENT te = { sizeof te, TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&te);
            redraw();
        }
        if (pk.drag) {   // drag up for more, like pulling the next number into view
            int n = (pk.drag_y - y) / D(16);
            if (n) {
                pk.drag_y -= n * D(16);
                pk.moved = 1;
                step(pk.sel, n);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        pk.hot = 0;
        redraw();
        return 0;

    case WM_LBUTTONUP: {
        int was_pressed = pk.pressed, was_drag = pk.drag;
        pk.pressed = pk.drag = 0;
        ReleaseCapture();
        if (was_pressed && in_button(x, y)) {
            start();
            return 0;
        }
        if (was_drag && !pk.moved) {   // click a neighbour to step to it
            if (y < D(LINE_TOP)) step(pk.sel, -1);
            else if (y > D(LINE_BOT)) step(pk.sel, 1);
        }
        redraw();
        return 0;
    }

    case WM_KEYDOWN:
        switch (wp) {
        case VK_UP:     step(pk.sel, -1); break;
        case VK_DOWN:   step(pk.sel, 1); break;
        case VK_LEFT:
        case VK_RIGHT:
        case VK_TAB:    pk.sel = !pk.sel; redraw(); break;
        case VK_RETURN: start(); break;
        case VK_ESCAPE: DestroyWindow(hwnd); break;
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;

    case WM_DESTROY:
        if (pk.hwnd == hwnd) pk.hwnd = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void picker_register(HINSTANCE inst)
{
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = pk_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    wc.lpszClassName = PK_CLASS;
    RegisterClassW(&wc);
}

void picker_open(POINT anchor, RECT area, UINT dpi, int focus_min, int break_min, PickerDone done)
{
    if (pk.hwnd) DestroyWindow(pk.hwnd);
    memset(&pk, 0, sizeof pk);
    pk.done = done;
    pk.wh[0] = (Wheel){ L"专注", FOCUS_LO, FOCUS_HI, FOCUS_STEP, focus_min };
    pk.wh[1] = (Wheel){ L"休息", BREAK_LO, BREAK_HI, 1, break_min };
    pk.dpi = dpi;
    pk.u = dpi >= 144 ? 3 : 2;
    pk.W = snap(D(PK_W));
    pk.H = snap(D(PK_BH)) + 2 * pk.u;

    POINT pos = { anchor.x - pk.W / 2, anchor.y - pk.H };
    if (pos.x + pk.W > area.right) pos.x = area.right - pk.W;
    if (pos.x < area.left) pos.x = area.left;
    if (pos.y < area.top) pos.y = area.top;
    pk.tail = (anchor.x - pos.x) / pk.u;
    if (pk.tail < 2) pk.tail = 2;
    if (pk.tail > pk.W / pk.u - 3) pk.tail = pk.W / pk.u - 3;

    pk.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, PK_CLASS, L"番茄钟",
                              WS_POPUP, pos.x, pos.y, pk.W, pk.H, NULL, NULL, GetModuleHandleW(NULL), NULL);
    draw(&pos);
    ShowWindow(pk.hwnd, SW_SHOW);
    SetForegroundWindow(pk.hwnd);
    SetFocus(pk.hwnd);
}
