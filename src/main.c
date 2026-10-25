#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pet.h"
#include "bubble.h"
#include "picker.h"
#include "wardrobe.h"
#include "update.h"
#include "version.h"
#include "wall.h"

#define WM_TRAY    (WM_APP + 1)
#define WM_UPDATE  (WM_APP + 2)   // from update.c: wParam UPD_*, lParam manual
#define TICK_MS    33
#define MAX_PETS   6
#define APP_NAME   L"ClawdPet"
#define HOST_CLASS L"ClawdPetHost"
#define REG_KEY    L"Software\\ClawdPet"
#define RUN_KEY    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

#define CC_WORK_MS     (3 * 60 * 1000)    // no event for this long: assume Claude stopped
#define CC_WAIT_MS     (10 * 60 * 1000)

enum {
    IDM_PAUSE = 100, IDM_SLEEP, IDM_SIZE_S, IDM_SIZE_M, IDM_SIZE_L, IDM_AUTOSTART, IDM_EXIT,
    IDM_MOUSE_OFF, IDM_MOUSE_FOLLOW, IDM_MOUSE_SHY, IDM_TYPING, IDM_FULLSCREEN,
    IDM_POMO_START, IDM_POMO_STOP, IDM_WATER_OFF, IDM_WATER_30, IDM_WATER_45, IDM_WATER_60,
    IDM_ADD_PET, IDM_REMOVE_PET, IDM_FEAT_POMO, IDM_FEAT_WATER, IDM_CLIMB, IDM_FEAT_CLIMB,
    IDM_STAIRS_SLAB, IDM_STAIRS_SOLID, IDM_STAIRS_OUTLINE,   // same order as STAIRS_*
    IDM_CLI, IDM_WALL_DRAW, IDM_WALL_CLEAR, IDM_WARDROBE,
    IDM_PLAY_GREET, IDM_PLAY_CHASE, IDM_PLAY_LINE, IDM_PLAY_RIDE, IDM_PLAY_DAZE,   // same order as PLAY_*
    IDM_FLIP, IDM_UPDATE_CHECK, IDM_UPDATE_INSTALL, IDM_FEAT_UPDATE, IDM_FOCUS, IDM_SLIDE, IDM_FEAT_TOAST,
};

// Claude Code hook events, sent by `clawd.exe --cc <name>`.
enum { CC_EV_PROMPT = 1, CC_EV_TOOL, CC_EV_PERMISSION, CC_EV_STOP, CC_EV_END };
static const wchar_t *CC_EVENTS[] = { L"prompt", L"tool", L"permission", L"stop", L"end" };

static const int SIZE_PX[3] = { 3, 4, 6 };   // logical pixel size at 96 dpi
static const int WATER_MIN[4] = { 0, 30, 45, 60 };

typedef struct {
    HWND hwnd;
    Pet pet;
    TextBubble tb;
    HDC memdc;
    HBITMAP dib;
    uint32_t *bits;
    int dib_k, dirty, hidden;
    POINT prev_pos;
    uint32_t px[CW * CH], prev_px[CW * CH];
    // the stairs it is climbing, in a click-through window of their own
    HWND st_hwnd;
    HDC st_dc;
    HBITMAP st_dib;
    uint32_t *st_bits;
    int st_w, st_h, st_k;
    POINT st_pos;
    uint32_t st_px[STAIRS_CW * STAIRS_CH], st_prev[STAIRS_CW * STAIRS_CH];
    // while it is in the air: a bigger window just below it, there to the mouse but not
    // to the eye, so it is easier to catch
    HWND catch_hwnd;
} PetWin;

static HINSTANCE g_inst;
static HWND g_host;
static PetWin *g_pets[MAX_PETS];
static int g_npets;
static NOTIFYICONDATAW g_nid;
static UINT g_msg_taskbar, g_msg_cc;
static UINT g_dpi = 96;

// settings (persisted)
static int g_size = 1, g_mouse_mode = MOUSE_OFF, g_typing_on = 1, g_hide_fullscreen = 1, g_water = 0;
static int g_pomo_focus = 25, g_pomo_break = 5;   // minutes
static int g_feat_pomo = 1, g_feat_water = 1, g_feat_climb = 1;   // features shown in the menu
static int g_feat_update = 1;   // look for a new version now and then
static int g_feat_toast = 1;    // up onto Steam's popups
static int g_focus;              // focus mode: everyone sits quietly at a desk (not kept)
static int g_focus_pomo;         // ...turned on by the pomodoro, which turns it off again
static int g_stair_style = STAIRS_SLAB;

// runtime
static World g_world;
static ULONGLONG g_update_next;   // next automatic check
static int g_update_ready;        // a newer release is out (update_tag())
static char g_update_told[32];    // the one we last told the user about
static PetWin *g_press_pw;
static int g_dragging;
static POINT g_press_pt, g_last_cursor;
static ULONGLONG g_cursor_moved_at, g_last_key_at;
static BYTE g_key_down[256];
static HMONITOR g_fs_mon;
static int g_pomo;                 // 0 off, 1 focus, 2 break
static ULONGLONG g_pomo_end, g_water_next, g_cc_until;
static int g_cc;

static int scale_for(int size)
{
    int s = (int)((SIZE_PX[size] * g_dpi + 48) / 96);
    return s < 1 ? 1 : s;
}

/* ---- settings --------------------------------------------------------- */

static DWORD reg_get_dword(const wchar_t *name, DWORD def)
{
    DWORD v, sz = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, REG_KEY, name, RRF_RT_REG_DWORD, NULL, &v, &sz) == ERROR_SUCCESS)
        return v;
    return def;
}

static void reg_set_dword(const wchar_t *name, DWORD v)
{
    RegSetKeyValueW(HKEY_CURRENT_USER, REG_KEY, name, REG_DWORD, &v, sizeof v);
}

static int clampi(int v, int lo, int hi, int def)
{
    return v < lo || v > hi ? def : v;
}

static int autostart_enabled(void)
{
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, APP_NAME, RRF_RT_REG_SZ, NULL, NULL, NULL) == ERROR_SUCCESS;
}

static void autostart_set(int on)
{
    if (!on) {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, APP_NAME);
        return;
    }
    wchar_t path[MAX_PATH], cmd[MAX_PATH + 3];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    cmd[0] = L'"';
    wcscpy(cmd + 1, path);
    wcscat(cmd, L"\"");
    RegSetKeyValueW(HKEY_CURRENT_USER, RUN_KEY, APP_NAME, REG_SZ, cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
}

/* ---- `clawd` command -------------------------------------------------- */

// A clawd.cmd in its own folder that launches this exe, with only that
// folder on the user PATH, so nothing else next to the exe becomes a command.
#define CLI_DIR_RAW L"%LOCALAPPDATA%\\ClawdPet\\bin"
#define ENV_KEY     L"Environment"

static void cli_paths(wchar_t *dir, wchar_t *cmd)
{
    ExpandEnvironmentStringsW(CLI_DIR_RAW, dir, MAX_PATH);
    _snwprintf(cmd, MAX_PATH, L"%ls\\clawd.cmd", dir);
    cmd[MAX_PATH - 1] = 0;
}

// The user PATH, unexpanded (caller frees). Empty string if there is none.
static wchar_t *user_path_get(void)
{
    DWORD sz = 0;
    DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND;
    RegGetValueW(HKEY_CURRENT_USER, ENV_KEY, L"Path", flags, NULL, NULL, &sz);
    wchar_t *v = calloc(sz / sizeof(wchar_t) + 1, sizeof(wchar_t));
    if (sz && RegGetValueW(HKEY_CURRENT_USER, ENV_KEY, L"Path", flags, NULL, v, &sz) != ERROR_SUCCESS) v[0] = 0;
    return v;
}

