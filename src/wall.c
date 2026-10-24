#include "wall.h"
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "pet.h"

#define WALL_CLASS L"ClawdWall"
#define DRAW_CLASS L"ClawdWallDraw"
#define REG_KEY    L"Software\\ClawdPet"

enum { IDM_WALL_REMOVE = 1, IDM_WALL_CLEAR };

typedef struct {
    HWND hwnd;
    RECT r;   // screen px
    int k;    // logical pixel size it was drawn at
} Wall;

static HINSTANCE g_inst;
static HWND g_owner;
static Wall g_walls[MAX_WALLS];
static int g_n;
static HMONITOR g_hidden_on;

// while drawing
static HWND g_canvas;    // all but invisible, over every screen, catching the mouse
static Wall g_preview;
static int g_draw_k, g_down;
static POINT g_from;
static void (*g_done)(void);

static HWND make_window(int click_through)
{
    return CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                           (click_through ? WS_EX_TRANSPARENT : 0),
                           WALL_CLASS, L"", WS_POPUP, 0, 0, 1, 1, NULL, NULL, g_inst, NULL);
}

static void paint(HWND hwnd, RECT r, int k, BYTE alpha)
{
    int lw = (r.right - r.left) / k, lh = (r.bottom - r.top) / k;
    uint32_t *lp = malloc((size_t)lw * lh * 4);
    Canvas c = { lp, lw, lh };
    draw_wall(&c);

    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = lw * k;
    bi.bmiHeader.biHeight = -lh * k;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    uint32_t *bits;
    HBITMAP dib = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    HDC dc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(dc, dib);
    cv_scale_premul(&c, bits, k);

    SIZE sz = { lw * k, lh * k };
    POINT src = { 0, 0 }, pos = { r.left, r.top };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd, NULL, &pos, &sz, dc, &src, 0, &bf, ULW_ALPHA);

    SelectObject(dc, old);
    DeleteDC(dc);
    DeleteObject(dib);
    free(lp);
}

static void show_wall(const Wall *w)
{
    int hide = g_hidden_on && MonitorFromRect(&w->r, MONITOR_DEFAULTTONEAREST) == g_hidden_on;
    ShowWindow(w->hwnd, hide ? SW_HIDE : SW_SHOWNOACTIVATE);
}

// Tells the pets, and remembers the walls for next time.
static void publish(void)
{
    RECT r[MAX_WALLS];
    wchar_t text[MAX_WALLS * 64] = L"";
    size_t len = 0;
    for (int i = 0; i < g_n; i++) {
        const RECT *q = &g_walls[i].r;
        r[i] = *q;
        len += (size_t)_snwprintf(text + len, 64, L"%ld,%ld,%ld,%ld,%d;", q->left, q->top, q->right, q->bottom,
                                  g_walls[i].k);
    }
    pet_set_walls(r, g_n);
    RegSetKeyValueW(HKEY_CURRENT_USER, REG_KEY, L"Walls", REG_SZ, text, (DWORD)((len + 1) * sizeof(wchar_t)));
}

static int valid(RECT r, int k)
{
    int w = r.right - r.left, h = r.bottom - r.top, len = w > h ? w : h, t = w > h ? h : w;
    return k >= 1 && k <= 32 && w % k == 0 && h % k == 0 && t == WALL_T * k &&
           len >= WALL_MIN * k && len <= WALL_MAX * k && MonitorFromRect(&r, MONITOR_DEFAULTTONULL);
}

static void add(RECT r, int k)
{
    if (g_n >= MAX_WALLS || !valid(r, k)) return;
    Wall *w = &g_walls[g_n++];
    w->r = r;
    w->k = k;
    w->hwnd = make_window(0);
    paint(w->hwnd, r, k, 255);
    show_wall(w);
}

static void remove_wall(int i)
{
    DestroyWindow(g_walls[i].hwnd);
    memmove(&g_walls[i], &g_walls[i + 1], sizeof g_walls[0] * (size_t)(g_n - i - 1));
    g_n--;
}

static int find(HWND hwnd)
{
    for (int i = 0; i < g_n; i++)
        if (g_walls[i].hwnd == hwnd) return i;
    return -1;
}

