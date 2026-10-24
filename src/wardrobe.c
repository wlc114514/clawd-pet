#include "wardrobe.h"
#include <stdint.h>
#include <string.h>

#define WD_CLASS   L"ClawdWardrobe"
#define COL_INK    0xFF2B2B2Bu
#define COL_PAPER  0xFFFFFFFFu
#define COL_DIM    0xFFA8A8A8u
#define COL_LINE   0xFFE6E1D6u
#define COL_STAGE  0xFFF7F4EEu
#define COL_WORN   0xFFFBEDE7u
#define COL_ACCENT 0xFFD77757u
#define COL_HOT    0xFFC86A4Bu
#define COL_PRESS  0xFFB25A3Cu

/*
 * Layout at 96 dpi (scaled by D()):
 *
 *     .BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.
 *     B  换装                                [×]  B    title         y 10..32
 *     B  +---------+  头    [  ] [  ] [  ] [  ]   B    a row a slot  y 42.. (ROW_H each)
 *     B  |         |  脸    [  ] [  ] [  ]        B
 *     B  |  Clawd  |  嘴    [  ] [  ] [  ] [  ]   B
 *     B  |         |  脖子  [  ] [  ]             B
 *     B  |         |  手    [  ] [  ] [  ] [  ]   B
 *     B  +---------+        [  随机  ] [全部脱掉]  B    buttons       y 350..378
 *     .BBBBBBBBBBBBBBBBBBBBBB.BBBBBBBBBBBBBBBBBBBB.
 *                           B.B
 *                            B
 */
#define WD_W      496
#define WD_BH     392
#define TOP       42
#define PV_X0     14
#define PV_X1     152
#define LABEL_X   166
#define CELL_X    210
#define CELL_W    64
#define CELL_H    54
#define CELL_GAP  6
#define ROW_H     60
#define ICON_H    36   // top part of a cell; the name goes below
#define BTN_Y0    350
#define BTN_Y1    378
#define CLOSE_SZ  22

enum { HIT_NONE = -1, HIT_RANDOM = 100, HIT_CLEAR, HIT_CLOSE };   // or an ACC_*

static struct {
    HWND hwnd;
    WardrobeChanged changed;
    int pet;
    unsigned char wear[SLOTS];
    unsigned rng;
    UINT dpi;
    int u, W, H, tail;   // border unit, window size, tail column (units)
    int hot, pressed;    // HIT_* under the mouse / being pressed
    int tick, shown;     // animation clock; what the preview last showed
} wd;

static int D(int v)
{
    return MulDiv(v, (int)wd.dpi, 96);
}

static int snap(int v)
{
    return v / wd.u * wd.u;
}

static int S(int v)   // scaled and on the border grid
{
    return snap(D(v));
}

static void fill(uint32_t *px, int x0, int y0, int x1, int y1, uint32_t col)
{
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < wd.H; y++)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < wd.W; x++) px[y * wd.W + x] = col;
}

// Chamfered box with a one-unit border, like the text bubbles.
static void box(uint32_t *px, int x0, int y0, int x1, int y1, uint32_t border, uint32_t inside)
{
    int u = wd.u;
    fill(px, x0 + u, y0, x1 - u, y0 + u, border);
    fill(px, x0 + u, y1 - u, x1 - u, y1, border);
    fill(px, x0, y0 + u, x0 + u, y1 - u, border);
    fill(px, x1 - u, y0 + u, x1, y1 - u, border);
    fill(px, x0 + u, y0 + u, x1 - u, y1 - u, inside);
}

// Part of a canvas (sx, sy, sw by sh), each pixel k by k, onto opaque px at (x, y).
static void blit(uint32_t *px, const Canvas *c, int sx, int sy, int sw, int sh, int x, int y, int k)
{
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sw; i++) {
            uint32_t s = c->px[(sy + j) * c->w + sx + i], a = s >> 24;
            if (!a) continue;
            for (int yy = y + j * k; yy < y + (j + 1) * k && yy < wd.H; yy++)
                for (int xx = x + i * k; xx < x + (i + 1) * k && xx < wd.W; xx++) {
                    uint32_t d = px[yy * wd.W + xx], out = 0xFF000000u;
                    for (int sh8 = 0; sh8 < 24; sh8 += 8)
                        out |= ((((s >> sh8) & 255) * a + ((d >> sh8) & 255) * (255 - a)) / 255) << sh8;
                    px[yy * wd.W + xx] = out;
                }
        }
}

static Pose stand(void)
{
    Pose q = { 0 };
    q.eyes = EYE_OPEN;
    for (int i = 0; i < 4; i++) q.legs[i] = 2;
    return q;
}

/* ---- layout ----------------------------------------------------------- */

