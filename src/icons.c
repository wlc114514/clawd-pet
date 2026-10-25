/*
 * Desktop icons, through the IFolderView2 of Explorer's desktop view: the
 * same documented shell interface that "View > Align icons to grid" and
 * friends use. Nothing here reads or writes another process's memory.
 */
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <string.h>
#include "icons.h"

#define REG_ICONS L"Software\\ClawdPet\\Icons"
#define GRABS     8
#define MAX_ICONS 2048

static IFolderView2 *g_fv;
static HWND g_lv;                 // the desktop's list view: positions are in its client area
static PITEMID_CHILD g_grab[GRABS];

static void drop_view(void)
{
    if (g_fv) IFolderView2_Release(g_fv);
    g_fv = NULL;
    g_lv = NULL;
}

void icons_reset(void)
{
    drop_view();
}

static IFolderView2 *view(void)
{
    if (g_fv && IsWindow(g_lv)) return g_fv;
    drop_view();
    IShellWindows *sw = NULL;
    IDispatch *disp = NULL;
    IShellBrowser *sb = NULL;
    IShellView *sv = NULL;
    VARIANT loc, empty;
    long hwnd = 0;
    VariantInit(&loc);
    VariantInit(&empty);
    V_VT(&loc) = VT_I4;
    V_I4(&loc) = CSIDL_DESKTOP;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER, &IID_IShellWindows, (void **)&sw)) &&
        IShellWindows_FindWindowSW(sw, &loc, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &disp) == S_OK &&
        SUCCEEDED(IUnknown_QueryService((IUnknown *)disp, &SID_STopLevelBrowser, &IID_IShellBrowser, (void **)&sb)) &&
        SUCCEEDED(IShellBrowser_QueryActiveShellView(sb, &sv)) &&
        SUCCEEDED(IShellView_QueryInterface(sv, &IID_IFolderView2, (void **)&g_fv))) {
        HWND dv = NULL;
        IShellView_GetWindow(sv, &dv);
        g_lv = FindWindowExW(dv, NULL, L"SysListView32", NULL);
        if (!g_lv) drop_view();
    }
    if (sv) IShellView_Release(sv);
    if (sb) IShellBrowser_Release(sb);
    if (disp) IDispatch_Release(disp);
    if (sw) IShellWindows_Release(sw);
    return g_fv;
}

// Screen position of the list view's (0, 0).
static POINT origin(void)
{
    POINT o = { 0, 0 };
    if (g_lv) ClientToScreen(g_lv, &o);
    return o;
}

int icons_ready(void)
{
    IFolderView2 *fv = view();
    DWORD flags = 0;
    return fv && SUCCEEDED(IFolderView2_GetCurrentFolderFlags(fv, &flags)) && !(flags & FWF_AUTOARRANGE);
}

int icons_size(void)
{
    IFolderView2 *fv = view();
    FOLDERVIEWMODE mode;
    int size = 48;
    if (fv) IFolderView2_GetViewModeAndIconSize(fv, &mode, &size);
    UINT dpi = g_lv ? GetDpiForWindow(g_lv) : 0;
    return MulDiv(size, dpi ? (int)dpi : 96, 96);
}

static void item_name(IShellFolder *sf, PCUITEMID_CHILD id, wchar_t *out)
{
    STRRET sr;
    out[0] = 0;
    if (SUCCEEDED(IShellFolder_GetDisplayNameOf(sf, id, SHGDN_INFOLDER | SHGDN_FORPARSING, &sr)))
        StrRetToBufW(&sr, id, out, ICON_NAME);
}

/*
 * Goes through every icon: fn(name, list-view position, its ID, arg) for
 * each; it keeps the ID by returning 2 (then it is the caller's to free), or
 * stops with 1 / 2. Names are only looked up if `named`.
 */
