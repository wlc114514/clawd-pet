#include "ui.h"

HBITMAP ui_dib(int w, int h, uint32_t **bits)
{
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)bits, NULL, 0);
}

HFONT ui_font(int px, int weight)
{
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       0, 0, ANTIALIASED_QUALITY, 0, L"Microsoft YaHei UI");
}

int ui_text(HDC dc, const wchar_t *s, int px, int weight, uint32_t col, int x, int cy, int center)
{
    HFONT f = ui_font(px, weight), of = SelectObject(dc, f);
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

void ui_fill(uint32_t *px, int W, int H, int x0, int y0, int x1, int y1, uint32_t col)
{
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < H; y++)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < W; x++) px[y * W + x] = col;
}

void ui_box(uint32_t *px, int W, int H, int u, int x0, int y0, int x1, int y1, uint32_t border, uint32_t inside)
{
    ui_fill(px, W, H, x0 + u, y0, x1 - u, y0 + u, border);
    ui_fill(px, W, H, x0 + u, y1 - u, x1 - u, y1, border);
    ui_fill(px, W, H, x0, y0 + u, x0 + u, y1 - u, border);
    ui_fill(px, W, H, x1 - u, y0 + u, x1, y1 - u, border);
    ui_fill(px, W, H, x0 + u, y0 + u, x1 - u, y1 - u, inside);
}
