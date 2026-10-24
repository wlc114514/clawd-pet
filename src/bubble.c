#include "bubble.h"
#include <stdint.h>
#include <string.h>

#define TB_CLASS  L"ClawdBubble"
#define COL_INK   0xFF2B2B2Bu
#define COL_PAPER 0xFFFFFFFFu

static LRESULT CALLBACK tb_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_LBUTTONUP) {
        TextBubble *b = (TextBubble *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (b) tb_hide(b);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void tb_register(HINSTANCE inst)
{
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = tb_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    wc.lpszClassName = TB_CLASS;
    RegisterClassW(&wc);
}

void tb_show(TextBubble *b, const wchar_t *text, DWORD ms)
{
    wcsncpy(b->text, text, 127);
    b->text[127] = 0;
    b->until = GetTickCount64() + ms;
    b->tail = -1;
}

void tb_suspend(TextBubble *b)
{
    if (b->hwnd && b->shown) ShowWindow(b->hwnd, SW_HIDE);
    b->shown = 0;
}

void tb_hide(TextBubble *b)
{
    b->until = 0;
    tb_suspend(b);
}

void tb_destroy(TextBubble *b)
{
    if (b->hwnd) DestroyWindow(b->hwnd);
    b->hwnd = NULL;
    b->until = 0;
    b->shown = 0;
}

static HFONT make_font(UINT dpi)
{
    return CreateFontW(-MulDiv(15, dpi, 96), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                       0, 0, ANTIALIASED_QUALITY, 0, L"Microsoft YaHei UI");
}

static HBITMAP make_dib(HDC dc, int w, int h, uint32_t **bits)
{
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)bits, NULL, 0);
}

// Measure the text and work out the window size (border units of u px).
static void tb_measure(TextBubble *b, UINT dpi, SIZE *text)
{
    HDC dc = CreateCompatibleDC(NULL);
    HFONT f = make_font(dpi), of = SelectObject(dc, f);
    RECT r = { 0, 0, 0, 0 };
    DrawTextW(dc, b->text, -1, &r, DT_CALCRECT | DT_NOPREFIX);
    SelectObject(dc, of);
    DeleteObject(f);
    DeleteDC(dc);

    int u = dpi >= 144 ? 3 : 2;
    int uw = (r.right + 10 * u + u - 1) / u, uh = (r.bottom + 7 * u + u - 1) / u;
    b->u = u;
    b->w = uw * u;
    b->h = (uh + 2) * u;   // two extra rows for the tail
    text->cx = r.right;
    text->cy = r.bottom;
}

/*
 * Pixel-art bubble: chamfered box with a 1-unit ink border, and a little
 * stepped tail under column `tail`:
 *
 *    .BBBBBBBB.
 *    B........B
 *    B..text..B
 *    .BBB.BBBB.
 *      B.B
 *       B
 */
static void tb_draw(TextBubble *b, POINT pos, UINT dpi)
{
    SIZE ts;
    tb_measure(b, dpi, &ts);
    int u = b->u, W = b->w, H = b->h, UW = W / u, UH = H / u - 2, tail = b->tail;

    HDC dc = CreateCompatibleDC(NULL);
    uint32_t *px, *tx;
    HBITMAP bmp = make_dib(dc, W, H, &px), obmp = SelectObject(dc, bmp);
    memset(px, 0, (size_t)W * H * 4);

    for (int uy = 0; uy < UH + 2; uy++)
        for (int ux = 0; ux < UW; ux++) {
            uint32_t col = 0;
            if (uy < UH) {
                int edge_x = ux == 0 || ux == UW - 1, edge_y = uy == 0 || uy == UH - 1;
                if (edge_x && edge_y) continue;
                col = (edge_x || edge_y) ? COL_INK : COL_PAPER;
                if (uy == UH - 1 && ux == tail) col = COL_PAPER;
            } else if (uy == UH) {
                if (ux == tail) col = COL_PAPER;
                else if (ux == tail - 1 || ux == tail + 1) col = COL_INK;
            } else if (ux == tail) {
                col = COL_INK;
            }
            if (!col) continue;
            for (int y = uy * u; y < (uy + 1) * u; y++)
                for (int x = ux * u; x < (ux + 1) * u; x++) px[y * W + x] = col;
        }

    // Render white-on-black grayscale text, then use it as coverage.
    HDC tdc = CreateCompatibleDC(NULL);
    HBITMAP tbmp = make_dib(tdc, ts.cx, ts.cy, &tx), otbmp = SelectObject(tdc, tbmp);
    memset(tx, 0, (size_t)ts.cx * ts.cy * 4);
    HFONT f = make_font(dpi), of = SelectObject(tdc, f);
    SetTextColor(tdc, RGB(255, 255, 255));
    SetBkMode(tdc, TRANSPARENT);
    RECT tr = { 0, 0, ts.cx, ts.cy };
    DrawTextW(tdc, b->text, -1, &tr, DT_CENTER | DT_NOPREFIX);
    GdiFlush();

    int ox = (W - ts.cx) / 2, oy = (UH * u - ts.cy) / 2;
    for (int y = 0; y < ts.cy; y++)
        for (int x = 0; x < ts.cx; x++) {
            uint32_t cov = (tx[y * ts.cx + x] >> 8) & 255, out = 0xFF000000u;
            for (int sh = 0; sh < 24; sh += 8)
                out |= ((((COL_PAPER >> sh) & 255) * (255 - cov) + ((COL_INK >> sh) & 255) * cov) / 255) << sh;
            px[(oy + y) * W + ox + x] = out;
        }
    SelectObject(tdc, of);
    DeleteObject(f);
    SelectObject(tdc, otbmp);
    DeleteObject(tbmp);
    DeleteDC(tdc);

    SIZE sz = { W, H };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(b->hwnd, NULL, &pos, &sz, dc, &src, 0, &bf, ULW_ALPHA);
    SelectObject(dc, obmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

void tb_update(TextBubble *b, POINT anchor, RECT area, UINT dpi)
{
    if (!b->until) return;
    if (GetTickCount64() >= b->until) {
        tb_hide(b);
        return;
    }
    if (!b->hwnd) {
        b->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                  TB_CLASS, L"", WS_POPUP, 0, 0, 1, 1, NULL, NULL,
                                  GetModuleHandleW(NULL), NULL);
        SetWindowLongPtrW(b->hwnd, GWLP_USERDATA, (LONG_PTR)b);
    }
    if (b->tail < 0) {   // new text: measure before placing
        SIZE ts;
        tb_measure(b, dpi, &ts);
    }

    POINT pos = { anchor.x - b->w / 2, anchor.y - b->h };
    if (pos.x + b->w > area.right) pos.x = area.right - b->w;
    if (pos.x < area.left) pos.x = area.left;
    if (pos.y < area.top) pos.y = area.top;

    int UW = b->w / b->u, tail = (anchor.x - pos.x) / b->u;
    if (tail < 2) tail = 2;
    if (tail > UW - 3) tail = UW - 3;

    if (tail != b->tail) {
        b->tail = tail;
        tb_draw(b, pos, dpi);
    } else if (pos.x != b->pos.x || pos.y != b->pos.y) {
        SetWindowPos(b->hwnd, HWND_TOPMOST, pos.x, pos.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }
    b->pos = pos;
    if (!b->shown) {
        ShowWindow(b->hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(b->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        b->shown = 1;
    }
}