static LRESULT CALLBACK wall_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_RBUTTONUP && find(hwnd) >= 0) {
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING, IDM_WALL_REMOVE, L"拆掉这面墙");
        AppendMenuW(m, MF_STRING, IDM_WALL_CLEAR, L"拆掉所有围墙");
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(g_owner);
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g_owner, NULL);
        PostMessageW(g_owner, WM_NULL, 0, 0);
        DestroyMenu(m);
        int i = find(hwnd);   // still there after the menu?
        if (cmd == IDM_WALL_REMOVE && i >= 0) {
            remove_wall(i);
            publish();
        } else if (cmd == IDM_WALL_CLEAR) {
            walls_clear();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- drawing ---------------------------------------------------------- */

// The wall a drag from a to b makes: along whichever way it moved more,
// starting at a and centred on the line of the drag.
static int span(POINT a, POINT b, int k, RECT *r)
{
    int dx = b.x - a.x, dy = b.y - a.y, horiz = abs(dx) >= abs(dy);
    int len = (horiz ? abs(dx) : abs(dy)) / k, t = WALL_T * k;
    if (len < WALL_MIN) return 0;
    if (len > WALL_MAX) len = WALL_MAX;
    len *= k;
    if (horiz) {
        r->left = dx >= 0 ? a.x : a.x - len;
        r->top = a.y - t / 2;
        r->right = r->left + len;
        r->bottom = r->top + t;
    } else {
        r->top = dy >= 0 ? a.y : a.y - len;
        r->left = a.x - t / 2;
        r->right = r->left + t;
        r->bottom = r->top + len;
    }
    return 1;
}

// Screen point of a mouse message to the canvas.
static POINT at(HWND hwnd, LPARAM lp)
{
    POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    ClientToScreen(hwnd, &pt);
    return pt;
}

static void preview(POINT c)
{
    RECT r;
    if (!span(g_from, c, g_draw_k, &r)) {
        if (g_preview.hwnd) ShowWindow(g_preview.hwnd, SW_HIDE);
        return;
    }
    if (!g_preview.hwnd) g_preview.hwnd = make_window(1);
    if (!EqualRect(&r, &g_preview.r)) {
        g_preview.r = r;
        paint(g_preview.hwnd, r, g_draw_k, 170);   // see-through until it is placed
    }
    ShowWindow(g_preview.hwnd, SW_SHOWNOACTIVATE);
}

static void finish(void)
{
    if (!g_canvas) return;
    HWND c = g_canvas;
    g_canvas = NULL;
    g_down = 0;
    if (g_preview.hwnd) DestroyWindow(g_preview.hwnd);
    memset(&g_preview, 0, sizeof g_preview);
    DestroyWindow(c);
    if (g_done) g_done();
}

static LRESULT CALLBACK canvas_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
        g_down = 1;
        g_from = at(hwnd, lp);
        SetCapture(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        if (g_down) preview(at(hwnd, lp));
        return 0;

    case WM_LBUTTONUP: {
        if (!g_down) return 0;
        g_down = 0;
        ReleaseCapture();
        RECT r;
        if (span(g_from, at(hwnd, lp), g_draw_k, &r)) {
            add(r, g_draw_k);
            publish();
            finish();
        } else if (g_preview.hwnd) {   // too short to be a wall: try again
            ShowWindow(g_preview.hwnd, SW_HIDE);
        }
        return 0;
    }

    case WM_RBUTTONUP:
        finish();
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) finish();
        return 0;

    case WM_ACTIVATE:   // switched away: stop drawing
        if (LOWORD(wp) == WA_INACTIVE) finish();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- public ----------------------------------------------------------- */

void walls_register(HINSTANCE inst, HWND owner)
{
    g_inst = inst;
    g_owner = owner;
    WNDCLASSW wc = { 0 };
    wc.hInstance = inst;
    wc.lpfnWndProc = wall_proc;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = WALL_CLASS;
    RegisterClassW(&wc);
    wc.lpfnWndProc = canvas_proc;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_CROSS);
    wc.lpszClassName = DRAW_CLASS;
    RegisterClassW(&wc);
}

void walls_load(void)
{
    DWORD sz = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, REG_KEY, L"Walls", RRF_RT_REG_SZ, NULL, NULL, &sz) != ERROR_SUCCESS) return;
    wchar_t *text = calloc(sz / sizeof(wchar_t) + 1, sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, REG_KEY, L"Walls", RRF_RT_REG_SZ, NULL, text, &sz) == ERROR_SUCCESS) {
        const wchar_t *s = text;
        RECT r;
        int k, used;
        while (used = 0, swscanf(s, L"%ld,%ld,%ld,%ld,%d;%n", &r.left, &r.top, &r.right, &r.bottom, &k, &used) == 5 &&
               used) {
            add(r, k);   // skips walls on a screen that is gone
            s += used;
        }
    }
    free(text);
    publish();
}

int walls_count(void)
{
    return g_n;
}

int walls_drawing(void)
{
    return g_canvas != NULL;
}

void walls_draw(int scale, void (*done)(void))
{
    if (g_canvas || g_n >= MAX_WALLS) return;
    g_draw_k = scale;
    g_done = done;
    g_down = 0;
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    g_canvas = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, DRAW_CLASS, L"", WS_POPUP,
                               x, y, w, h, NULL, NULL, g_inst, NULL);
    SetLayeredWindowAttributes(g_canvas, 0, 1, LWA_ALPHA);   // invisible, but it still gets the mouse
    ShowWindow(g_canvas, SW_SHOW);
    SetForegroundWindow(g_canvas);
}

void walls_clear(void)
{
    while (g_n) remove_wall(g_n - 1);
    publish();
}

void walls_raise(void)
{
    for (int i = 0; i < g_n; i++)
        SetWindowPos(g_walls[i].hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void walls_hide_on(HMONITOR m)
{
    if (m == g_hidden_on) return;
    g_hidden_on = m;
    for (int i = 0; i < g_n; i++) show_wall(&g_walls[i]);
}