static int is_cli_entry(const wchar_t *e, size_t n, const wchar_t *dir)
{
    while (n && e[n - 1] == L'\\') n--;
    return (n == wcslen(CLI_DIR_RAW) && !_wcsnicmp(e, CLI_DIR_RAW, n)) ||
           (n == wcslen(dir) && !_wcsnicmp(e, dir, n));
}

// Adds or removes our folder in the user PATH, keeping every other entry.
static int user_path_set(int add)
{
    wchar_t dir[MAX_PATH], cmd[MAX_PATH];
    cli_paths(dir, cmd);
    wchar_t *old = user_path_get();
    size_t cap = wcslen(old) + wcslen(CLI_DIR_RAW) + 2;
    wchar_t *out = calloc(cap, sizeof(wchar_t));
    int found = 0;
    for (wchar_t *e = old; *e;) {
        wchar_t *semi = wcschr(e, L';');
        size_t n = semi ? (size_t)(semi - e) : wcslen(e);
        if (is_cli_entry(e, n, dir)) found = 1;
        else if (n) {
            if (*out) wcscat(out, L";");
            wcsncat(out, e, n);
        }
        e += n + (semi != NULL);
    }
    if (add) {
        if (*out) wcscat(out, L";");
        wcscat(out, CLI_DIR_RAW);
    }
    int changed = add || found;
    if (changed) {
        RegSetKeyValueW(HKEY_CURRENT_USER, ENV_KEY, L"Path", REG_EXPAND_SZ, out,
                        (DWORD)((wcslen(out) + 1) * sizeof(wchar_t)));
        // Tell Explorer, so terminals opened from now on see it.
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)ENV_KEY, SMTO_ABORTIFHUNG, 1000, NULL);
    }
    free(old);
    free(out);
    return found;
}

static int cli_enabled(void)
{
    wchar_t dir[MAX_PATH], cmd[MAX_PATH];
    cli_paths(dir, cmd);
    if (GetFileAttributesW(cmd) == INVALID_FILE_ATTRIBUTES) return 0;
    wchar_t *path = user_path_get();
    int on = 0;
    for (wchar_t *e = path; *e && !on;) {
        wchar_t *semi = wcschr(e, L';');
        size_t n = semi ? (size_t)(semi - e) : wcslen(e);
        on = is_cli_entry(e, n, dir);
        e += n + (semi != NULL);
    }
    free(path);
    return on;
}

