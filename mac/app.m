/*
 * The macOS app around the pet: menu bar item and menu, settings, the
 * per-second sensing (CPU, heat, typing, idle), the pomodoro and water
 * reminders, the Claude Code hook socket, and the `clawd` command.
 */
#import "mac.h"
#include "version.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>

#define TICK_MS    33
#define MAX_PETS   6

#define CC_WORK_MS (3 * 60 * 1000)    // no event for this long: assume Claude stopped
#define CC_WAIT_MS (10 * 60 * 1000)

enum {
    IDM_PAUSE = 100, IDM_SLEEP, IDM_SIZE_S, IDM_SIZE_M, IDM_SIZE_L, IDM_AUTOSTART, IDM_EXIT,
    IDM_MOUSE_OFF, IDM_MOUSE_FOLLOW, IDM_MOUSE_SHY, IDM_TYPING, IDM_FULLSCREEN,
    IDM_POMO_START, IDM_POMO_STOP, IDM_WATER_OFF, IDM_WATER_30, IDM_WATER_45, IDM_WATER_60,
    IDM_ADD_PET, IDM_REMOVE_PET, IDM_FEAT_POMO, IDM_FEAT_WATER, IDM_CLIMB, IDM_FEAT_CLIMB,
    IDM_STAIRS_SLAB, IDM_STAIRS_SOLID, IDM_STAIRS_OUTLINE,   // same order as STAIRS_*
    IDM_CLI, IDM_WALL_DRAW, IDM_WALL_CLEAR, IDM_WARDROBE,
    IDM_PLAY_GREET, IDM_PLAY_CHASE, IDM_PLAY_LINE, IDM_PLAY_RIDE, IDM_PLAY_DAZE,   // same order as PLAY_*
    IDM_FLIP, IDM_UPDATE_CHECK, IDM_UPDATE_OPEN, IDM_FEAT_UPDATE, IDM_FOCUS, IDM_SLIDE,
};

// Claude Code hook events, sent by `clawd --cc <name>`.
enum { CC_EV_PROMPT = 1, CC_EV_TOOL, CC_EV_PERMISSION, CC_EV_STOP, CC_EV_END };
static const char *CC_EVENTS[] = { "prompt", "tool", "permission", "stop", "end" };

static const int SIZE_PX[3] = { 3, 4, 6 };   // logical pixel size, in points
static const int WATER_MIN[4] = { 0, 30, 45, 60 };

World g_world;
int g_stair_style = STAIRS_SLAB;

static NSMutableArray<PetWindow *> *g_pets;
static NSStatusItem *g_status;
static NSMenu *g_menu;
static NSSet<NSNumber *> *g_fs;   // displays hidden behind a fullscreen window

// settings (persisted)
static int g_size = 1, g_mouse_mode = MOUSE_OFF, g_typing_on = 1, g_hide_fullscreen = 1, g_water = 0;
static int g_pomo_focus = 25, g_pomo_break = 5;   // minutes
static int g_feat_pomo = 1, g_feat_water = 1, g_feat_climb = 1;   // features shown in the menu
static int g_feat_update = 1;   // look for a new version now and then
static int g_focus;              // focus mode: everyone sits quietly at a desk (not kept)
static int g_focus_pomo;         // ...turned on by the pomodoro, which turns it off again

// runtime
static int g_pomo;                 // 0 off, 1 focus, 2 break
static ULONGLONG g_pomo_end, g_water_next, g_cc_until;
static int g_cc;
static dispatch_source_t g_cc_src;   // held here: a source must outlive the call that made it
static POINT g_last_cursor;
static ULONGLONG g_cursor_moved_at, g_last_key_at;
static unsigned char g_key_down[256];
static ULONGLONG g_update_next;            // next automatic check
static NSString *g_update_tag, *g_update_told;   // a newer release; the one we last told about

/* ---- settings --------------------------------------------------------- */

static int ud_int(NSString *key, int def)
{
    id v = [[NSUserDefaults standardUserDefaults] objectForKey:key];
    return v ? [v intValue] : def;
}

static void ud_set(NSString *key, int v)
{
    [[NSUserDefaults standardUserDefaults] setInteger:v forKey:key];
}

// The pomodoro's focus time is over, or it was stopped: so is the focus
// mode it turned on.
static void pomo_focus_over(void)
{
    if (g_focus_pomo) g_focus = g_focus_pomo = 0;
}

static int clampi(int v, int lo, int hi, int def)
{
    return v < lo || v > hi ? def : v;
}

// Points, not pixels: the pet is the same size it is on a 96 dpi Windows
// desktop, and Retina doubles every pixel for free.
static int scale_for(int size)
{
    return SIZE_PX[size];
}

/* ---- autostart -------------------------------------------------------- */

static NSString *launch_agent_path(void)
{
    return [NSHomeDirectory() stringByAppendingPathComponent:
            @"Library/LaunchAgents/com.github.wlc114514.clawd-pet.plist"];
}

static BOOL autostart_enabled(void)
{
    return [[NSFileManager defaultManager] fileExistsAtPath:launch_agent_path()];
}

static NSString *xml_escape(NSString *s)
{
    return [[[s stringByReplacingOccurrencesOfString:@"&" withString:@"&amp;"]
             stringByReplacingOccurrencesOfString:@"<" withString:@"&lt;"]
            stringByReplacingOccurrencesOfString:@">" withString:@"&gt;"];
}

static void autostart_set(BOOL on)
{
    NSString *path = launch_agent_path();
    NSFileManager *fm = [NSFileManager defaultManager];
    if (!on) {
        [fm removeItemAtPath:path error:NULL];
        return;
    }
    NSString *exe = NSBundle.mainBundle.executablePath;
    NSString *plist = [NSString stringWithFormat:
        @"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
         "<plist version=\"1.0\">\n"
         "<dict>\n"
         "  <key>Label</key><string>com.github.wlc114514.clawd-pet</string>\n"
         "  <key>ProgramArguments</key><array><string>%@</string></array>\n"
         "  <key>RunAtLoad</key><true/>\n"
         "</dict>\n"
         "</plist>\n", xml_escape(exe)];
    [fm createDirectoryAtPath:[path stringByDeletingLastPathComponent]
  withIntermediateDirectories:YES
                   attributes:nil
                        error:NULL];
    [plist writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL];
}

