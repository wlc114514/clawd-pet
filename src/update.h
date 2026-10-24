#pragma once
#include <windows.h>

// Looking for a newer release on GitHub, and swapping this exe for it.
// Both run on a thread of their own and post `msg` to `notify` when done,
// with one of UPD_* as wParam (and for a check, lParam = manual).
enum { UPD_NEWER, UPD_LATEST, UPD_FAILED, UPD_INSTALLED, UPD_INSTALL_FAILED };

// Asks GitHub for the latest release. Ignored while one is running.
void update_check(HWND notify, UINT msg, int manual);
// After UPD_NEWER: its tag ("v1.2.3") and its page on GitHub.
const char *update_tag(void);
const wchar_t *update_page(void);
// Downloads the new clawd.exe next to this one and swaps them over; on
// UPD_INSTALLED the caller starts the new exe and quits.
void update_install(HWND notify, UINT msg);
// Removes the old exe an update left behind.
void update_cleanup(void);