static int each_icon(int named, int (*fn)(const wchar_t *, POINT, PITEMID_CHILD, void *), void *arg)
{
    IFolderView2 *fv = view();
    IShellFolder *sf = NULL;
    int count = 0;
    if (!fv || FAILED(IFolderView2_ItemCount(fv, SVGIO_ALLVIEW, &count)) ||
        (named && FAILED(IFolderView2_GetFolder(fv, &IID_IShellFolder, (void **)&sf)))) {
        drop_view();
        return 0;
    }
    wchar_t name[ICON_NAME] = L"";
    int stop = 0;
    for (int i = 0; i < count && !stop; i++) {
        PITEMID_CHILD id = NULL;
        POINT pt;
        if (FAILED(IFolderView2_Item(fv, i, &id))) continue;
        if (SUCCEEDED(IFolderView2_GetItemPosition(fv, id, &pt))) {
            if (named) item_name(sf, id, name);
            stop = fn(name, pt, id, arg);
        }
        if (stop != 2) CoTaskMemFree(id);
    }
    if (sf) IShellFolder_Release(sf);
    return stop;
}

typedef struct {
    DeskIcon *out;
    int n, max, size;
    RECT in;   // list view coordinates
} ListArg;

static int list_one(const wchar_t *name, POINT pt, PITEMID_CHILD id, void *arg)
{
    (void)id;
    ListArg *a = arg;
    if (a->n >= a->max) return 1;
    if (pt.x >= a->in.left && pt.y >= a->in.top && pt.x + a->size <= a->in.right && pt.y + a->size <= a->in.bottom) {
        wcsncpy(a->out[a->n].name, name, ICON_NAME - 1);
        a->out[a->n].name[ICON_NAME - 1] = 0;
        a->out[a->n++].pt = pt;
    }
    return 0;
}

// In list view coordinates; `named`: with their names.
static int list_lv(DeskIcon *out, int max, const RECT *in, int named)
{
    ListArg a = { out, 0, max, named ? icons_size() : 0, *in };
    each_icon(named, list_one, &a);
    return a.n;
}

int icons_list(DeskIcon *out, int max, const RECT *in)
{
    if (!view()) return 0;
    POINT o = origin();
    RECT r = { in->left - o.x, in->top - o.y, in->right - o.x, in->bottom - o.y };
    int n = list_lv(out, max, &r, 1);
    for (int i = 0; i < n; i++) {
        out[i].pt.x += o.x;
        out[i].pt.y += o.y;
    }
    return n;
}

typedef struct {
    const wchar_t *name;
    POINT pt;
    PITEMID_CHILD id;
    int found;
} FindArg;

static int find_one(const wchar_t *name, POINT pt, PITEMID_CHILD id, void *arg)
{
    FindArg *a = arg;
    if (wcscmp(name, a->name)) return 0;
    a->found = 1;
    a->pt = pt;
    a->id = id;
    return 2;
}

int icons_find(const wchar_t *name, POINT *pt)
{
    FindArg a = { name };
    each_icon(1, find_one, &a);
    if (!a.found) return 0;
    CoTaskMemFree(a.id);
    POINT o = origin();
    pt->x = a.pt.x + o.x;
    pt->y = a.pt.y + o.y;
    return 1;
}

int icons_grab(const wchar_t *name)
{
    int h = 0;
    while (h < GRABS && g_grab[h]) h++;
    if (h == GRABS) return -1;
    FindArg a = { name };
    each_icon(1, find_one, &a);
    if (!a.found) return -1;
    g_grab[h] = a.id;
    return h;
}

void icons_move(int h, POINT pt)
{
    if (h < 0 || h >= GRABS || !g_grab[h]) return;
    IFolderView2 *fv = view();
    if (!fv) return;
    POINT o = origin();
    POINT at = { pt.x - o.x, pt.y - o.y };
    PCUITEMID_CHILD ids[1] = { g_grab[h] };
    if (FAILED(IFolderView2_SelectAndPositionItems(fv, 1, ids, &at, SVSI_POSITIONITEM))) drop_view();
}

void icons_release(int h)
{
    if (h < 0 || h >= GRABS || !g_grab[h]) return;
    CoTaskMemFree(g_grab[h]);
    g_grab[h] = NULL;
}