/* ---- `clawd` command -------------------------------------------------- */

// A `clawd` shell script in its own folder, with that folder on the PATH of
// new shells, so nothing else next to the app becomes a command.
static NSString *cli_dir(void)
{
    return [mac_support_dir() stringByAppendingPathComponent:@"bin"];
}

static NSString *cli_script(void)
{
    return [cli_dir() stringByAppendingPathComponent:@"clawd"];
}

static NSArray<NSString *> *shell_rcs(void)
{
    NSString *home = NSHomeDirectory();
    NSMutableArray *out = [NSMutableArray arrayWithObject:[home stringByAppendingPathComponent:@".zshrc"]];
    NSString *bash = [home stringByAppendingPathComponent:@".bash_profile"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:bash]) [out addObject:bash];
    return out;
}

static NSString *const CLI_MARK_BEGIN = @"# >>> Clawd >>>";
static NSString *const CLI_MARK_END = @"# <<< Clawd <<<";

// A shell rc's text: @"" if there is no such file, nil if it exists but
// can't be read as UTF-8 (then it must be left alone, not overwritten).
static NSString *read_rc(NSString *rc)
{
    if (![[NSFileManager defaultManager] fileExistsAtPath:rc]) return @"";
    return [NSString stringWithContentsOfFile:rc encoding:NSUTF8StringEncoding error:NULL];
}

// Writes through a symlinked rc (dotfile managers) instead of replacing the link.
static void write_rc(NSString *rc, NSString *text)
{
    [text writeToFile:[rc stringByResolvingSymlinksInPath] atomically:YES encoding:NSUTF8StringEncoding error:NULL];
}

static BOOL cli_enabled(void)
{
    if (![[NSFileManager defaultManager] fileExistsAtPath:cli_script()]) return NO;
    for (NSString *rc in shell_rcs()) {
        NSString *s = read_rc(rc);
        if (s && [s containsString:CLI_MARK_BEGIN]) return YES;
    }
    return NO;
}

static void cli_set(BOOL on)
{
    NSFileManager *fm = [NSFileManager defaultManager];
    NSString *dir = cli_dir(), *script = cli_script();

    if (on) {
        [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:NULL];
        NSString *text = [NSString stringWithFormat:
            @"#!/bin/sh\n"
             "# Made by Clawd (menu: clawd command). Launches the desktop pet.\n"
             "exec \"%@\" \"$@\"\n", NSBundle.mainBundle.executablePath];
        [text writeToFile:script atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        [fm setAttributes:@{ NSFilePosixPermissions: @0755 } ofItemAtPath:script error:NULL];

        NSString *block = [NSString stringWithFormat:@"\n%@\nexport PATH=\"%@:$PATH\"\n%@\n",
                           CLI_MARK_BEGIN, dir, CLI_MARK_END];
        for (NSString *rc in shell_rcs()) {
            NSString *s = read_rc(rc);
            if (!s || [s containsString:CLI_MARK_BEGIN]) continue;
            write_rc(rc, [s stringByAppendingString:block]);
        }
        return;
    }

    [fm removeItemAtPath:script error:NULL];
    [fm removeItemAtPath:dir error:NULL];
    for (NSString *rc in shell_rcs()) {
        NSString *s = read_rc(rc);
        if (!s || ![s containsString:CLI_MARK_BEGIN]) continue;
        // Drop the block and the blank line put in front of it, keep the rest as it was.
        NSMutableArray<NSString *> *keep = [NSMutableArray array];
        BOOL skip = NO;
        for (NSString *line in [s componentsSeparatedByString:@"\n"]) {
            if ([line isEqualToString:CLI_MARK_BEGIN]) {
                if ([keep.lastObject isEqualToString:@""]) [keep removeLastObject];
                skip = YES;
                continue;
            }
            if ([line isEqualToString:CLI_MARK_END]) { skip = NO; continue; }
            if (!skip) [keep addObject:line];
        }
        write_rc(rc, [keep componentsJoinedByString:@"\n"]);
    }
}

/* ---- Claude Code events over a unix socket ---------------------------- */

static NSString *cc_socket_path(void)
{
    return [mac_support_dir() stringByAppendingPathComponent:@"cc.sock"];
}

// Binds the hook socket. An instance that was force quit leaves the file
// behind, and bind then fails with EADDRINUSE, so drop it and try again.
static int cc_listen(void)
{
    NSString *path = cc_socket_path();
    struct sockaddr_un a = { 0 };
    a.sun_family = AF_UNIX;
    strlcpy(a.sun_path, path.fileSystemRepresentation, sizeof a.sun_path);

    for (int attempt = 0; attempt < 2; attempt++) {
        int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (fd < 0) return -1;
        unlink(path.fileSystemRepresentation);
        if (bind(fd, (struct sockaddr *)&a, sizeof a) == 0) return fd;
        int err = errno;
        close(fd);
        if (err != EADDRINUSE) return -1;
    }
    return -1;
}

// `clawd --cc <event>`: one datagram to the running pet, and out. If no pet
// is running the send just fails and nothing happens.
static int send_cc_event(const char *name)
{
    int ev = 0;
    for (int i = 0; i < (int)(sizeof CC_EVENTS / sizeof CC_EVENTS[0]); i++)
        if (!strcmp(name, CC_EVENTS[i])) ev = i + 1;
    if (!ev) return 0;

    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_un a = { 0 };
    a.sun_family = AF_UNIX;
    strlcpy(a.sun_path, cc_socket_path().fileSystemRepresentation, sizeof a.sun_path);
    unsigned char b = (unsigned char)ev;
    sendto(fd, &b, 1, 0, (struct sockaddr *)&a, sizeof a);
    close(fd);
    return 0;
}

/* ---- single instance -------------------------------------------------- */

static int g_lock_fd = -1;

static BOOL single_instance(void)
{
    NSString *path = [mac_support_dir() stringByAppendingPathComponent:@"lock"];
    g_lock_fd = open(path.fileSystemRepresentation, O_CREAT | O_RDWR, 0600);
    if (g_lock_fd < 0) return YES;   // no lock file: better to run than not to
    return flock(g_lock_fd, LOCK_EX | LOCK_NB) == 0;
}

/* ---- sensing ---------------------------------------------------------- */

// Keys worth counting as "typing". Polled, so no event tap and nothing about
// which key it was is kept beyond this table.
static const CGKeyCode TYPING_KEYS[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17,        // letters
    31, 32, 34, 35, 37, 38, 40, 45, 46,
    18, 19, 20, 21, 22, 23, 25, 26, 28, 29,                          // digits
    24, 27, 30, 33, 39, 41, 42, 43, 44, 47, 50,                      // punctuation
    36, 48, 49, 51,                                                  // return, tab, space, delete
};

static void sample_input(void)
{
    ULONGLONG now = plat_ticks_ms();
    POINT c;
    plat_cursor(&c);
    if (c.x != g_last_cursor.x || c.y != g_last_cursor.y) {
        g_last_cursor = c;
        g_cursor_moved_at = now;
    }
    g_world.cursor = c;
    g_world.cursor_still_ms = (DWORD)(now - g_cursor_moved_at);

    int keys = 0;
    if (g_typing_on)
        for (size_t i = 0; i < sizeof TYPING_KEYS / sizeof TYPING_KEYS[0]; i++) {
            CGKeyCode k = TYPING_KEYS[i];
            int down = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, k) != 0;
            if (down && !g_key_down[k]) keys++;
            g_key_down[k] = (unsigned char)down;
        }
    if (keys) g_last_key_at = now;
    g_world.keys = keys;
    g_world.typing = g_typing_on && g_last_key_at && now - g_last_key_at < 1500;
    g_world.mouse_mode = g_mouse_mode;
    g_world.cc = g_cc;
    g_world.climb = g_feat_climb;
    g_world.focus = g_focus;
}