static void cell_rect(int a, RECT *r)
{
    int s = acc_slot(a), col = 0;
    for (int b = 1; b < a; b++) col += acc_slot(b) == s;
    r->left = S(CELL_X + col * (CELL_W + CELL_GAP));
    r->top = S(TOP + s * ROW_H);
    r->right = r->left + S(CELL_W);
    r->bottom = r->top + S(CELL_H);
}

static void button_rect(int which, RECT *r)
{
    int x0 = which == HIT_RANDOM ? CELL_X : CELL_X + 2 * (CELL_W + CELL_GAP);
    r->left = S(x0);
    r->right = S(x0 + 2 * CELL_W + CELL_GAP);
    r->top = S(BTN_Y0);
    r->bottom = S(BTN_Y1);
}

static void close_rect(RECT *r)
{
    r->right = wd.W - S(12);
    r->left = r->right - S(CLOSE_SZ);
    r->top = S(10);
    r->bottom = r->top + S(CLOSE_SZ);
}

static int hit(int x, int y)
{
    POINT p = { x, y };
    RECT r;
    for (int a = 1; a < ACC_COUNT; a++) {
        cell_rect(a, &r);
        if (PtInRect(&r, p)) return a;
    }
    for (int b = HIT_RANDOM; b <= HIT_CLEAR; b++) {
        button_rect(b, &r);
        if (PtInRect(&r, p)) return b;
    }
    close_rect(&r);
    return PtInRect(&r, p) ? HIT_CLOSE : HIT_NONE;
}

/* ---- drawing ---------------------------------------------------------- */

static HFONT font(int size, int weight)
{
    return CreateFontW(-D(size), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       0, 0, ANTIALIASED_QUALITY, 0, L"Microsoft YaHei UI");
}

// Draws s (UTF-8) with its left edge at x (or centred on x), vertically centred on cy.
static void text(HDC dc, const char *s, int size, int weight, uint32_t col, int x, int cy, int center)
{
    wchar_t w[32];
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, w, 32) - 1;
    if (n <= 0) return;
    HFONT f = font(size, weight), of = SelectObject(dc, f);
    SIZE sz;
    TEXTMETRICW tm;
    GetTextExtentPoint32W(dc, w, n, &sz);
    GetTextMetricsW(dc, &tm);
    SetTextColor(dc, RGB((col >> 16) & 255, (col >> 8) & 255, col & 255));
    if (center) x -= sz.cx / 2;
    TextOutW(dc, x, cy + (tm.tmAscent - tm.tmInternalLeading) / 2, w, n);
    SelectObject(dc, of);
    DeleteObject(f);
}

// What the preview shows this tick: blinking now and then, looking about.
static Pose preview_pose(void)
{
    Pose q = stand();
    static const int looks[4] = { 0, 1, 0, -1 };
    q.look = looks[(wd.tick / 50) % 4];
    if (wd.tick % 70 < 3) q.eyes = EYE_CLOSED;
    memcpy(q.wear, wd.wear, sizeof q.wear);
    q.tick = wd.tick;
    return q;
}

static int preview_key(void)
{
    Pose q = preview_pose();
    return q.eyes * 100 + (q.look + 1) * 10 + (q.tick / 8) % 2;
}

static void draw_preview(uint32_t *px, int x0, int y0, int x1, int y1)
{
    static uint32_t lp[CW * CH];
    Canvas c = { lp, CW, CH };
    Pose q = preview_pose();
    cv_clear(&c);
    draw_clawd(&c, SPR_OX, SPR_OY, &q);

    // Columns 7..26 and rows 8..23 hold the sprite with any hat and cup.
    // The body is centred, so the wider side (the cup's) decides the size.
    int sx = SPR_OX + 1, sw = 20, sy = SPR_OY - 6, sh = FOOT_Y - sy;
    int mid = (SPR_OX + 9) - sx, side = sw - mid > mid ? sw - mid : mid;
    int k = ((x1 - x0) / 2 - D(10)) / side;
    if (k > (y1 - y0) / (sh + 8)) k = (y1 - y0) / (sh + 8);
    if (k < 1) k = 1;
    int x = (x0 + x1) / 2 - mid * k, feet = y0 + (y1 - y0) * 5 / 8;
    fill(px, x0 + wd.u, feet, x1 - wd.u, feet + wd.u, COL_LINE);   // the floor
    blit(px, &c, sx, sy, sw, sh, x, feet - sh * k, k);
}