// (Re)writes clawd.cmd to point at wherever this exe is now.
static int cli_write_shim(void)
{
    wchar_t dir[MAX_PATH], cmd[MAX_PATH], exe[MAX_PATH], shortexe[MAX_PATH];
    cli_paths(dir, cmd);
    GetModuleFileNameW(NULL, exe, MAX_PATH);

    // cmd.exe reads batch files in the OEM code page; fall back to the 8.3
    // name for paths it can't spell.
    char path[MAX_PATH * 2], text[MAX_PATH * 2 + 128];
    BOOL lossy = FALSE;
    WideCharToMultiByte(CP_OEMCP, 0, exe, -1, path, sizeof path, NULL, &lossy);
    if (lossy && GetShortPathNameW(exe, shortexe, MAX_PATH))
        WideCharToMultiByte(CP_OEMCP, 0, shortexe, -1, path, sizeof path, NULL, NULL);
    int len = snprintf(text, sizeof text,
                       "@echo off\r\nrem Made by Clawd (menu: clawd command). Launches the desktop pet.\r\n"
                       "start \"\" \"%s\" %%*\r\n", path);

    SHCreateDirectoryExW(NULL, dir, NULL);
    HANDLE f = CreateFileW(cmd, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD wrote;
    WriteFile(f, text, (DWORD)len, &wrote, NULL);
    CloseHandle(f);
    return 1;
}

static void cli_set(int on)
{
    wchar_t dir[MAX_PATH], cmd[MAX_PATH];
    cli_paths(dir, cmd);
    if (on) {
        if (cli_write_shim()) user_path_set(1);
        return;
    }
    DeleteFileW(cmd);
    RemoveDirectoryW(dir);   // and ClawdPet\, if nothing else is in them
    *wcsrchr(dir, L'\\') = 0;
    RemoveDirectoryW(dir);
    user_path_set(0);
}

/* ---- drawing ---------------------------------------------------------- */

static HBITMAP make_dib(int w, int h, uint32_t **bits)
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

static void ensure_dib(PetWin *pw, int k)
{
    if (pw->dib && pw->dib_k == k) return;
    uint32_t *bits;
    HBITMAP dib = make_dib(CW * k, CH * k, &bits);
    SelectObject(pw->memdc, dib);
    if (pw->dib) DeleteObject(pw->dib);
    pw->dib = dib;
    pw->bits = bits;
    pw->dib_k = k;
    pw->dirty = 1;
}

static void present(PetWin *pw)
{
    int k = pw->pet.scale;
    ensure_dib(pw, k);
    Canvas cv = { pw->px, CW, CH };
    pet_render(&pw->pet, &cv);

    POINT pos = { (LONG)floor(pw->pet.x + 0.5), (LONG)floor(pw->pet.y + 0.5) };
    if (!pw->dirty && pos.x == pw->prev_pos.x && pos.y == pw->prev_pos.y &&
        !memcmp(pw->px, pw->prev_px, sizeof pw->px))
        return;
    pw->dirty = 0;
    pw->prev_pos = pos;
    memcpy(pw->prev_px, pw->px, sizeof pw->px);

    cv_scale_premul(&cv, pw->bits, k);
    SIZE sz = { CW * k, CH * k };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(pw->hwnd, NULL, &pos, &sz, pw->memdc, &src, 0, &bf, ULW_ALPHA);
}

static void stairs_destroy(PetWin *pw)
{
    if (!pw->st_hwnd) return;
    DestroyWindow(pw->st_hwnd);
    DeleteDC(pw->st_dc);
    if (pw->st_dib) DeleteObject(pw->st_dib);
    pw->st_hwnd = NULL;
    pw->st_dc = NULL;
    pw->st_dib = NULL;
}

static void present_stairs(PetWin *pw)
{
    Canvas cv = { pw->st_px, 0, 0 };
    POINT pos;
    if (!pet_stairs(&pw->pet, g_stair_style, &cv, &pos)) {
        stairs_destroy(pw);
        return;
    }
    int k = pw->pet.stairs.scale, first = !pw->st_hwnd;
    if (first) {
        pw->st_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                                      WS_EX_TRANSPARENT, L"ClawdStairs", L"", WS_POPUP, 0, 0, 1, 1,
                                      NULL, NULL, g_inst, NULL);
        pw->st_dc = CreateCompatibleDC(NULL);
    }
    if (first || cv.w != pw->st_w || cv.h != pw->st_h || k != pw->st_k) {
        HBITMAP dib = make_dib(cv.w * k, cv.h * k, &pw->st_bits);
        SelectObject(pw->st_dc, dib);
        if (pw->st_dib) DeleteObject(pw->st_dib);
        pw->st_dib = dib;
        pw->st_w = cv.w;
        pw->st_h = cv.h;
        pw->st_k = k;
    } else if (pos.x == pw->st_pos.x && pos.y == pw->st_pos.y &&
               !memcmp(pw->st_px, pw->st_prev, sizeof(uint32_t) * cv.w * cv.h)) {
        return;
    }
    pw->st_pos = pos;
    memcpy(pw->st_prev, pw->st_px, sizeof(uint32_t) * cv.w * cv.h);

    cv_scale_premul(&cv, pw->st_bits, k);
    SIZE sz = { cv.w * k, cv.h * k };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(pw->st_hwnd, NULL, &pos, &sz, pw->st_dc, &src, 0, &bf, ULW_ALPHA);
    if (first)   // just below its pet
        SetWindowPos(pw->st_hwnd, pw->hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void present_catch(PetWin *pw)
{
    RECT r;
    if (pw->hidden || !pet_catch_area(&pw->pet, &r)) {
        if (pw->catch_hwnd && IsWindowVisible(pw->catch_hwnd)) ShowWindow(pw->catch_hwnd, SW_HIDE);
        return;
    }
    if (!pw->catch_hwnd) {
        pw->catch_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                         L"ClawdCatch", L"", WS_POPUP, 0, 0, 1, 1, NULL, NULL, g_inst, NULL);
        SetWindowLongPtrW(pw->catch_hwnd, GWLP_USERDATA, (LONG_PTR)pw);
        SetLayeredWindowAttributes(pw->catch_hwnd, 0, 1, LWA_ALPHA);   // alpha 1: hit by the mouse, unseen
    }
    SetWindowPos(pw->catch_hwnd, pw->hwnd, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static HICON make_icon(int size)
{
    // The sprite's visible part is 16x10 (x 1..16), so it fits a 16px icon exactly.
    int k = size / 16 < 1 ? 1 : size / 16;
    uint32_t lp[16 * 10];
    Canvas c = { lp, 16, 10 };
    Pose q = pose_stand();
    cv_clear(&c);
    draw_clawd(&c, -1, 0, &q);

    uint32_t *bits;
    HBITMAP color = make_dib(size, size, &bits);
    memset(bits, 0, (size_t)size * size * 4);
    int ox = (size - 16 * k) / 2, oy = (size - 10 * k) / 2;
    for (int y = 0; y < 10 * k; y++)
        for (int x = 0; x < 16 * k; x++)
            bits[(oy + y) * size + ox + x] = lp[(y / k) * 16 + x / k];

    void *zeros = calloc((size_t)((size + 15) / 16 * 2) * size, 1);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, zeros);
    ICONINFO ii = { TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    free(zeros);
    return icon;
}

/* ---- pets ------------------------------------------------------------- */

static PetWin *spawn_pet(int drop_in)
{
    if (g_npets >= MAX_PETS) return NULL;
    PetWin *pw = calloc(1, sizeof *pw);
    pet_init(&pw->pet, scale_for(g_size), GetTickCount() ^ ((unsigned)g_npets * 0x9E3779B9u));
    pw->pet.typist = !g_npets;   // only the first one types along
    if (drop_in) pw->pet.y = pw->pet.area.top - CH * pw->pet.scale;   // falls from the sky
    pw->prev_pos.x = pw->prev_pos.y = INT_MIN;
    pw->dirty = 1;
    pw->memdc = CreateCompatibleDC(NULL);
    pw->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                               APP_NAME, L"Clawd", WS_POPUP,
                               (int)pw->pet.x, (int)pw->pet.y, CW * pw->pet.scale, CH * pw->pet.scale,
                               NULL, NULL, g_inst, NULL);
    SetWindowLongPtrW(pw->hwnd, GWLP_USERDATA, (LONG_PTR)pw);
    present(pw);
    ShowWindow(pw->hwnd, SW_SHOWNOACTIVATE);
    g_pets[g_npets++] = pw;
    return pw;
}

// Pets that are not on their way out.
static int active_pets(void)
{
    int n = 0;
    for (int i = 0; i < g_npets; i++) n += g_pets[i]->pet.state != ST_LEAVE;
    return n;
}

// Send the newest pet away; it is destroyed once it has fallen off screen.
// g_pets[0] (the talker) always stays.
static void send_away_pet(void)
{
    if (active_pets() <= 1) return;
    for (int i = g_npets - 1; i > 0; i--)
        if (g_pets[i]->pet.state != ST_LEAVE) {
            pet_leave(&g_pets[i]->pet);
            return;
        }
}

static void destroy_pet(int idx)
{
    PetWin *pw = g_pets[idx];
    if (g_press_pw == pw) g_press_pw = NULL;
    tb_destroy(&pw->tb);
    stairs_destroy(pw);
    if (pw->catch_hwnd) DestroyWindow(pw->catch_hwnd);
    DestroyWindow(pw->hwnd);
    DeleteDC(pw->memdc);
    if (pw->dib) DeleteObject(pw->dib);
    free(pw);
    memmove(&g_pets[idx], &g_pets[idx + 1], sizeof g_pets[0] * (size_t)(g_npets - idx - 1));
    g_npets--;
}

// The first Clawd does the talking; everyone cheers.
static void announce(const wchar_t *text, DWORD ms, int cheer)
{
    if (g_npets) tb_show(&g_pets[0]->tb, text, ms);
    if (cheer)
        for (int i = 0; i < g_npets; i++) pet_cheer(&g_pets[i]->pet);
}

/* ---- sensing ---------------------------------------------------------- */

// Keys worth counting as "typing". Polled, so no global hook and nothing
// about which key it was is kept beyond this function.
static int is_typing_key(int vk)
{
    return vk == VK_BACK || vk == VK_RETURN || vk == VK_SPACE ||
           (vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') ||
           (vk >= VK_OEM_1 && vk <= VK_OEM_3) || (vk >= VK_OEM_4 && vk <= VK_OEM_7);
}

/*
 * Steam's popups: SDL_app windows titled notificationtoasts_<n>_desktop.
 * One slides up to just above the taskbar, sits there about 5 s, then slides
 * back down out of sight.
 */
static HWND g_toast_hwnd;
static RECT g_toast_last;
static ULONGLONG g_toast_moved_at, g_toast_still_at;   // still_at: 0 while it is coming up

static HWND find_toast(RECT *r)
{
    HWND h = NULL;
    while ((h = FindWindowExW(NULL, h, L"SDL_app", NULL))) {
        wchar_t t[64];
        if (!IsWindowVisible(h) || !GetWindowTextW(h, t, 64) || wcsncmp(t, L"notificationtoasts_", 19)) continue;
        GetWindowRect(h, r);
        if (r->right > r->left && r->bottom > r->top) return h;
    }
    return NULL;
}

static void sample_toast(void)
{
    ULONGLONG now = GetTickCount64();
    RECT r;
    HWND h = g_feat_toast ? find_toast(&r) : NULL;
    if (!h) {
        g_toast_hwnd = NULL;
        g_world.toast = TOAST_NONE;
        return;
    }
    if (h != g_toast_hwnd) {   // a new one
        g_toast_hwnd = h;
        g_world.toast_id++;
        g_world.toast = TOAST_UP;
        g_toast_moved_at = now;
        g_toast_still_at = 0;
    } else if (memcmp(&r, &g_toast_last, sizeof r)) {
        if (g_toast_still_at && r.top > g_toast_last.top) g_world.toast = TOAST_DOWN;   // on its way out
        g_toast_moved_at = now;
    } else if (!g_toast_still_at && now - g_toast_moved_at >= 300) {
        g_toast_still_at = g_toast_moved_at;   // has come up
    }
    g_toast_last = r;
    g_world.toast_rc = r;
    g_world.toast_ms = g_toast_still_at ? (DWORD)(now - g_toast_still_at) : 0;
}

static void sample_input(void)
{
    ULONGLONG now = GetTickCount64();
    POINT c;
    GetCursorPos(&c);
    if (c.x != g_last_cursor.x || c.y != g_last_cursor.y) {
        g_last_cursor = c;
        g_cursor_moved_at = now;
    }
    g_world.cursor = c;
    g_world.cursor_still_ms = (DWORD)(now - g_cursor_moved_at);

    int keys = 0;
    if (g_typing_on)
        for (int vk = VK_BACK; vk <= VK_OEM_7; vk++) {
            if (!is_typing_key(vk)) continue;
            BYTE down = (GetAsyncKeyState(vk) & 0x8000) != 0;
            if (down && !g_key_down[vk]) keys++;
            g_key_down[vk] = down;
        }
    if (keys) g_last_key_at = now;
    g_world.keys = keys;
    g_world.typing = g_typing_on && g_last_key_at && now - g_last_key_at < 1500;
    g_world.mouse_mode = g_mouse_mode;
    g_world.cc = g_cc;
    g_world.climb = g_feat_climb;
    g_world.focus = g_focus;
    sample_toast();
}

static ULONGLONG ft64(FILETIME f)
{
    return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime;
}

static double sample_cpu(void)
{
    static ULONGLONG pi, pk, pu;
    FILETIME fi, fk, fu;
    if (!GetSystemTimes(&fi, &fk, &fu)) return 0;
    ULONGLONG i = ft64(fi), k = ft64(fk), u = ft64(fu);
    ULONGLONG di = i - pi, total = (k - pk) + (u - pu);   // kernel time includes idle
    int first = pi == 0;
    pi = i; pk = k; pu = u;
    if (first || !total) return 0;
    double busy = 1.0 - (double)di / (double)total;
    return busy < 0 ? 0 : busy > 1 ? 1 : busy;
}

// Hottest ACPI thermal zone in °C via the "Thermal Zone Information"
// performance counter (no admin needed), or -1 if there is none.
static double sample_temp(void)
{
    static PDH_HQUERY q;
    static PDH_HCOUNTER c;
    static int ok = -1;
    if (ok < 0)
        ok = PdhOpenQueryW(NULL, 0, &q) == ERROR_SUCCESS &&
             PdhAddEnglishCounterW(q, L"\\Thermal Zone Information(*)\\Temperature", 0, &c) == ERROR_SUCCESS;
    if (!ok || PdhCollectQueryData(q) != ERROR_SUCCESS) return -1;

    DWORD sz = 0, n = 0;
    PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE, &sz, &n, NULL);
    PDH_FMT_COUNTERVALUE_ITEM_W *items = sz ? malloc(sz) : NULL;
    double best = -1;
    if (items && PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE, &sz, &n, items) == ERROR_SUCCESS)
        for (DWORD i = 0; i < n; i++) {
            double t = items[i].FmtValue.doubleValue - 273.15;   // reported in kelvin
            if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA && t > best && t < 150) best = t;
        }
    free(items);
    return best;
}