static double sample_cpu(void)
{
    static host_cpu_load_info_data_t prev;
    host_cpu_load_info_data_t cur;
    mach_msg_type_number_t n = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&cur, &n) != KERN_SUCCESS)
        return 0;

    uint64_t busy = (uint64_t)cur.cpu_ticks[CPU_STATE_USER] + cur.cpu_ticks[CPU_STATE_SYSTEM] +
                    cur.cpu_ticks[CPU_STATE_NICE];
    uint64_t total = busy + cur.cpu_ticks[CPU_STATE_IDLE];
    uint64_t pbusy = (uint64_t)prev.cpu_ticks[CPU_STATE_USER] + prev.cpu_ticks[CPU_STATE_SYSTEM] +
                     prev.cpu_ticks[CPU_STATE_NICE];
    uint64_t ptotal = pbusy + prev.cpu_ticks[CPU_STATE_IDLE];
    int first = prev.cpu_ticks[CPU_STATE_IDLE] == 0;
    prev = cur;
    if (first || total <= ptotal) return 0;

    double v = (double)(busy - pbusy) / (double)(total - ptotal);
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

// How hot Clawd feels (World.heat). macOS keeps CPU die temperatures behind
// the SMC, but it does report thermal pressure, which is the same idea and
// needs no privileges: nominal is fine, fair is warm, anything above is hot.
// Machines that sit at nominal while pinned go by CPU load instead.
static int sample_heat(void)
{
    switch (NSProcessInfo.processInfo.thermalState) {
    case NSProcessInfoThermalStateNominal: return g_world.cpu > 0.8 ? 1 : 0;
    case NSProcessInfoThermalStateFair:    return 1;
    default:                               return 2;
    }
}

// Displays covered by a fullscreen window (game, video, slides).
static void check_fullscreen(void)
{
    NSMutableSet *fs = [NSMutableSet set];
    if (g_hide_fullscreen) {
        CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly |
                                                     kCGWindowListExcludeDesktopElements,
                                                     kCGNullWindowID);
        pid_t me = getpid();
        for (NSDictionary *w in (__bridge_transfer NSArray *)list) {
            if ([w[(id)kCGWindowOwnerPID] intValue] == me) continue;
            if ([w[(id)kCGWindowLayer] intValue] != 0) continue;
            if ([w[(id)kCGWindowAlpha] doubleValue] <= 0.01) continue;
            CGRect b;
            if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)w[(id)kCGWindowBounds], &b))
                continue;
            for (NSScreen *s in [NSScreen screens]) {
                CGDirectDisplayID d = mac_display_of_screen(s);
                CGRect f = CGDisplayBounds(d);
                if (b.origin.x <= f.origin.x + 2 && b.origin.y <= f.origin.y + 2 &&
                    CGRectGetMaxX(b) >= CGRectGetMaxX(f) - 2 && CGRectGetMaxY(b) >= CGRectGetMaxY(f) - 2)
                    [fs addObject:@(d)];
            }
        }
    }
    g_fs = fs;
    walls_hide_on(fs);
}

/* ---- menu bar icon ---------------------------------------------------- */