static void draw_icon(uint32_t *px, int a, const RECT *r)
{
    static uint32_t lp[CW * CH];
    Canvas c = { lp, CW, CH };
    Pose q = stand();
    q.wear[acc_slot(a)] = (unsigned char)a;

    // Its size, over both frames of any animation.
    int x0 = CW, y0 = CH, x1 = -1, y1 = -1;
    for (int t = 0; t < 16; t += 8) {
        q.tick = t;
        cv_clear(&c);
        draw_outfit(&c, SPR_OX, SPR_OY, &q);
        for (int y = 0; y < CH; y++)
            for (int x = 0; x < CW; x++)
                if (lp[y * CW + x] >> 24) {
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
    }
    int w = x1 - x0 + 1, h = y1 - y0 + 1, k = D(4);
    if (k > (S(CELL_W) - D(10)) / w) k = (S(CELL_W) - D(10)) / w;
    if (k > (S(ICON_H) - D(8)) / h) k = (S(ICON_H) - D(8)) / h;
    if (k < 1) k = 1;

    q.tick = wd.tick;
    cv_clear(&c);
    draw_outfit(&c, SPR_OX, SPR_OY, &q);
    int cx = (r->left + r->right) / 2, cy = r->top + S(ICON_H) / 2 + D(2);
    blit(px, &c, x0, y0, w, h, cx - w * k / 2, cy - h * k / 2, k);
}

static void draw(POINT *pos)
{
    int u = wd.u, W = wd.W, H = wd.H, BH = H - 2 * u;
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
    int t = wd.tail * u;
    fill(px, t, BH - u, t + u, BH, COL_PAPER);
    fill(px, t - u, BH, t, BH + u, COL_INK);
    fill(px, t, BH, t + u, BH + u, COL_PAPER);
    fill(px, t + u, BH, t + 2 * u, BH + u, COL_INK);
    fill(px, t, BH + u, t + u, BH + 2 * u, COL_INK);

    // The preview, on a little stage.
    box(px, S(PV_X0), S(TOP), S(PV_X1), S(BTN_Y1), COL_LINE, COL_STAGE);
    draw_preview(px, S(PV_X0), S(TOP), S(PV_X1), S(BTN_Y1));

    for (int a = 1; a < ACC_COUNT; a++) {
        RECT r;
        cell_rect(a, &r);
        int worn = wd.wear[acc_slot(a)] == a;
        uint32_t border = worn ? COL_ACCENT : wd.hot == a ? COL_DIM : COL_LINE;
        box(px, r.left, r.top, r.right, r.bottom, border, worn ? COL_WORN : COL_PAPER);
        if (worn) box(px, r.left + u, r.top + u, r.right - u, r.bottom - u, COL_ACCENT, COL_WORN);   // thicker
        draw_icon(px, a, &r);
    }

    for (int b = HIT_RANDOM; b <= HIT_CLEAR; b++) {
        RECT r;
        button_rect(b, &r);
        int on = wd.hot == b;
        uint32_t fillc = b == HIT_RANDOM ? (on && wd.pressed == b ? COL_PRESS : on ? COL_HOT : COL_ACCENT)
                                         : (on ? COL_LINE : COL_PAPER);
        box(px, r.left, r.top, r.right, r.bottom, COL_INK, fillc);
    }
    RECT cr;
    close_rect(&cr);
    if (wd.hot == HIT_CLOSE) box(px, cr.left, cr.top, cr.right, cr.bottom, COL_LINE, COL_LINE);
    for (int i = 0; i < 5; i++) {   // ×, in border units
        int cx = (cr.left + cr.right) / 2 - u / 2, cy = (cr.top + cr.bottom) / 2 - u / 2, d = (i - 2) * u;
        fill(px, cx + d, cy + d, cx + d + u, cy + d + u, COL_INK);
        fill(px, cx + d, cy - d, cx + d + u, cy - d + u, COL_INK);
    }

    // GDI text on top. It blends with the opaque pixels below but zeroes
    // their alpha, which is put back afterwards.
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_BASELINE | TA_LEFT);
    text(dc, "换装", 15, FW_BOLD, COL_INK, S(PV_X0) + D(2), S(21), 0);
    for (int s = 0; s < SLOTS; s++)
        text(dc, acc_slot_name(s), 13, FW_SEMIBOLD, COL_INK, S(LABEL_X), S(TOP + s * ROW_H) + S(CELL_H) / 2, 0);
    for (int a = 1; a < ACC_COUNT; a++) {
        RECT r;
        cell_rect(a, &r);
        int worn = wd.wear[acc_slot(a)] == a;
        text(dc, acc_name(a), 11, worn ? FW_SEMIBOLD : FW_NORMAL, worn ? COL_PRESS : COL_INK,
             (r.left + r.right) / 2, r.top + S(ICON_H) + (S(CELL_H) - S(ICON_H)) / 2 - u / 2, 1);
    }
    for (int b = HIT_RANDOM; b <= HIT_CLEAR; b++) {
        RECT r;
        button_rect(b, &r);
        text(dc, b == HIT_RANDOM ? "随机搭配" : "全部脱掉", 13, FW_BOLD, b == HIT_RANDOM ? COL_PAPER : COL_INK,
             (r.left + r.right) / 2, (r.top + r.bottom) / 2, 1);
    }
    GdiFlush();
    for (int i = 0; i < W * H; i++)
        if (px[i] && !(px[i] >> 24)) px[i] |= 0xFF000000u;

    SIZE size = { W, H };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(wd.hwnd, NULL, pos, &size, dc, &src, 0, &bf, ULW_ALPHA);
    SelectObject(dc, obmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    wd.shown = preview_key();
}

static void redraw(void)
{
    draw(NULL);
}

/* ---- behaviour -------------------------------------------------------- */

static void changed(void)
{
    redraw();
    if (wd.changed) wd.changed(wd.pet, wd.wear);
}

static void press(int what)
{
    if (what > ACC_NONE && what < ACC_COUNT) {
        unsigned char *slot = &wd.wear[acc_slot(what)];
        *slot = *slot == what ? ACC_NONE : (unsigned char)what;
        changed();
    } else if (what == HIT_RANDOM) {
        unsigned char was[SLOTS];
        memcpy(was, wd.wear, sizeof was);
        static const unsigned char none[SLOTS];
        do outfit_roll(wd.wear, &wd.rng);   // something new, and not nothing
        while (!memcmp(was, wd.wear, sizeof was) || !memcmp(none, wd.wear, sizeof none));
        changed();
    } else if (what == HIT_CLEAR) {
        memset(wd.wear, 0, sizeof wd.wear);
        changed();
    } else if (what == HIT_CLOSE) {
        wardrobe_close();
    }
}

static LRESULT CALLBACK wd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    switch (msg) {
    case WM_TIMER:
        wd.tick++;
        if (preview_key() != wd.shown) redraw();
        return 0;

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        wd.pressed = hit(x, y);
        redraw();
        return 0;

    case WM_MOUSEMOVE: {
        int h = hit(x, y);
        if (h != wd.hot) {
            wd.hot = h;
            TRACKMOUSEEVENT te = { sizeof te, TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&te);
            redraw();
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        wd.hot = HIT_NONE;
        redraw();
        return 0;

    case WM_LBUTTONUP: {
        int was = wd.pressed;
        wd.pressed = HIT_NONE;
        ReleaseCapture();
        if (was != HIT_NONE && was == hit(x, y)) press(was);
        else redraw();
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_RETURN) wardrobe_close();
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (wd.hwnd == hwnd) wd.hwnd = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void wardrobe_register(HINSTANCE inst)
{
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = wd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    wc.lpszClassName = WD_CLASS;
    RegisterClassW(&wc);
}

void wardrobe_open(POINT anchor, RECT area, UINT dpi, int pet_id, const unsigned char *wear,
                   WardrobeChanged on_change)
{
    if (wd.hwnd) DestroyWindow(wd.hwnd);
    memset(&wd, 0, sizeof wd);
    wd.changed = on_change;
    wd.pet = pet_id;
    memcpy(wd.wear, wear, sizeof wd.wear);
    wd.rng = GetTickCount() | 1;
    wd.hot = wd.pressed = HIT_NONE;
    wd.dpi = dpi;
    wd.u = dpi >= 144 ? 3 : 2;
    wd.W = S(WD_W);
    wd.H = S(WD_BH) + 2 * wd.u;

    POINT pos = { anchor.x - wd.W / 2, anchor.y - wd.H };
    if (pos.x + wd.W > area.right) pos.x = area.right - wd.W;
    if (pos.x < area.left) pos.x = area.left;
    if (pos.y < area.top) pos.y = area.top;
    wd.tail = (anchor.x - pos.x) / wd.u;
    if (wd.tail < 2) wd.tail = 2;
    if (wd.tail > wd.W / wd.u - 3) wd.tail = wd.W / wd.u - 3;

    wd.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, WD_CLASS, L"换装",
                              WS_POPUP, pos.x, pos.y, wd.W, wd.H, NULL, NULL, GetModuleHandleW(NULL), NULL);
    draw(&pos);
    SetTimer(wd.hwnd, 1, 50, NULL);
    ShowWindow(wd.hwnd, SW_SHOW);
    SetForegroundWindow(wd.hwnd);
    SetFocus(wd.hwnd);
}

int wardrobe_pet(void)
{
    return wd.hwnd ? wd.pet : 0;
}

void wardrobe_close(void)
{
    if (wd.hwnd) DestroyWindow(wd.hwnd);
}