// How hot Clawd feels (World.heat). Goes by temperature, with a few degrees
// of hysteresis; on machines whose thermal zones never read like a CPU
// (missing, or stuck at room temperature) by CPU load instead.
static int sample_heat(void)
{
    static int trusted, level;
    double t = sample_temp();
    if (t >= 45) trusted = 1;
    if (!trusted || t < 0) return g_world.cpu > 0.8;
    int up = t >= 85 ? 2 : t >= 75 ? 1 : 0, down = t >= 82 ? 2 : t >= 72 ? 1 : 0;
    if (up > level) level = up;
    else if (down < level) level = down;
    return level;
}

static int is_own_window(HWND h)
{
    wchar_t cls[32];
    GetClassNameW(h, cls, 32);
    return !wcscmp(cls, APP_NAME) || !wcscmp(cls, L"ClawdBubble") || !wcscmp(cls, HOST_CLASS) ||
           !wcscmp(cls, L"ClawdPicker") || !wcscmp(cls, L"ClawdStairs") || !wcscmp(cls, L"ClawdWall") ||
           !wcscmp(cls, L"ClawdWallDraw") || !wcscmp(cls, L"ClawdWardrobe") || !wcscmp(cls, L"ClawdCatch");
}

// Monitor covered by a fullscreen foreground window (game, video, slides), or NULL.
static HMONITOR fullscreen_monitor(void)
{
    HWND fg = GetForegroundWindow();
    if (!fg || is_own_window(fg)) return g_fs_mon;
    wchar_t cls[32];
    GetClassNameW(fg, cls, 32);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd"))
        return NULL;
    RECT r;
    HMONITOR m = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof mi };
    if (!GetWindowRect(fg, &r) || !GetMonitorInfoW(m, &mi)) return NULL;
    RECT s = mi.rcMonitor;
    return r.left <= s.left && r.top <= s.top && r.right >= s.right && r.bottom >= s.bottom ? m : NULL;
}

static HMONITOR pet_monitor(const Pet *p)
{
    POINT c = { (LONG)p->x + CW * p->scale / 2, (LONG)p->y + FOOT_Y * p->scale - 1 };
    return MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST);
}

/* ---- tray & timers ---------------------------------------------------- */

// The pomodoro's focus time is over, or it was stopped: so is the focus
// mode it turned on.
static void pomo_focus_over(void)
{
    if (g_focus_pomo) g_focus = g_focus_pomo = 0;
}

static void pomo_status(wchar_t *buf, int n)
{
    buf[0] = 0;
    if (!g_pomo) {
        if (g_focus) wcsncpy(buf, L"专注模式", n);
        return;
    }
    ULONGLONG now = GetTickCount64();
    int mins = now >= g_pomo_end ? 0 : (int)((g_pomo_end - now + 59999) / 60000);
    _snwprintf(buf, n, L"%ls · 还剩 %d 分钟", g_pomo == 1 ? L"专注中" : L"休息中", mins);
    buf[n - 1] = 0;
}