// The sprite's visible part is 16x10 (x 1..16), so it fits a 16pt icon
// exactly; a 2x copy keeps it sharp on Retina.
static NSImage *status_icon(void)
{
    uint32_t lp[16 * 10];
    Canvas c = { lp, 16, 10 };
    Pose q = pose_stand();
    cv_clear(&c);
    draw_clawd(&c, -1, 0, &q);

    NSImage *img = [[NSImage alloc] initWithSize:NSMakeSize(18, 18)];
    for (int k = 1; k <= 2; k++) {
        int size = 18 * k;
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                        pixelsWide:size
                                                                        pixelsHigh:size
                                                                     bitsPerSample:8
                                                                   samplesPerPixel:4
                                                                          hasAlpha:YES
                                                                          isPlanar:NO
                                                                    colorSpaceName:NSDeviceRGBColorSpace
                                                                       bytesPerRow:size * 4
                                                                      bitsPerPixel:32];
        rep.size = NSMakeSize(18, 18);   // 1x and 2x copies of the same icon
        uint8_t *d = rep.bitmapData;
        memset(d, 0, (size_t)size * size * 4);
        int ox = (size - 16 * k) / 2, oy = (size - 10 * k) / 2;
        for (int y = 0; y < 10 * k; y++)
            for (int x = 0; x < 16 * k; x++) {
                uint32_t s = lp[(y / k) * 16 + x / k];
                uint8_t *o = d + ((size_t)(oy + y) * size + ox + x) * 4;
                o[0] = (s >> 16) & 255;
                o[1] = (s >> 8) & 255;
                o[2] = s & 255;
                o[3] = s >> 24;
            }
        [img addRepresentation:rep];
    }
    return img;
}

/* ---- dev: pose sheet -------------------------------------------------- */

static int write_sheet(const char *path)
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

    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    uint32_t file_size = 14 + 40 + (uint32_t)W * H * 4, off = 54, info_size = 40;
    int32_t bw = W, bh = -H;
    uint16_t planes = 1, bpp = 32;
    unsigned char fh[14] = { 'B', 'M' }, ih[40] = { 0 };
    memcpy(fh + 2, &file_size, 4);
    memcpy(fh + 10, &off, 4);
    memcpy(ih, &info_size, 4);
    memcpy(ih + 4, &bw, 4);
    memcpy(ih + 8, &bh, 4);
    memcpy(ih + 12, &planes, 2);
    memcpy(ih + 14, &bpp, 2);
    fwrite(fh, 1, sizeof fh, f);
    fwrite(ih, 1, sizeof ih, f);
    fwrite(img, 4, (size_t)W * H, f);
    fclose(f);
    free(img);
    free(cell);
    return 0;
}

/* ---- the app ---------------------------------------------------------- */

@class ClawdApp;

@interface ClawdApp : NSObject <NSApplicationDelegate, NSMenuDelegate> {
    PetWindow *_menuPet;   // the pet the open menu belongs to, nil for the tray
}
@end

@implementation ClawdApp

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    (void)note;
    mac_screens_changed();
    g_pets = [NSMutableArray array];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(screensChanged:)
                                                 name:NSApplicationDidChangeScreenParametersNotification
                                               object:nil];

    // settings
    g_size = clampi(ud_int(@"Size", 1), 0, 2, 1);
    g_mouse_mode = clampi(ud_int(@"MouseMode", MOUSE_OFF), 0, 2, MOUSE_OFF);
    g_typing_on = ud_int(@"Typing", 1) != 0;
    g_hide_fullscreen = ud_int(@"HideFullscreen", 1) != 0;
    g_water = clampi(ud_int(@"Water", 0), 0, 3, 0);
    g_water_next = plat_ticks_ms() + (ULONGLONG)WATER_MIN[g_water] * 60000;
    g_feat_pomo = ud_int(@"FeatPomo", 1) != 0;
    g_feat_water = ud_int(@"FeatWater", 1) != 0;
    g_feat_climb = ud_int(@"FeatClimb", 1) != 0;
    g_feat_update = ud_int(@"FeatUpdate", 1) != 0;
    g_update_next = plat_ticks_ms() + 60000;
    g_stair_style = clampi(ud_int(@"StairStyle", STAIRS_SLAB), 0, STAIRS_STYLES - 1, STAIRS_SLAB);
    g_pomo_focus = ud_int(@"PomoFocus", 25);
    if (g_pomo_focus < FOCUS_LO || g_pomo_focus > FOCUS_HI || g_pomo_focus % FOCUS_STEP) g_pomo_focus = 25;
    g_pomo_break = clampi(ud_int(@"PomoBreak", 5), BREAK_LO, BREAK_HI, 5);
    int count = clampi(ud_int(@"Count", 1), 1, MAX_PETS, 1);

    // menu bar
    g_status = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    g_status.button.image = status_icon();
    g_status.button.imageScaling = NSImageScaleProportionallyDown;
    g_status.button.toolTip = @"Clawd";
    g_menu = [[NSMenu alloc] initWithTitle:@"Clawd"];
    g_menu.delegate = self;
    g_menu.autoenablesItems = NO;
    g_status.menu = g_menu;

    // Claude Code hook socket
    int fd = cc_listen();
    if (fd >= 0) {
        fcntl(fd, F_SETFL, O_NONBLOCK);
        g_cc_src = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t)fd, 0,
                                          dispatch_get_main_queue());
        dispatch_source_set_event_handler(g_cc_src, ^{
            unsigned char b;
            if (read(fd, &b, 1) == 1) [self onCCEvent:(int)b];
        });
        dispatch_resume(g_cc_src);
    }

    walls_load();
    for (int i = 0; i < count; i++) [self spawnPet:NO];
    check_fullscreen();

    NSTimer *t = [NSTimer timerWithTimeInterval:TICK_MS / 1000.0
                                         target:self
                                       selector:@selector(tick)
                                       userInfo:nil
                                        repeats:YES];
    t.tolerance = 0.004;
    [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
}

- (void)applicationWillTerminate:(NSNotification *)note
{
    (void)note;
    unlink(cc_socket_path().fileSystemRepresentation);
}

- (void)screensChanged:(NSNotification *)note
{
    (void)note;
    mac_screens_changed();
    for (PetWindow *pw in g_pets) [pw invalidate];
    walls_relayout();
    check_fullscreen();
}

/* ---- pets ------------------------------------------------------------- */

