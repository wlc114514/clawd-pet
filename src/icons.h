#pragma once
#include <windows.h>
#include <stdint.h>

/*
 * The icons on the desktop, through the shell's documented IFolderView2
 * interface (the one Explorer's own desktop view hands out). Positions are
 * the top-left of an icon's picture, in screen pixels.
 */
#define ICON_NAME 260

typedef struct {
    wchar_t name[ICON_NAME];   // its name in the desktop folder, which stays the same when it moves
    POINT pt;
} DeskIcon;

// The desktop can be reached and icons go where they are put: not arranged
// automatically by Explorer.
int  icons_ready(void);
void icons_reset(void);   // Explorer restarted: start over
int  icons_size(void);    // px of an icon's picture
// All the icons whose picture is inside *in; returns how many (at most max).
int  icons_list(DeskIcon *out, int max, const RECT *in);
// Where the named icon is; 0 if there is none by that name.
int  icons_find(const wchar_t *name, POINT *pt);
// Empty grid spots inside *in, in the same grid as the icons already there.
int  icons_free_spots(POINT *out, int max, const RECT *in);
// A handle on the named icon for moving it about; -1 if there is none.
int  icons_grab(const wchar_t *name);
void icons_move(int h, POINT pt);
void icons_release(int h);
// The picture the desktop shows for the named icon, size x size straight ARGB
// (malloc'd, the caller frees it); NULL if there is none. Only ever its icon,
// never a thumbnail of what is in the file or folder.
uint32_t *icons_picture(const wchar_t *name, int size);

/*
 * Icons Clawd has moved, and where they were (HKCU\Software\ClawdPet\Icons),
 * so they can be put back, even after a restart. One that is no longer where
 * Clawd left it (the user moved it), or no longer there at all, is let be.
 */
typedef struct {
    wchar_t name[ICON_NAME];
    POINT home, now;
} MovedIcon;

int  moved_load(MovedIcon *out, int max);
// It is now at `now`; `home` is kept from the first time. Back home: forgotten.
void moved_note(const wchar_t *name, POINT home, POINT now);
void moved_forget(const wchar_t *name);
// Drops those the user has moved since, or that are gone; skip(name) says
// which to leave alone (being carried right now). Returns how many are left.
int  moved_check(int (*skip)(const wchar_t *name));