uint32_t *icons_picture(const wchar_t *name, int size)
{
    if (size <= 0) return NULL;
    // By name, from the desktop folder itself: the view's own IDs get only a
    // generic picture for most things.
    IShellFolder *sf = NULL;
    PIDLIST_RELATIVE id = NULL;
    IShellItemImageFactory *f = NULL;
    HBITMAP bmp = NULL;
    SIZE want = { size, size };
    if (SUCCEEDED(SHGetDesktopFolder(&sf)) &&
        SUCCEEDED(IShellFolder_ParseDisplayName(sf, NULL, NULL, (LPWSTR)name, NULL, &id, NULL)) &&
        SUCCEEDED(SHCreateItemWithParent(NULL, sf, (PCUITEMID_CHILD)id, &IID_IShellItemImageFactory, (void **)&f)))
        IShellItemImageFactory_GetImage(f, want, SIIGBF_ICONONLY, &bmp);   // never a thumbnail of what is inside
    if (f) IShellItemImageFactory_Release(f);
    CoTaskMemFree(id);
    if (sf) IShellFolder_Release(sf);
    if (!bmp) return NULL;

    // Copied out top-down, whatever its own size, into the middle of size x size.
    BITMAP bm;
    uint32_t *src = NULL, *out = calloc((size_t)size * size, 4);
    if (GetObjectW(bmp, sizeof bm, &bm) && bm.bmWidth > 0 && bm.bmHeight > 0 && out &&
        (src = malloc((size_t)bm.bmWidth * bm.bmHeight * 4))) {
        BITMAPINFO bi = { { sizeof bi.bmiHeader, bm.bmWidth, -bm.bmHeight, 1, 32, BI_RGB } };
        HDC dc = GetDC(NULL);
        int ok = GetDIBits(dc, bmp, 0, (UINT)bm.bmHeight, src, &bi, DIB_RGB_COLORS) == bm.bmHeight;
        ReleaseDC(NULL, dc);
        int n = bm.bmWidth * bm.bmHeight, alpha = 0, straight = 0;
        for (int i = 0; ok && i < n; i++) {
            uint32_t s = src[i], a = s >> 24;
            alpha |= a != 0;
            straight |= ((s >> 16) & 255) > a || ((s >> 8) & 255) > a || (s & 255) > a;
        }
        for (int i = 0; ok && i < n; i++) {   // to straight ARGB: no alpha at all is opaque
            uint32_t s = src[i], a = s >> 24;
            if (!alpha) src[i] = s | 0xFF000000u;
            else if (!straight && a && a < 255)
                src[i] = a << 24 | (((s >> 16) & 255) * 255 / a) << 16 | (((s >> 8) & 255) * 255 / a) << 8 |
                         (s & 255) * 255 / a;
        }
        int ox = (size - bm.bmWidth) / 2, oy = (size - bm.bmHeight) / 2;
        for (int y = 0; ok && y < bm.bmHeight; y++)
            for (int x = 0; x < bm.bmWidth; x++)
                if (ox + x >= 0 && ox + x < size && oy + y >= 0 && oy + y < size)
                    out[(oy + y) * size + ox + x] = src[y * bm.bmWidth + x];
        if (!ok) {
            free(out);
            out = NULL;
        }
    } else {
        free(out);
        out = NULL;
    }
    free(src);
    DeleteObject(bmp);
    return out;
}

static LONG wrap(LONG v, LONG m)
{
    return m > 0 ? ((v % m) + m) % m : 0;
}

// The most common value of v[i] mod m (the grid the icons are lined up on); def if there are none.
static LONG grid_phase(const LONG *v, int n, LONG m, LONG def)
{
    LONG best = def;
    int best_n = 0;
    for (int i = 0; i < n; i++) {
        int c = 0;
        for (int j = 0; j < n; j++) c += wrap(v[j], m) == wrap(v[i], m);
        if (c > best_n) {
            best_n = c;
            best = wrap(v[i], m);
        }
    }
    return best;
}