- (PetWindow *)spawnPet:(BOOL)dropIn
{
    if (g_pets.count >= MAX_PETS) return nil;
    Pet p;
    pet_init(&p, scale_for(g_size), (unsigned)plat_ticks_ms() ^ ((unsigned)g_pets.count * 0x9E3779B9u));
    if (dropIn) p.y = p.area.top - CH * p.scale;   // falls from the sky
    p.typist = g_pets.count == 0;   // only the first one types along
    PetWindow *pw = [[PetWindow alloc] initWithPet:p];
    [g_pets addObject:pw];
    return pw;
}

// Pets that are not on their way out.
- (int)activePets
{
    int n = 0;
    for (PetWindow *pw in g_pets) n += pw.pet->state != ST_LEAVE;
    return n;
}

// Send the newest pet away; it is destroyed once it has fallen off screen.
// g_pets[0] (the talker) always stays.
- (void)sendAwayPet
{
    if ([self activePets] <= 1) return;
    for (NSInteger i = (NSInteger)g_pets.count - 1; i > 0; i--)
        if (g_pets[i].pet->state != ST_LEAVE) {
            pet_leave(g_pets[i].pet);
            return;
        }
}

// The first Clawd does the talking; everyone cheers.
- (void)announce:(NSString *)text ms:(int)ms cheer:(BOOL)cheer
{
    if (g_pets.count) [g_pets[0].bubble show:text ms:ms];
    if (cheer)
        for (PetWindow *pw in g_pets) pet_cheer(pw.pet);
}

- (void)petClicked
{
    if (g_cc == CLAUDE_WAITING) {   // "ok, I saw it"
        g_cc = CLAUDE_IDLE;
        if (g_pets.count) [g_pets[0].bubble hide];
    }
}

/* ---- the tick --------------------------------------------------------- */

- (void)tick
{
    static unsigned n;
    static ULONGLONG last_sec;
    n++;
    sample_input();
    ULONGLONG now = plat_ticks_ms();
    if (now - last_sec >= 1000) {
        last_sec = now;
        [self everySecond];
    }
    if (n % 15 == 0) check_fullscreen();

    Pet *live[MAX_PETS];
    int nlive = 0;
    for (PetWindow *pw in [g_pets copy]) {
        Pet *p = pw.pet;
        NSScreen *s = mac_screen_at(p->x + CW * p->scale / 2, p->y + FOOT_Y * p->scale - 1);
        BOOL hide = g_fs.count && [g_fs containsObject:@(mac_display_of_screen(s))] &&
                    pet_window_pressed() != pw;
        [pw setHiddenByFullscreen:hide];
        if (hide) {
            if (p->state == ST_LEAVE) p->gone = 1;   // nobody to see the fall
            continue;
        }
        pet_update(p, &g_world);
        live[nlive++] = p;
    }
    // Sort out what they do together, then draw.
    pets_together(live, nlive, &g_world);
    int dressing = [Wardrobe pet], found = 0;   // close the wardrobe if its pet has gone
    for (int i = 0; i < nlive; i++) found |= live[i]->id == dressing && live[i]->state != ST_LEAVE;
    if (dressing && !found) [Wardrobe close];
    for (PetWindow *pw in g_pets) {
        Pet *p = pw.pet;
        if (pw.hiddenByFullscreen) continue;
        [pw present];
        [pw presentStairs];
        [pw presentCatch];
        [pw.bubble update:pet_head(p) area:p->area];
        if (n % 60 == 0) [pw orderFrontRegardless];   // the Dock likes to climb above us
    }

    // Everyone else jumps out of their skin when one drops through the Dock.
    for (PetWindow *pw in [g_pets copy]) {
        if (!pw.pet->dropped) continue;
        pw.pet->dropped = 0;
        for (PetWindow *other in g_pets)
            if (other != pw) pet_startle(other.pet, pw.pet->x);
    }

    for (NSInteger i = (NSInteger)g_pets.count - 1; i >= 0; i--)
        if (g_pets[i].pet->gone) {
            [g_pets[i].bubble suspend];
            [g_pets removeObjectAtIndex:i];
        }
}

- (void)everySecond
{
    ULONGLONG now = plat_ticks_ms();
    g_world.cpu = g_world.cpu * 0.5 + sample_cpu() * 0.5;
    g_world.heat = sample_heat();

    if (g_cc && now >= g_cc_until) g_cc = CLAUDE_IDLE;

    if (g_pomo && now >= g_pomo_end) {
        if (g_pomo == 1) {
            g_pomo = 2;
            g_pomo_end = now + (ULONGLONG)g_pomo_break * 60000;
            pomo_focus_over();
            [self announce:[NSString stringWithFormat:@"专注 %d 分钟完成！\n起来休息 %d 分钟吧",
                                                      g_pomo_focus, g_pomo_break]
                        ms:60000 cheer:YES];
        } else {
            g_pomo = 0;
            [self announce:@"休息结束，继续加油！" ms:15000 cheer:YES];
        }
    }

    // Only remind someone who is actually at the computer.
    if (g_feat_water && g_water && now >= g_water_next && plat_idle_ms() < 60000) {
        g_water_next = now + (ULONGLONG)WATER_MIN[g_water] * 60000;
        [self announce:@"该喝水啦！" ms:15000 cheer:YES];
    }

    if (now >= g_update_next) {   // a minute after starting, then once a day
        g_update_next = now + 24ULL * 3600 * 1000;
        if (g_feat_update) [self checkUpdate:NO];
    }

    [self updateTooltip];
}

- (void)checkUpdate:(BOOL)manual
{
    update_check(^(int result, NSString *tag) {
        switch (result) {
        case UPD_NEWER:
            g_update_tag = tag;
            if (manual || ![g_update_told isEqualToString:tag]) {   // once per version unless asked
                g_update_told = tag;
                [self announce:[NSString stringWithFormat:@"有新版本 %@ 啦！\n菜单里点「下载 %@」", tag, tag]
                            ms:10000 cheer:NO];
            }
            break;
        case UPD_LATEST:
            if (manual) [self announce:@"已经是最新版 v" CLAWD_VERSION " 啦" ms:4000 cheer:NO];
            break;
        default:
            if (manual) [self announce:@"检查更新失败了，网络好像不太通" ms:4000 cheer:NO];
            break;
        }
    });
}