static void update_tooltip(void)
{
    wchar_t tip[128], st[64];
    pomo_status(st, 64);
    _snwprintf(tip, 128, st[0] ? L"Clawd\n%ls" : L"Clawd%ls", st);
    tip[127] = 0;
    if (!wcscmp(tip, g_nid.szTip)) return;
    wcsncpy(g_nid.szTip, tip, 127);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void tray_add(void)
{
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_host;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    if (!g_nid.hIcon) g_nid.hIcon = make_icon(GetSystemMetrics(SM_CXSMICON));
    wcscpy(g_nid.szTip, L"Clawd");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static DWORD user_idle_ms(void)
{
    LASTINPUTINFO li = { sizeof li };
    GetLastInputInfo(&li);
    return GetTickCount() - li.dwTime;
}

static void every_second(void)
{
    ULONGLONG now = GetTickCount64();
    if (now >= g_update_next) {   // a minute after starting, then once a day
        g_update_next = now + 24ULL * 3600 * 1000;
        update_cleanup();
        if (g_feat_update) update_check(g_host, WM_UPDATE, 0);
    }
    g_world.cpu = g_world.cpu * 0.5 + sample_cpu() * 0.5;
    g_world.heat = sample_heat();

    if (g_cc && now >= g_cc_until) g_cc = CLAUDE_IDLE;

    if (g_pomo && now >= g_pomo_end) {
        if (g_pomo == 1) {
            wchar_t msg[64];
            _snwprintf(msg, 64, L"专注 %d 分钟完成！\n起来休息 %d 分钟吧", g_pomo_focus, g_pomo_break);
            msg[63] = 0;
            g_pomo = 2;
            g_pomo_end = now + (ULONGLONG)g_pomo_break * 60000;
            pomo_focus_over();
            announce(msg, 60000, 1);
        } else {
            g_pomo = 0;
            announce(L"休息结束，继续加油！", 15000, 1);
        }
    }

    // Only remind someone who is actually at the computer.
    if (g_feat_water && g_water && now >= g_water_next && user_idle_ms() < 60000) {
        g_water_next = now + (ULONGLONG)WATER_MIN[g_water] * 60000;
        announce(L"该喝水啦！", 15000, 1);
    }

    update_tooltip();
}

static void tick(void)
{
    static unsigned n;
    static ULONGLONG last_sec;
    n++;
    sample_input();
    ULONGLONG now = GetTickCount64();
    if (now - last_sec >= 1000) {
        last_sec = now;
        every_second();
    }
    if (n % 15 == 0) {
        g_fs_mon = g_hide_fullscreen ? fullscreen_monitor() : NULL;
        walls_hide_on(g_fs_mon);
    }
    if (n % 60 == 0) walls_raise();   // the pets go back on top of them just below

    // Move everyone, sort out what they do together, then draw.
    Pet *live[MAX_PETS];
    int nlive = 0;
    for (int i = 0; i < g_npets; i++) {
        PetWin *pw = g_pets[i];
        int hide = g_fs_mon && pet_monitor(&pw->pet) == g_fs_mon && g_press_pw != pw;
        if (hide != pw->hidden) {
            pw->hidden = hide;
            pw->dirty = 1;
            ShowWindow(pw->hwnd, hide ? SW_HIDE : SW_SHOWNOACTIVATE);
            if (hide) {
                tb_suspend(&pw->tb);
                stairs_destroy(pw);   // rebuilt from the pet's state when it comes back
                present_catch(pw);
            }
        }
        if (hide) {
            if (pw->pet.state == ST_LEAVE) pw->pet.gone = 1;   // nobody to see the fall
            continue;
        }
        pet_update(&pw->pet, &g_world);
        live[nlive++] = &pw->pet;
    }
    pets_together(live, nlive, &g_world);
    int dressing = wardrobe_pet(), found = 0;   // close the wardrobe if its pet has gone
    for (int i = 0; i < nlive; i++) found |= live[i]->id == dressing && live[i]->state != ST_LEAVE;
    if (dressing && !found) wardrobe_close();
    for (int i = 0; i < g_npets; i++) {
        PetWin *pw = g_pets[i];
        if (pw->hidden) continue;
        present(pw);
        present_stairs(pw);
        present_catch(pw);
        tb_update(&pw->tb, pet_head(&pw->pet), pw->pet.area, g_dpi);
        if (n % 60 == 0) {   // the taskbar likes to climb above us; text bubbles go above pets
            SetWindowPos(pw->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (pw->st_hwnd)
                SetWindowPos(pw->st_hwnd, pw->hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (pw->tb.shown)
                SetWindowPos(pw->tb.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    // Everyone else jumps out of their skin when one drops through the taskbar.
    for (int i = 0; i < g_npets; i++) {
        if (!g_pets[i]->pet.dropped) continue;
        g_pets[i]->pet.dropped = 0;
        for (int j = 0; j < g_npets; j++)
            if (j != i) pet_startle(&g_pets[j]->pet, g_pets[i]->pet.x);
    }
    for (int i = g_npets - 1; i >= 0; i--)
        if (g_pets[i]->pet.gone) destroy_pet(i);
}

static void on_cc_event(int ev)
{
    ULONGLONG now = GetTickCount64();
    switch (ev) {
    case CC_EV_PROMPT:
    case CC_EV_TOOL:
        if (g_cc == CLAUDE_WAITING && g_npets) tb_hide(&g_pets[0]->tb);
        g_cc = CLAUDE_WORKING;
        g_cc_until = now + CC_WORK_MS;
        break;
    case CC_EV_PERMISSION:
        g_cc = CLAUDE_WAITING;
        g_cc_until = now + CC_WAIT_MS;
        announce(L"Claude 在等你确认～", 8000, 0);
        break;
    case CC_EV_STOP:
        g_cc = CLAUDE_IDLE;
        announce(L"搞定啦！", 4000, 1);
        break;
    case CC_EV_END:
        g_cc = CLAUDE_IDLE;
        break;
    }
    g_world.cc = g_cc;
}

/* ---- menu ------------------------------------------------------------- */

static void set_size(int size)
{
    g_size = size;
    reg_set_dword(L"Size", (DWORD)size);
    for (int i = 0; i < g_npets; i++) pet_set_scale(&g_pets[i]->pet, scale_for(size));
}

// Pets stand in front of walls, and the drawing hint can go.
static void wall_drawn(void)
{
    if (g_npets) tb_hide(&g_pets[0]->tb);
    for (int i = 0; i < g_npets; i++)
        SetWindowPos(g_pets[i]->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// Called by the picker's start button.
static void pomo_start(int focus, int brk)
{
    wchar_t msg[64];
    g_pomo_focus = focus;
    g_pomo_break = brk;
    reg_set_dword(L"PomoFocus", (DWORD)focus);
    reg_set_dword(L"PomoBreak", (DWORD)brk);
    g_pomo = 1;
    g_pomo_end = GetTickCount64() + (ULONGLONG)focus * 60000;
    if (!g_focus) g_focus = g_focus_pomo = 1;
    _snwprintf(msg, 64, L"开始专注 %d 分钟！", focus);
    msg[63] = 0;
    announce(msg, 4000, 0);
    update_tooltip();
}

// Called by the wardrobe on every change.
static void dressed(int id, const unsigned char *wear)
{
    for (int i = 0; i < g_npets; i++)
        if (g_pets[i]->pet.id == id) pet_dress(&g_pets[i]->pet, wear);
}

#define CHECK(cond) ((cond) ? MF_CHECKED : 0)

static void show_menu(PetWin *pw)
{
    wchar_t st[64];
    HMENU m = CreatePopupMenu(), sz = CreatePopupMenu(), mouse = CreatePopupMenu(), feat = CreatePopupMenu();

    AppendMenuW(sz, MF_STRING | CHECK(g_size == 0), IDM_SIZE_S, L"小");
    AppendMenuW(sz, MF_STRING | CHECK(g_size == 1), IDM_SIZE_M, L"中");
    AppendMenuW(sz, MF_STRING | CHECK(g_size == 2), IDM_SIZE_L, L"大");

    AppendMenuW(mouse, MF_STRING | CHECK(g_mouse_mode == MOUSE_OFF), IDM_MOUSE_OFF, L"不理鼠标");
    AppendMenuW(mouse, MF_STRING | CHECK(g_mouse_mode == MOUSE_FOLLOW), IDM_MOUSE_FOLLOW, L"跟着鼠标走");
    AppendMenuW(mouse, MF_STRING | CHECK(g_mouse_mode == MOUSE_SHY), IDM_MOUSE_SHY, L"怕鼠标");

    AppendMenuW(feat, MF_STRING | CHECK(g_feat_pomo), IDM_FEAT_POMO, L"番茄钟");
    AppendMenuW(feat, MF_STRING | CHECK(g_feat_water), IDM_FEAT_WATER, L"喝水提醒");
    AppendMenuW(feat, MF_STRING | CHECK(g_feat_climb), IDM_FEAT_CLIMB, L"爬楼梯");
    AppendMenuW(feat, MF_STRING | CHECK(g_feat_toast), IDM_FEAT_TOAST, L"跳上 Steam 弹窗");
    AppendMenuW(feat, MF_STRING | CHECK(g_feat_update), IDM_FEAT_UPDATE, L"自动检查更新");

    int paused = pw ? pw->pet.paused : g_pets[0]->pet.paused, can_climb = 0;
    for (int i = 0; i < g_npets; i++)
        if (!pw || g_pets[i] == pw) can_climb |= pet_can_climb(&g_pets[i]->pet);
    // Settings live in one submenu; the top level is things to do right now.
    HMENU set = CreatePopupMenu();
    AppendMenuW(set, MF_POPUP, (UINT_PTR)sz, L"大小");
    if (g_feat_climb) {
        HMENU st = CreatePopupMenu();
        AppendMenuW(st, MF_STRING | CHECK(g_stair_style == STAIRS_SLAB), IDM_STAIRS_SLAB, L"悬空石板");
        AppendMenuW(st, MF_STRING | CHECK(g_stair_style == STAIRS_SOLID), IDM_STAIRS_SOLID, L"实心石砖");
        AppendMenuW(st, MF_STRING | CHECK(g_stair_style == STAIRS_OUTLINE), IDM_STAIRS_OUTLINE, L"只有轮廓线");
        AppendMenuW(set, MF_POPUP, (UINT_PTR)st, L"楼梯样式");
    }
    AppendMenuW(set, MF_POPUP, (UINT_PTR)mouse, L"鼠标互动");
    AppendMenuW(set, MF_POPUP, (UINT_PTR)feat, L"功能开关");
    AppendMenuW(set, MF_SEPARATOR, 0, NULL);
    AppendMenuW(set, MF_STRING | CHECK(g_typing_on), IDM_TYPING, L"打字时陪你敲");
    AppendMenuW(set, MF_STRING | CHECK(g_hide_fullscreen), IDM_FULLSCREEN, L"全屏时躲起来");
    AppendMenuW(set, MF_STRING | CHECK(autostart_enabled()), IDM_AUTOSTART, L"开机自启");
    AppendMenuW(set, MF_STRING | CHECK(cli_enabled()), IDM_CLI, L"终端里可用 clawd 命令");
    AppendMenuW(set, MF_SEPARATOR, 0, NULL);
    AppendMenuW(set, MF_STRING, IDM_UPDATE_CHECK, L"检查更新（当前 v" CLAWD_VERSION L"）");

    // In focus mode they stay at their desks: no flips, slides, games or climbing.
    UINT busy = g_focus ? MF_GRAYED : 0;
    AppendMenuW(m, MF_STRING | CHECK(g_focus), IDM_FOCUS, L"专注模式");
    AppendMenuW(m, MF_STRING | CHECK(paused), IDM_PAUSE, L"待在原地");
    AppendMenuW(m, MF_STRING, IDM_SLEEP, L"去睡觉");
    AppendMenuW(m, MF_STRING | busy, IDM_FLIP, L"后空翻");
    AppendMenuW(m, MF_STRING | busy, IDM_SLIDE, L"滑一下");
    AppendMenuW(m, MF_STRING, IDM_WARDROBE, L"换装…");
    HMENU play = CreatePopupMenu();
    AppendMenuW(play, MF_STRING, IDM_PLAY_GREET, L"打招呼聊天");
    AppendMenuW(play, MF_STRING, IDM_PLAY_CHASE, L"追着玩");
    AppendMenuW(play, MF_STRING, IDM_PLAY_LINE, L"排队走");
    AppendMenuW(play, MF_STRING, IDM_PLAY_RIDE, L"叠罗汉");
    AppendMenuW(play, MF_STRING, IDM_PLAY_DAZE, L"晕倒求安慰");
    AppendMenuW(m, MF_POPUP | (active_pets() < 2 ? MF_GRAYED : busy), (UINT_PTR)play, L"一起玩");
    if (g_feat_climb) AppendMenuW(m, MF_STRING | (can_climb ? busy : MF_GRAYED), IDM_CLIMB, L"爬上去");
    AppendMenuW(m, MF_STRING | (walls_count() >= MAX_WALLS || walls_drawing() ? MF_GRAYED : 0), IDM_WALL_DRAW,
                L"放置围墙");
    if (walls_count()) AppendMenuW(m, MF_STRING, IDM_WALL_CLEAR, L"拆掉所有围墙");
    if (g_feat_pomo || g_feat_water) AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    if (g_feat_pomo && g_pomo) {
        HMENU pomo = CreatePopupMenu();
        pomo_status(st, 64);
        AppendMenuW(pomo, MF_STRING | MF_GRAYED, 0, st);
        AppendMenuW(pomo, MF_STRING, IDM_POMO_STOP, L"停止");
        AppendMenuW(m, MF_POPUP, (UINT_PTR)pomo, L"番茄钟");
    } else if (g_feat_pomo) {
        AppendMenuW(m, MF_STRING, IDM_POMO_START, L"番茄钟…");
    }
    if (g_feat_water) {
        HMENU water = CreatePopupMenu();
        AppendMenuW(water, MF_STRING | CHECK(g_water == 0), IDM_WATER_OFF, L"关闭");
        AppendMenuW(water, MF_STRING | CHECK(g_water == 1), IDM_WATER_30, L"每 30 分钟");
        AppendMenuW(water, MF_STRING | CHECK(g_water == 2), IDM_WATER_45, L"每 45 分钟");
        AppendMenuW(water, MF_STRING | CHECK(g_water == 3), IDM_WATER_60, L"每 60 分钟");
        AppendMenuW(m, MF_POPUP, (UINT_PTR)water, L"喝水提醒");
    }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g_npets >= MAX_PETS ? MF_GRAYED : 0), IDM_ADD_PET, L"再来一只");
    AppendMenuW(m, MF_STRING | (active_pets() <= 1 ? MF_GRAYED : 0), IDM_REMOVE_PET, L"送走一只");
    if (g_update_ready) {
        wchar_t up[64];
        _snwprintf(up, 64, L"更新到 %hs", update_tag());
        up[63] = 0;
        AppendMenuW(m, MF_STRING, IDM_UPDATE_INSTALL, up);
    }
    AppendMenuW(m, MF_POPUP, (UINT_PTR)set, L"设置");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"退出");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_host);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g_host, NULL);
    PostMessageW(g_host, WM_NULL, 0, 0);
    DestroyMenu(m);

    ULONGLONG now = GetTickCount64();
    switch (cmd) {
    case IDM_FOCUS:
        g_focus = !g_focus;
        g_focus_pomo = 0;   // the user's call now, whatever the pomodoro does
        break;
    case IDM_PAUSE:
        // From a pet: just that one. From the tray: everyone.
        for (int i = 0; i < g_npets; i++)
            if (!pw || g_pets[i] == pw) g_pets[i]->pet.paused = !paused;
        break;
    case IDM_SLEEP:
        for (int i = 0; i < g_npets; i++)
            if (!pw || g_pets[i] == pw) pet_sleep(&g_pets[i]->pet);
        break;
    case IDM_FLIP:
        for (int i = 0; i < g_npets; i++)
            if (!pw || g_pets[i] == pw) pet_flip(&g_pets[i]->pet);
        break;
    case IDM_SLIDE:
        for (int i = 0; i < g_npets; i++)
            if (!pw || g_pets[i] == pw) pet_slide(&g_pets[i]->pet);
        break;
    case IDM_CLIMB:
        for (int i = 0; i < g_npets; i++)
            if (!pw || g_pets[i] == pw) pet_climb(&g_pets[i]->pet);
        break;
    case IDM_WALL_DRAW:
        announce(L"按住鼠标拖一下，画一面墙\n横着拖是横墙，竖着拖是竖墙\n右键或 Esc 取消", 30000, 0);
        walls_draw(scale_for(g_size), wall_drawn);
        break;
    case IDM_WALL_CLEAR:
        walls_clear();
        break;
    case IDM_STAIRS_SLAB:
    case IDM_STAIRS_SOLID:
    case IDM_STAIRS_OUTLINE:
        g_stair_style = cmd - IDM_STAIRS_SLAB;
        reg_set_dword(L"StairStyle", (DWORD)g_stair_style);
        break;
    case IDM_FEAT_UPDATE:
        g_feat_update = !g_feat_update;
        reg_set_dword(L"FeatUpdate", (DWORD)g_feat_update);
        break;
    case IDM_UPDATE_CHECK:
        update_check(g_host, WM_UPDATE, 1);
        break;
    case IDM_UPDATE_INSTALL:
        announce(L"正在下载新版本…", 60000, 0);
        update_install(g_host, WM_UPDATE);
        break;
    case IDM_FEAT_TOAST:
        g_feat_toast = !g_feat_toast;
        reg_set_dword(L"FeatToast", (DWORD)g_feat_toast);
        break;
    case IDM_FEAT_CLIMB:
        g_feat_climb = !g_feat_climb;
        reg_set_dword(L"FeatClimb", (DWORD)g_feat_climb);
        break;
    case IDM_MOUSE_OFF:
    case IDM_MOUSE_FOLLOW:
    case IDM_MOUSE_SHY:
        g_mouse_mode = cmd - IDM_MOUSE_OFF;
        reg_set_dword(L"MouseMode", (DWORD)g_mouse_mode);
        break;
    case IDM_TYPING:
        g_typing_on = !g_typing_on;
        reg_set_dword(L"Typing", (DWORD)g_typing_on);
        break;
    case IDM_PLAY_GREET:
    case IDM_PLAY_CHASE:
    case IDM_PLAY_LINE:
    case IDM_PLAY_RIDE:
    case IDM_PLAY_DAZE: {   // from a pet: that one starts it
        Pet *all[MAX_PETS];
        int n = 0;
        for (int i = 0; i < g_npets; i++)
            if (!g_pets[i]->hidden && g_pets[i]->pet.state != ST_LEAVE) all[n++] = &g_pets[i]->pet;
        if (!pets_play(all, n, pw ? &pw->pet : NULL, cmd - IDM_PLAY_GREET))
            announce(L"它们现在都忙着呢，待会儿再试试～", 3000, 0);
        break;
    }
    case IDM_WARDROBE: {   // from the tray: the first one
        const Pet *p = pw ? &pw->pet : &g_pets[0]->pet;
        wardrobe_open(pet_head(p), p->area, g_dpi, p->id, p->wear, dressed);
        break;
    }
    case IDM_POMO_START: {
        const Pet *p = pw ? &pw->pet : &g_pets[0]->pet;
        picker_open(pet_head(p), p->area, g_dpi, g_pomo_focus, g_pomo_break, pomo_start);
        break;
    }
    case IDM_POMO_STOP:
        g_pomo = 0;
        pomo_focus_over();
        break;
    case IDM_FEAT_POMO:
        g_feat_pomo = !g_feat_pomo;
        g_pomo = 0;
        pomo_focus_over();
        reg_set_dword(L"FeatPomo", (DWORD)g_feat_pomo);
        break;
    case IDM_FEAT_WATER:
        g_feat_water = !g_feat_water;
        g_water_next = now + (ULONGLONG)WATER_MIN[g_water] * 60000;
        reg_set_dword(L"FeatWater", (DWORD)g_feat_water);
        break;
    case IDM_WATER_OFF:
    case IDM_WATER_30:
    case IDM_WATER_45:
    case IDM_WATER_60:
        g_water = cmd - IDM_WATER_OFF;
        g_water_next = now + (ULONGLONG)WATER_MIN[g_water] * 60000;
        reg_set_dword(L"Water", (DWORD)g_water);
        break;
    case IDM_ADD_PET:
        spawn_pet(1);
        reg_set_dword(L"Count", (DWORD)active_pets());
        break;
    case IDM_REMOVE_PET:
        send_away_pet();
        reg_set_dword(L"Count", (DWORD)active_pets());
        break;
    case IDM_SIZE_S:
    case IDM_SIZE_M:
    case IDM_SIZE_L:
        set_size(cmd - IDM_SIZE_S);
        break;
    case IDM_FULLSCREEN:
        g_hide_fullscreen = !g_hide_fullscreen;
        reg_set_dword(L"HideFullscreen", (DWORD)g_hide_fullscreen);
        break;
    case IDM_CLI:
        if (cli_enabled()) {
            cli_set(0);
        } else {
            cli_set(1);
            if (cli_enabled()) announce(L"新开一个终端，\n输入 clawd 就能叫我出来", 6000, 0);
        }
        break;
    case IDM_AUTOSTART:
        autostart_set(!autostart_enabled());
        break;
    case IDM_EXIT:
        DestroyWindow(g_host);
        break;
    }
    update_tooltip();
}

/* ---- windows ---------------------------------------------------------- */

static LRESULT CALLBACK pet_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PetWin *pw = (PetWin *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_LBUTTONDOWN:
        if (!pw) return 0;
        SetCapture(pw->hwnd);   // the pet's own window, even when caught by its catch window
        g_press_pw = pw;
        g_dragging = 0;
        GetCursorPos(&g_press_pt);
        if (pet_catch(&pw->pet, g_press_pt)) g_dragging = 1;   // in the air: caught there and then
        return 0;

    case WM_MOUSEMOVE:
        if (g_press_pw == pw && pw && !g_dragging) {
            POINT c;
            GetCursorPos(&c);
            if (abs(c.x - g_press_pt.x) > 4 || abs(c.y - g_press_pt.y) > 4) {
                g_dragging = 1;
                pet_begin_drag(&pw->pet, g_press_pt);
            }
        }
        return 0;

    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED: {
        if (!pw || g_press_pw != pw) return 0;
        int was_drag = g_dragging;
        g_press_pw = NULL;
        g_dragging = 0;
        if (msg == WM_LBUTTONUP) ReleaseCapture();
        if (was_drag) {
            pet_end_drag(&pw->pet);
        } else if (msg == WM_LBUTTONUP) {
            if (g_cc == CLAUDE_WAITING) {   // "ok, I saw it"
                g_cc = CLAUDE_IDLE;
                tb_hide(&g_pets[0]->tb);
            }
            pet_click(&pw->pet);
        }
        return 0;
    }

    case WM_RBUTTONUP:
        show_menu(pw);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// What update.c found or did.
static void on_update(int result, int manual)
{
    wchar_t msg[128];
    switch (result) {
    case UPD_NEWER:
        g_update_ready = 1;
        if (manual || strcmp(g_update_told, update_tag())) {   // once per version unless asked
            strcpy(g_update_told, update_tag());
            _snwprintf(msg, 128, L"有新版本 %hs 啦！\n右键菜单里点「更新到 %hs」", update_tag(), update_tag());
            msg[127] = 0;
            announce(msg, 10000, 0);
        }
        break;
    case UPD_LATEST:
        if (manual) announce(L"已经是最新版 v" CLAWD_VERSION L" 啦", 4000, 0);
        break;
    case UPD_FAILED:
        if (manual) announce(L"检查更新失败了，网络好像不太通", 4000, 0);
        break;
    case UPD_INSTALLED: {   // start the new one and make way for it
        wchar_t exe[MAX_PATH], cmd[MAX_PATH + 32];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        _snwprintf(cmd, MAX_PATH + 32, L"\"%s\" --updated", exe);
        cmd[MAX_PATH + 31] = 0;
        STARTUPINFOW si = { sizeof si };
        PROCESS_INFORMATION pi;
        if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            DestroyWindow(g_host);
        } else {
            announce(L"新版本下好了，重启一下 Clawd 就能用", 8000, 0);
        }
        break;
    }
    case UPD_INSTALL_FAILED:
        announce(L"自动更新没成功，帮你打开下载页面了", 6000, 0);
        ShellExecuteW(NULL, L"open", update_page(), NULL, NULL, SW_SHOWNORMAL);
        break;
    }
}

static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_UPDATE:
        on_update((int)wp, (int)lp);
        return 0;

    case WM_TIMER:
        tick();
        return 0;

    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) show_menu(NULL);
        return 0;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g_msg_cc && g_msg_cc) {
        on_cc_event((int)wp);
        return 0;
    }
    if (msg == g_msg_taskbar && g_msg_taskbar) {   // explorer restarted
        tray_add();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- dev: pose sheet -------------------------------------------------- */

static int write_sheet(const wchar_t *path)
{
    Pose poses[96];
    int n = pet_demo_poses(poses, 96);
    // Glyphs after the poses, in as many cells as they need.
    int gcell[G_COUNT], gx[G_COUNT], gy[G_COUNT], gcells = 1;
    for (int g = 0, x = 2, y = 3; g < G_COUNT; g++) {
        if (x + glyph_width(g) > CW - 1) {
            x = 2;
            y += 11;
        }
        if (y + glyph_height(g) > CH - 1) {
            x = 2;
            y = 3;
            gcells++;
        }
        gcell[g] = gcells - 1;
        gx[g] = x;
        gy[g] = y;
        x += glyph_width(g) + 3;
    }
    int k = 6, cw = CW * k, ch = CH * k, cols = n + gcells, W = cw * cols, H = ch * 2;
    uint32_t *img = calloc((size_t)W * H, 4), *cell = malloc((size_t)cw * ch * 4);
    uint32_t lp[CW * CH];
    Canvas c = { lp, CW, CH };

    for (int i = 0; i < cols; i++) {
        cv_clear(&c);
        if (i < n) {
            draw_clawd(&c, SPR_OX, SPR_OY, &poses[i]);
        } else {
            static const uint32_t col[G_COUNT] = {
                [G_Z] = 0xFFFFFF, [G_ZBIG] = 0xFFFFFF, [G_EXCL] = 0xFFD23F, [G_QUEST] = 0xFFFFFF,
                [G_HEART] = 0xFF5A78, [G_DROP] = 0x5AB4FF, [G_TEAR] = 0x4FA8FF, [G_WIND] = 0xCFE8FF,
                [G_STAR] = 0xFFD23F, [G_NOTE] = 0x9B7BFF, [G_COFFEE] = 0xB8B8B8, [G_BOLT] = 0xF2A007,
            };
            for (int g = 0; g < G_COUNT; g++)
                if (gcell[g] == i - n) draw_glyph(&c, gx[g], gy[g], g, col[g], 255);
        }
        cv_scale_premul(&c, cell, k);
        for (int row = 0; row < 2; row++) {
            uint32_t bg = row ? 0x2B2B2B : 0xF0EEE6;
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++) {
                    uint32_t s = cell[y * cw + x], a = s >> 24, out = 0xFF000000u;
                    for (int sh = 0; sh < 24; sh += 8)
                        out |= ((((s >> sh) & 255) + ((bg >> sh) & 255) * (255 - a) / 255) & 255) << sh;
                    if (x == 0 || y == 0) out = 0xFF888888u;   // cell grid
                    img[(row * ch + y) * W + i * cw + x] = out;
                }
        }
    }

    FILE *f = _wfopen(path, L"wb");
    if (!f) return 1;
    BITMAPFILEHEADER fh = { 0 };
    BITMAPINFOHEADER ih = { 0 };
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + (DWORD)W * H * 4;
    ih.biSize = sizeof ih;
    ih.biWidth = W;
    ih.biHeight = -H;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&ih, sizeof ih, 1, f);
    fwrite(img, 4, (size_t)W * H, f);
    fclose(f);
    free(img);
    free(cell);
    return 0;
}

/* ---- entry ------------------------------------------------------------ */

// `clawd.exe --cc <event>`: forward a Claude Code hook event to the running pet.
static int send_cc_event(const wchar_t *name)
{
    HWND host = FindWindowW(HOST_CLASS, NULL);
    if (!host) return 0;
    UINT msg = RegisterWindowMessageW(L"ClawdPet.CC");
    for (int i = 0; i < (int)(sizeof CC_EVENTS / sizeof CC_EVENTS[0]); i++)
        if (!wcscmp(name, CC_EVENTS[i])) PostMessageW(host, msg, (WPARAM)(i + 1), 0);
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    (void)prev; (void)cmdline; (void)show;
    g_inst = inst;

    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 3 && !wcscmp(argv[1], L"--sheet")) return write_sheet(argv[2]);
    if (argc >= 3 && !wcscmp(argv[1], L"--cc")) return send_cc_event(argv[2]);

    // Just updated: the old one is still on its way out, so wait for it.
    int updated = argc >= 2 && !wcscmp(argv[1], L"--updated");
    HANDLE one = CreateMutexW(NULL, TRUE, L"ClawdPet.SingleInstance");
    for (int i = 0; updated && GetLastError() == ERROR_ALREADY_EXISTS && i < 50; i++) {
        CloseHandle(one);
        Sleep(100);
        one = CreateMutexW(NULL, TRUE, L"ClawdPet.SingleInstance");
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_dpi = GetDpiForSystem();
    if (cli_enabled()) cli_write_shim();   // follow the exe if it was moved
    g_size = clampi((int)reg_get_dword(L"Size", 1), 0, 2, 1);
    g_mouse_mode = clampi((int)reg_get_dword(L"MouseMode", MOUSE_OFF), 0, 2, MOUSE_OFF);
    g_typing_on = reg_get_dword(L"Typing", 1) != 0;
    g_hide_fullscreen = reg_get_dword(L"HideFullscreen", 1) != 0;
    g_water = clampi((int)reg_get_dword(L"Water", 0), 0, 3, 0);
    g_water_next = GetTickCount64() + (ULONGLONG)WATER_MIN[g_water] * 60000;
    g_feat_pomo = reg_get_dword(L"FeatPomo", 1) != 0;
    g_feat_water = reg_get_dword(L"FeatWater", 1) != 0;
    g_feat_climb = reg_get_dword(L"FeatClimb", 1) != 0;
    g_feat_toast = reg_get_dword(L"FeatToast", 1) != 0;
    g_feat_update = reg_get_dword(L"FeatUpdate", 1) != 0;
    g_update_next = GetTickCount64() + 60000;
    g_stair_style = clampi((int)reg_get_dword(L"StairStyle", STAIRS_SLAB), 0, STAIRS_STYLES - 1, STAIRS_SLAB);
    g_pomo_focus = (int)reg_get_dword(L"PomoFocus", 25);
    if (g_pomo_focus < FOCUS_LO || g_pomo_focus > FOCUS_HI || g_pomo_focus % FOCUS_STEP) g_pomo_focus = 25;
    g_pomo_break = clampi((int)reg_get_dword(L"PomoBreak", 5), BREAK_LO, BREAK_HI, 5);
    int count = clampi((int)reg_get_dword(L"Count", 1), 1, MAX_PETS, 1);

    HICON icon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = pet_proc;
    wc.hInstance = inst;
    wc.hIcon = icon;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    wc.lpszClassName = APP_NAME;
    RegisterClassW(&wc);
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = HOST_CLASS;
    RegisterClassW(&wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = L"ClawdStairs";
    RegisterClassW(&wc);
    wc.lpfnWndProc = pet_proc;   // clicks on it are clicks on its pet
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"ClawdCatch";
    RegisterClassW(&wc);
    wc.hbrBackground = NULL;
    tb_register(inst);
    picker_register(inst);
    wardrobe_register(inst);

    // Hidden top-level window: owns the tray icon, timer and hook messages.
    g_host = CreateWindowExW(WS_EX_TOOLWINDOW, HOST_CLASS, L"Clawd", WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    g_msg_taskbar = RegisterWindowMessageW(L"TaskbarCreated");
    g_msg_cc = RegisterWindowMessageW(L"ClawdPet.CC");
    ChangeWindowMessageFilterEx(g_host, g_msg_cc, MSGFLT_ALLOW, NULL);
    walls_register(inst, g_host);
    walls_load();

    for (int i = 0; i < count; i++) spawn_pet(0);
    tray_add();
    if (updated) {
        update_cleanup();
        announce(L"更新好啦！现在是 v" CLAWD_VERSION, 5000, 1);
    }
    SetTimer(g_host, 1, TICK_MS, NULL);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
