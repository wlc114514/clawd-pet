#include "update.h"
#include <winhttp.h>
#include <stdlib.h>
#include <string.h>
#include "version.h"

// Not the API (60 asks an hour per IP, easily used up behind a shared one):
// releases/latest just redirects to the newest release's page, whose last
// part is its tag, and the exe sits at a fixed address under that tag.
#define REPO      L"https://github.com/wlc114514/clawd-pet/releases/"
#define MAX_EXE   (32 * 1024 * 1024)

static volatile LONG g_busy;     // a check or an install is running
static HWND g_notify;
static UINT g_msg;
static int g_manual;
static char g_tag[32];           // the newer release found
static wchar_t g_page[160];

const char *update_tag(void)
{
    return g_tag;
}

const wchar_t *update_page(void)
{
    return g_page;
}

/* ---- http ------------------------------------------------------------- */

static void http_close(HINTERNET h[3])
{
    for (int i = 3; i--;)
        if (h[i]) WinHttpCloseHandle(h[i]);
}

// Sends a GET for url, following redirects or not; the session, connection
// and request handles land in h (close them with http_close). Returns the
// answer's status, 0 if there was none.
static DWORD http_open(const wchar_t *url, int follow, HINTERNET h[3])
{
    wchar_t host[256], path[1024];
    URL_COMPONENTS uc = { sizeof uc };
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 1024;
    h[0] = h[1] = h[2] = NULL;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) return 0;

    h[0] = WinHttpOpen(L"clawd-pet/" CLAWD_VERSION, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (h[0]) h[1] = WinHttpConnect(h[0], host, uc.nPort, 0);
    if (h[1]) h[2] = WinHttpOpenRequest(h[1], L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!h[2]) return 0;
    WinHttpSetTimeouts(h[0], 10000, 10000, 15000, 30000);
    if (!follow) {
        DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(h[2], WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof never);
    }
    DWORD status = 0, sz = sizeof status;
    if (!WinHttpSendRequest(h[2], WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(h[2], NULL) ||
        !WinHttpQueryHeaders(h[2], WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &sz, WINHTTP_NO_HEADER_INDEX))
        return 0;
    return status;
}

// GET url into a malloc'd buffer, following redirects. NULL unless the
// answer was 200 and at most `max` bytes.
static char *http_get(const wchar_t *url, DWORD max, DWORD *len)
{
    HINTERNET h[3];
    char *buf = NULL;
    DWORD n = 0;
    if (http_open(url, 1, h) != 200) goto done;
    buf = malloc(max);
    for (;;) {
        DWORD avail = 0, got = 0;
        if (!WinHttpQueryDataAvailable(h[2], &avail)) goto fail;
        if (!avail) break;
        if (avail > max - n) goto fail;   // too big to be what we asked for
        if (!WinHttpReadData(h[2], buf + n, avail, &got)) goto fail;
        n += got;
    }
    *len = n;
    goto done;
fail:
    free(buf);
    buf = NULL;
done:
    http_close(h);
    return buf;
}

/* ---- check ------------------------------------------------------------ */

static DWORD WINAPI check_thread(void *arg)
{
    (void)arg;
    int result = UPD_FAILED;
    HINTERNET h[3];
    DWORD status = http_open(REPO L"latest", 0, h);
    wchar_t where[512];
    DWORD sz = sizeof where;
    if ((status == 301 || status == 302) &&
        WinHttpQueryHeaders(h[2], WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, where, &sz,
                            WINHTTP_NO_HEADER_INDEX)) {
        // .../releases/tag/v1.2.3 (just .../releases while there are none)
        static const wchar_t at[] = REPO L"tag/";
        const wchar_t *tag = where + wcslen(at);
        if (!wcsncmp(where, at, wcslen(at)) && *tag && wcslen(tag) < 32 && !wcspbrk(tag, L"/?#%")) {
            char t[32];
            WideCharToMultiByte(CP_UTF8, 0, tag, -1, t, sizeof t, NULL, NULL);
            result = UPD_LATEST;
            if (version_newer(t, CLAWD_VERSION)) {
                strcpy(g_tag, t);
                lstrcpyW(g_page, where);
                result = UPD_NEWER;
            }
        }
    }
    http_close(h);
    InterlockedExchange(&g_busy, 0);
    PostMessageW(g_notify, g_msg, (WPARAM)result, (LPARAM)g_manual);
    return 0;
}

void update_check(HWND notify, UINT msg, int manual)
{
    if (InterlockedCompareExchange(&g_busy, 1, 0)) return;
    g_notify = notify;
    g_msg = msg;
    g_manual = manual;
    HANDLE t = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    else InterlockedExchange(&g_busy, 0);
}

/* ---- install ---------------------------------------------------------- */

// clawd.exe, clawd.exe.new, clawd.exe.old
static void exe_paths(wchar_t *exe, wchar_t *fresh, wchar_t *old)
{
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wsprintfW(fresh, L"%s.new", exe);
    wsprintfW(old, L"%s.old", exe);
}

static DWORD WINAPI install_thread(void *arg)
{
    (void)arg;
    int result = UPD_INSTALL_FAILED;
    wchar_t exe[MAX_PATH], fresh[MAX_PATH + 8], old[MAX_PATH + 8], url[160];
    exe_paths(exe, fresh, old);
    wsprintfW(url, REPO L"download/%hs/clawd.exe", g_tag);
    DWORD len = 0;
    char *bin = g_tag[0] ? http_get(url, MAX_EXE, &len) : NULL;
    if (bin && len > 100000 && bin[0] == 'M' && bin[1] == 'Z') {   // looks like an exe
        HANDLE f = CreateFileW(fresh, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD wrote = 0;
        int ok = f != INVALID_HANDLE_VALUE && WriteFile(f, bin, len, &wrote, NULL) && wrote == len;
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        // A running exe can't be overwritten, but it can be renamed out of the way.
        DeleteFileW(old);
        if (ok && MoveFileExW(exe, old, MOVEFILE_REPLACE_EXISTING)) {
            if (MoveFileExW(fresh, exe, 0)) result = UPD_INSTALLED;
            else MoveFileExW(old, exe, 0);   // put it back
        }
        if (result != UPD_INSTALLED) DeleteFileW(fresh);
    }
    free(bin);
    InterlockedExchange(&g_busy, 0);
    PostMessageW(g_notify, g_msg, (WPARAM)result, 0);
    return 0;
}

void update_install(HWND notify, UINT msg)
{
    if (InterlockedCompareExchange(&g_busy, 1, 0)) return;
    g_notify = notify;
    g_msg = msg;
    HANDLE t = CreateThread(NULL, 0, install_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    else InterlockedExchange(&g_busy, 0);
}

void update_cleanup(void)
{
    wchar_t exe[MAX_PATH], fresh[MAX_PATH + 8], old[MAX_PATH + 8];
    exe_paths(exe, fresh, old);
    DeleteFileW(old);
}