- (void)updateTooltip
{
    NSString *tip = g_focus ? @"Clawd\n专注模式" : @"Clawd";
    if (g_pomo) {
        ULONGLONG now = plat_ticks_ms();
        int mins = now >= g_pomo_end ? 0 : (int)((g_pomo_end - now + 59999) / 60000);
        tip = [NSString stringWithFormat:@"Clawd\n%@ · 还剩 %d 分钟",
                                         g_pomo == 1 ? @"专注中" : @"休息中", mins];
    }
    g_status.button.toolTip = tip;
}

- (void)onCCEvent:(int)ev
{
    ULONGLONG now = plat_ticks_ms();
    switch (ev) {
    case CC_EV_PROMPT:
    case CC_EV_TOOL:
        if (g_cc == CLAUDE_WAITING && g_pets.count) [g_pets[0].bubble hide];
        g_cc = CLAUDE_WORKING;
        g_cc_until = now + CC_WORK_MS;
        break;
    case CC_EV_PERMISSION:
        g_cc = CLAUDE_WAITING;
        g_cc_until = now + CC_WAIT_MS;
        [self announce:@"Claude 在等你确认～" ms:8000 cheer:NO];
        break;
    case CC_EV_STOP:
        g_cc = CLAUDE_IDLE;
        [self announce:@"搞定啦！" ms:4000 cheer:YES];
        break;
    case CC_EV_END:
        g_cc = CLAUDE_IDLE;
        break;
    }
    g_world.cc = g_cc;
}

/* ---- menu ------------------------------------------------------------- */

static void add_item(NSMenu *m, NSString *title, int tag, BOOL on, BOOL enabled, id target)
{
    NSMenuItem *i = [[NSMenuItem alloc] initWithTitle:title
                                               action:enabled ? @selector(menuAction:) : nil
                                        keyEquivalent:@""];
    i.tag = tag;
    i.state = on ? NSControlStateValueOn : NSControlStateValueOff;
    i.enabled = enabled;
    i.target = target;
    [m addItem:i];
}

static void add_submenu(NSMenu *m, NSString *title, NSMenu *sub)
{
    NSMenuItem *i = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    i.submenu = sub;
    sub.autoenablesItems = NO;
    [m addItem:i];
}