int icons_free_spots(POINT *out, int max, const RECT *in)
{
    IFolderView2 *fv = view();
    POINT sp;
    if (!fv || FAILED(IFolderView2_GetSpacing(fv, &sp)) || sp.x <= 0 || sp.y <= 0) return 0;
    int size = icons_size();
    DeskIcon *all = malloc(sizeof *all * MAX_ICONS);
    LONG *xs = malloc(sizeof *xs * MAX_ICONS), *ys = malloc(sizeof *ys * MAX_ICONS);
    RECT every = { LONG_MIN / 2, LONG_MIN / 2, LONG_MAX / 2, LONG_MAX / 2 };
    int n = list_lv(all, MAX_ICONS, &every, 0), m = 0;
    for (int i = 0; i < n; i++) {
        xs[i] = all[i].pt.x;
        ys[i] = all[i].pt.y;
    }
    LONG gx = grid_phase(xs, n, sp.x, (sp.x - size) / 2), gy = grid_phase(ys, n, sp.y, 3);

    POINT o = origin();
    RECT r = { in->left - o.x, in->top - o.y, in->right - o.x, in->bottom - o.y };
    LONG pad = (sp.x - size) / 2;   // between the picture and the side of its cell
    for (LONG y = gy + sp.y * ((r.top - gy) / sp.y - 1); y + sp.y - 2 * gy <= r.bottom && m < max; y += sp.y) {
        if (y < r.top) continue;
        for (LONG x = gx + sp.x * ((r.left - gx) / sp.x - 1); x - pad + sp.x <= r.right && m < max; x += sp.x) {
            if (x - pad < r.left) continue;
            int taken = 0;
            for (int i = 0; i < n && !taken; i++)
                taken = labs(xs[i] - x) < sp.x / 2 + 1 && labs(ys[i] - y) < sp.y / 2 + 1;
            if (!taken) out[m++] = (POINT){ x + o.x, y + o.y };
        }
    }
    free(all);
    free(xs);
    free(ys);
    return m;
}

/* ---- what Clawd moved ------------------------------------------------- */

// The value: home and now, in list view coordinates.
typedef struct { LONG hx, hy, nx, ny; } Rec;

static int rec_get(const wchar_t *name, Rec *r)
{
    DWORD sz = sizeof *r;
    return RegGetValueW(HKEY_CURRENT_USER, REG_ICONS, name, RRF_RT_REG_BINARY, NULL, r, &sz) == ERROR_SUCCESS &&
           sz == sizeof *r;
}

int moved_load(MovedIcon *out, int max)
{
    HKEY key;
    if (!view() || RegOpenKeyExW(HKEY_CURRENT_USER, REG_ICONS, 0, KEY_READ, &key) != ERROR_SUCCESS) return 0;
    POINT o = origin();
    int n = 0;
    for (DWORD i = 0; n < max; i++) {
        DWORD len = ICON_NAME, type, sz = sizeof(Rec);
        Rec r;
        LONG e = RegEnumValueW(key, i, out[n].name, &len, NULL, &type, (BYTE *)&r, &sz);
        if (e == ERROR_NO_MORE_ITEMS) break;
        if (e != ERROR_SUCCESS || type != REG_BINARY || sz != sizeof r) continue;
        out[n].home = (POINT){ r.hx + o.x, r.hy + o.y };
        out[n].now = (POINT){ r.nx + o.x, r.ny + o.y };
        n++;
    }
    RegCloseKey(key);
    return n;
}

void moved_note(const wchar_t *name, POINT home, POINT now)
{
    if (!view()) return;
    POINT o = origin();
    Rec r = { home.x - o.x, home.y - o.y, now.x - o.x, now.y - o.y }, old;
    if (rec_get(name, &old)) {   // it was moved before: home is where it was then
        r.hx = old.hx;
        r.hy = old.hy;
    }
    if (r.hx == r.nx && r.hy == r.ny) moved_forget(name);
    else RegSetKeyValueW(HKEY_CURRENT_USER, REG_ICONS, name, REG_BINARY, &r, sizeof r);
}

void moved_forget(const wchar_t *name)
{
    RegDeleteKeyValueW(HKEY_CURRENT_USER, REG_ICONS, name);
}

int moved_check(int (*skip)(const wchar_t *name))
{
    enum { MAX_MOVED = 256 };
    MovedIcon *m = malloc(sizeof *m * MAX_MOVED);
    DeskIcon *all = malloc(sizeof *all * MAX_ICONS);
    int n = moved_load(m, MAX_MOVED), left = 0;
    RECT every = { LONG_MIN / 2, LONG_MIN / 2, LONG_MAX / 2, LONG_MAX / 2 };
    int na = n ? icons_list(all, MAX_ICONS, &every) : 0;
    if (!na) left = n;   // couldn't see the desktop just now: keep them all
    for (int i = 0; i < n && na; i++) {
        if (skip && skip(m[i].name)) {
            left++;
            continue;
        }
        int j = 0;
        while (j < na && wcscmp(all[j].name, m[i].name)) j++;
        if (j == na || all[j].pt.x != m[i].now.x || all[j].pt.y != m[i].now.y) moved_forget(m[i].name);
        else left++;
    }
    free(m);
    free(all);
    return left;
}