- (void)fillMenu:(NSMenu *)m forPet:(PetWindow *)pw
{
    [m removeAllItems];
    m.autoenablesItems = NO;

    BOOL paused = pw ? pw.pet->paused : (g_pets.count && g_pets[0].pet->paused);
    BOOL can_climb = NO;
    for (PetWindow *p in g_pets)
        if (!pw || p == pw) can_climb = can_climb || pet_can_climb(p.pet);

    NSMenu *sz = [[NSMenu alloc] initWithTitle:@"大小"];
    add_item(sz, @"小", IDM_SIZE_S, g_size == 0, YES, self);
    add_item(sz, @"中", IDM_SIZE_M, g_size == 1, YES, self);
    add_item(sz, @"大", IDM_SIZE_L, g_size == 2, YES, self);

    NSMenu *mouse = [[NSMenu alloc] initWithTitle:@"鼠标互动"];
    add_item(mouse, @"不理鼠标", IDM_MOUSE_OFF, g_mouse_mode == MOUSE_OFF, YES, self);
    add_item(mouse, @"跟着鼠标走", IDM_MOUSE_FOLLOW, g_mouse_mode == MOUSE_FOLLOW, YES, self);
    add_item(mouse, @"怕鼠标", IDM_MOUSE_SHY, g_mouse_mode == MOUSE_SHY, YES, self);

    NSMenu *feat = [[NSMenu alloc] initWithTitle:@"功能开关"];
    add_item(feat, @"番茄钟", IDM_FEAT_POMO, g_feat_pomo, YES, self);
    add_item(feat, @"喝水提醒", IDM_FEAT_WATER, g_feat_water, YES, self);
    add_item(feat, @"爬楼梯", IDM_FEAT_CLIMB, g_feat_climb, YES, self);
    add_item(feat, @"自动检查更新", IDM_FEAT_UPDATE, g_feat_update, YES, self);

    // Settings live in one submenu; the top level is things to do right now.
    NSMenu *set = [[NSMenu alloc] initWithTitle:@"设置"];
    add_submenu(set, @"大小", sz);
    if (g_feat_climb) {
        NSMenu *st = [[NSMenu alloc] initWithTitle:@"楼梯样式"];
        add_item(st, @"悬空石板", IDM_STAIRS_SLAB, g_stair_style == STAIRS_SLAB, YES, self);
        add_item(st, @"实心石砖", IDM_STAIRS_SOLID, g_stair_style == STAIRS_SOLID, YES, self);
        add_item(st, @"只有轮廓线", IDM_STAIRS_OUTLINE, g_stair_style == STAIRS_OUTLINE, YES, self);
        add_submenu(set, @"楼梯样式", st);
    }
    add_submenu(set, @"鼠标互动", mouse);
    add_submenu(set, @"功能开关", feat);
    [set addItem:[NSMenuItem separatorItem]];
    add_item(set, @"打字时陪你敲", IDM_TYPING, g_typing_on, YES, self);
    add_item(set, @"全屏时躲起来", IDM_FULLSCREEN, g_hide_fullscreen, YES, self);
    add_item(set, @"开机自启", IDM_AUTOSTART, autostart_enabled(), YES, self);
    add_item(set, @"终端里可用 clawd 命令", IDM_CLI, cli_enabled(), YES, self);
    [set addItem:[NSMenuItem separatorItem]];
    add_item(set, @"检查更新（当前 v" CLAWD_VERSION "）", IDM_UPDATE_CHECK, NO, YES, self);

    // In focus mode they stay at their desks: no flips, slides, games or climbing.
    add_item(m, @"专注模式", IDM_FOCUS, g_focus, YES, self);
    add_item(m, @"待在原地", IDM_PAUSE, paused, YES, self);
    add_item(m, @"去睡觉", IDM_SLEEP, NO, YES, self);
    add_item(m, @"后空翻", IDM_FLIP, NO, !g_focus, self);
    add_item(m, @"滑一下", IDM_SLIDE, NO, !g_focus, self);
    add_item(m, @"换装…", IDM_WARDROBE, NO, g_pets.count > 0, self);
    NSMenu *play = [[NSMenu alloc] initWithTitle:@"一起玩"];
    add_item(play, @"打招呼聊天", IDM_PLAY_GREET, NO, YES, self);
    add_item(play, @"追着玩", IDM_PLAY_CHASE, NO, YES, self);
    add_item(play, @"排队走", IDM_PLAY_LINE, NO, YES, self);
    add_item(play, @"叠罗汉", IDM_PLAY_RIDE, NO, YES, self);
    add_item(play, @"晕倒求安慰", IDM_PLAY_DAZE, NO, YES, self);
    add_submenu(m, @"一起玩", play);
    m.itemArray.lastObject.enabled = [self activePets] >= 2 && !g_focus;
    if (g_feat_climb) add_item(m, @"爬上去", IDM_CLIMB, NO, can_climb && !g_focus, self);
    add_item(m, @"放置围墙", IDM_WALL_DRAW, NO, walls_count() < MAX_WALLS && !walls_drawing(), self);
    if (walls_count()) add_item(m, @"拆掉所有围墙", IDM_WALL_CLEAR, NO, YES, self);
    if (g_feat_pomo || g_feat_water) [m addItem:[NSMenuItem separatorItem]];

    if (g_feat_pomo && g_pomo) {
        NSMenu *pomo = [[NSMenu alloc] initWithTitle:@"番茄钟"];
        NSString *status = [NSString stringWithFormat:@"%@ · 还剩 %d 分钟",
                            g_pomo == 1 ? @"专注中" : @"休息中",
                            g_pomo_end > plat_ticks_ms()
                                ? (int)((g_pomo_end - plat_ticks_ms() + 59999) / 60000) : 0];
        add_item(pomo, status, 0, NO, NO, self);
        add_item(pomo, @"停止", IDM_POMO_STOP, NO, YES, self);
        add_submenu(m, @"番茄钟", pomo);
    } else if (g_feat_pomo) {
        add_item(m, @"番茄钟…", IDM_POMO_START, NO, YES, self);
    }

    if (g_feat_water) {
        NSMenu *water = [[NSMenu alloc] initWithTitle:@"喝水提醒"];
        add_item(water, @"关闭", IDM_WATER_OFF, g_water == 0, YES, self);
        add_item(water, @"每 30 分钟", IDM_WATER_30, g_water == 1, YES, self);
        add_item(water, @"每 45 分钟", IDM_WATER_45, g_water == 2, YES, self);
        add_item(water, @"每 60 分钟", IDM_WATER_60, g_water == 3, YES, self);
        add_submenu(m, @"喝水提醒", water);
    }

    [m addItem:[NSMenuItem separatorItem]];
    add_item(m, @"再来一只", IDM_ADD_PET, NO, g_pets.count < MAX_PETS, self);
    add_item(m, @"送走一只", IDM_REMOVE_PET, NO, [self activePets] > 1, self);
    if (g_update_tag)
        add_item(m, [NSString stringWithFormat:@"下载 %@", g_update_tag], IDM_UPDATE_OPEN, NO, YES, self);
    add_submenu(m, @"设置", set);
    [m addItem:[NSMenuItem separatorItem]];
    add_item(m, @"退出", IDM_EXIT, NO, YES, self);
}

- (void)menuNeedsUpdate:(NSMenu *)menu
{
    _menuPet = nil;
    [self fillMenu:menu forPet:nil];
}

- (void)showMenuForPet:(PetWindow *)pw event:(NSEvent *)event
{
    _menuPet = pw;
    NSMenu *m = [[NSMenu alloc] initWithTitle:@"Clawd"];
    m.autoenablesItems = NO;
    [self fillMenu:m forPet:pw];
    [NSMenu popUpContextMenu:m withEvent:event forView:pw.contentView];
    _menuPet = nil;
}

- (void)menuAction:(NSMenuItem *)it
{
    int cmd = (int)it.tag;
    ULONGLONG now = plat_ticks_ms();
    PetWindow *pw = _menuPet;

    switch (cmd) {
    case IDM_FOCUS:
        g_focus = !g_focus;
        g_focus_pomo = 0;   // the user's call now, whatever the pomodoro does
        break;
    case IDM_PAUSE: {
        // From a pet: just that one. From the tray: everyone.
        BOOL paused = pw ? pw.pet->paused : (g_pets.count && g_pets[0].pet->paused);
        for (PetWindow *p in g_pets)
            if (!pw || p == pw) p.pet->paused = !paused;
        break;
    }
    case IDM_SLEEP:
        for (PetWindow *p in g_pets)
            if (!pw || p == pw) pet_sleep(p.pet);
        break;
    case IDM_FLIP:
        for (PetWindow *p in g_pets)
            if (!pw || p == pw) pet_flip(p.pet);
        break;
    case IDM_SLIDE:
        for (PetWindow *p in g_pets)
            if (!pw || p == pw) pet_slide(p.pet);
        break;
    case IDM_CLIMB:
        for (PetWindow *p in g_pets)
            if (!pw || p == pw) pet_climb(p.pet);
        break;
    case IDM_WALL_DRAW:
        [self announce:@"按住鼠标拖一下，画一面墙\n横着拖是横墙，竖着拖是竖墙\n右键或 Esc 取消" ms:30000 cheer:NO];
        walls_draw(scale_for(g_size), ^{
            if (g_pets.count) [g_pets[0].bubble hide];
        });
        break;
    case IDM_WALL_CLEAR:
        walls_clear();
        break;
    case IDM_STAIRS_SLAB:
    case IDM_STAIRS_SOLID:
    case IDM_STAIRS_OUTLINE:
        g_stair_style = cmd - IDM_STAIRS_SLAB;
        ud_set(@"StairStyle", g_stair_style);
        break;
    case IDM_FEAT_CLIMB:
        g_feat_climb = !g_feat_climb;
        ud_set(@"FeatClimb", g_feat_climb);
        break;
    case IDM_MOUSE_OFF:
    case IDM_MOUSE_FOLLOW:
    case IDM_MOUSE_SHY:
        g_mouse_mode = cmd - IDM_MOUSE_OFF;
        ud_set(@"MouseMode", g_mouse_mode);
        break;
    case IDM_TYPING:
        g_typing_on = !g_typing_on;
        ud_set(@"Typing", g_typing_on);
        break;
    case IDM_PLAY_GREET:
    case IDM_PLAY_CHASE:
    case IDM_PLAY_LINE:
    case IDM_PLAY_RIDE:
    case IDM_PLAY_DAZE: {   // from a pet: that one starts it
        Pet *all[MAX_PETS];
        int n = 0;
        for (PetWindow *p in g_pets)
            if (!p.hiddenByFullscreen && p.pet->state != ST_LEAVE && n < MAX_PETS) all[n++] = p.pet;
        if (!pets_play(all, n, pw ? pw.pet : NULL, cmd - IDM_PLAY_GREET))
            [self announce:@"它们现在都忙着呢，待会儿再试试～" ms:3000 cheer:NO];
        break;
    }
    case IDM_WARDROBE: {   // from the menu bar: the first one
        if (!g_pets.count) break;
        const Pet *p = pw ? pw.pet : g_pets[0].pet;
        [Wardrobe openAt:pet_head(p) area:p->area pet:p->id wear:p->wear
                 changed:^(int petId, const unsigned char *wear) {
            for (PetWindow *o in g_pets)
                if (o.pet->id == petId) pet_dress(o.pet, wear);
        }];
        break;
    }
    case IDM_POMO_START: {
        if (!g_pets.count) break;
        const Pet *p = pw ? pw.pet : g_pets[0].pet;
        [Picker openAt:pet_head(p) area:p->area focus:g_pomo_focus break:g_pomo_break
                  done:^(int focus, int brk) {
            g_pomo_focus = focus;
            g_pomo_break = brk;
            ud_set(@"PomoFocus", focus);
            ud_set(@"PomoBreak", brk);
            g_pomo = 1;
            g_pomo_end = plat_ticks_ms() + (ULONGLONG)focus * 60000;
            if (!g_focus) g_focus = g_focus_pomo = 1;
            [self announce:[NSString stringWithFormat:@"开始专注 %d 分钟！", focus] ms:4000 cheer:NO];
            [self updateTooltip];
        }];
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
        ud_set(@"FeatPomo", g_feat_pomo);
        break;
    case IDM_FEAT_WATER:
        g_feat_water = !g_feat_water;
        g_water_next = now + (ULONGLONG)WATER_MIN[g_water] * 60000;
        ud_set(@"FeatWater", g_feat_water);
        break;
    case IDM_WATER_OFF:
    case IDM_WATER_30:
    case IDM_WATER_45:
    case IDM_WATER_60:
        g_water = cmd - IDM_WATER_OFF;
        g_water_next = now + (ULONGLONG)WATER_MIN[g_water] * 60000;
        ud_set(@"Water", g_water);
        break;
    case IDM_ADD_PET:
        [self spawnPet:YES];
        ud_set(@"Count", [self activePets]);
        break;
    case IDM_REMOVE_PET:
        [self sendAwayPet];
        ud_set(@"Count", [self activePets]);
        break;
    case IDM_SIZE_S:
    case IDM_SIZE_M:
    case IDM_SIZE_L:
        g_size = cmd - IDM_SIZE_S;
        ud_set(@"Size", g_size);
        for (PetWindow *p in g_pets) pet_set_scale(p.pet, scale_for(g_size));
        break;
    case IDM_FULLSCREEN:
        g_hide_fullscreen = !g_hide_fullscreen;
        ud_set(@"HideFullscreen", g_hide_fullscreen);
        check_fullscreen();
        break;
    case IDM_CLI:
        if (cli_enabled()) {
            cli_set(NO);
        } else {
            cli_set(YES);
            if (cli_enabled()) [self announce:@"新开一个终端，\n输入 clawd 就能叫我出来" ms:6000 cheer:NO];
        }
        break;
    case IDM_FEAT_UPDATE:
        g_feat_update = !g_feat_update;
        ud_set(@"FeatUpdate", g_feat_update);
        break;
    case IDM_UPDATE_CHECK:
        [self checkUpdate:YES];
        break;
    case IDM_UPDATE_OPEN:
        if (g_update_tag) [NSWorkspace.sharedWorkspace openURL:update_page(g_update_tag)];
        break;
    case IDM_AUTOSTART:
        autostart_set(!autostart_enabled());
        break;
    case IDM_EXIT:
        [NSApp terminate:nil];
        break;
    }
    [self updateTooltip];
}

@end

/* ---- entry ------------------------------------------------------------ */

void app_pet_clicked(void)
{
    [(ClawdApp *)NSApp.delegate petClicked];
}

void app_show_menu(PetWindow *pw, NSEvent *event)
{
    [(ClawdApp *)NSApp.delegate showMenuForPet:pw event:event];
}

static ClawdApp *g_app;   // NSApplication.delegate is weak, so hold it here

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc >= 3 && !strcmp(argv[1], "--sheet")) return write_sheet(argv[2]);
        if (argc >= 3 && !strcmp(argv[1], "--cc")) return send_cc_event(argv[2]);

        if (!single_instance()) return 0;

        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];   // no Dock icon
        g_app = [[ClawdApp alloc] init];
        app.delegate = g_app;
        [app run];
    }
    return 0;
}
