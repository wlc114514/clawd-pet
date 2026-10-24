/*
 * macOS side of the platform layer in src/platform.h, plus the screen and
 * image helpers the rest of the Cocoa code shares.
 */
#import "mac.h"

#include <math.h>
#include <string.h>
#include <mach/mach_time.h>

static double g_top;    // Cocoa y of the top edge of the union of the screens
static double g_left;   // Cocoa x of the left edge of that union

void mac_screens_changed(void)
{
    double top = -1e12, left = 1e12;
    for (NSScreen *s in [NSScreen screens]) {
        NSRect f = s.frame;
        if (NSMaxY(f) > top) top = NSMaxY(f);
        if (NSMinX(f) < left) left = NSMinX(f);
    }
    if (top < -1e11) {   // no displays at all; keep something sane
        top = 0;
        left = 0;
    }
    g_top = top;
    g_left = left;
}

NSRect mac_cocoa_rect(double x, double y, double w, double h)
{
    return NSMakeRect(g_left + x, g_top - y - h, w, h);
}

RECT mac_rect_from_cocoa(NSRect r)
{
    RECT out;
    out.left = (LONG)lround(r.origin.x - g_left);
    out.top = (LONG)lround(g_top - NSMaxY(r));
    out.right = (LONG)lround(NSMaxX(r) - g_left);
    out.bottom = (LONG)lround(g_top - r.origin.y);
    return out;
}

NSScreen *mac_screen_at(double x, double y)
{
    NSPoint p = NSMakePoint(g_left + x, g_top - y);
    for (NSScreen *s in [NSScreen screens])
        if (NSPointInRect(p, s.frame)) return s;

    // Off every screen (a pet falling past the Dock, say): take the nearest.
    NSScreen *best = [NSScreen mainScreen];
    double bd = 1e18;
    for (NSScreen *s in [NSScreen screens]) {
        double dx = p.x - NSMidX(s.frame), dy = p.y - NSMidY(s.frame);
        double d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = s; }
    }
    return best;
}

CGDirectDisplayID mac_display_of_screen(NSScreen *s)
{
    NSNumber *n = s.deviceDescription[@"NSScreenNumber"];
    return n ? (CGDirectDisplayID)n.unsignedIntValue : 0;
}

/* ---- platform layer ---------------------------------------------------- */

DWORD plat_idle_ms(void)
{
    CFTimeInterval secs = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateCombinedSessionState,
                                                                kCGAnyInputEventType);
    return (DWORD)(secs * 1000.0);
}

ULONGLONG plat_ticks_ms(void)
{
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return (ULONGLONG)((double)mach_absolute_time() * (double)tb.numer / (double)tb.denom / 1e6);
}

void plat_cursor(POINT *out)
{
    NSPoint p = [NSEvent mouseLocation];   // Cocoa screen coordinates
    out->x = (LONG)lround(p.x - g_left);
    out->y = (LONG)lround(g_top - p.y);
}

void plat_screen_rects(POINT pt, RECT *work, RECT *frame)
{
    NSScreen *s = mac_screen_at(pt.x, pt.y);
    if (!s) return;
    *work = mac_rect_from_cocoa(s.visibleFrame);   // Dock and menu bar excluded
    *frame = mac_rect_from_cocoa(s.frame);
}

void plat_work_area(RECT *work, RECT *frame)
{
    NSScreen *s = [NSScreen mainScreen];
    *work = mac_rect_from_cocoa(s.visibleFrame);
    *frame = mac_rect_from_cocoa(s.frame);
}

/* ---- images ------------------------------------------------------------ */

static void free_pixels(void *info, const void *data, size_t size)
{
    (void)info; (void)size;
    free((void *)data);
}

// The canvas is premultiplied ARGB in a uint32 per pixel, which on a
// little-endian machine is B, G, R, A in memory: exactly what Core Graphics
// calls "premultiplied first, 32-bit little-endian".
CGImageRef mac_cgimage_take(uint32_t *px, int w, int h)
{
    size_t len = (size_t)w * h * 4;
    CGDataProviderRef prov = CGDataProviderCreateWithData(NULL, px, len, free_pixels);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGImageRef img = CGImageCreate(w, h, 8, 32, (size_t)w * 4, cs,
                                   kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst,
                                   prov, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(cs);
    CGDataProviderRelease(prov);
    return img;
}

NSImage *mac_nsimage(const uint32_t *px, int w, int h)
{
    uint32_t *copy = malloc((size_t)w * h * 4);
    memcpy(copy, px, (size_t)w * h * 4);
    CGImageRef img = mac_cgimage_take(copy, w, h);
    NSImage *out = [[NSImage alloc] initWithCGImage:img size:NSMakeSize(w, h)];
    CGImageRelease(img);
    return out;
}

NSColor *mac_color(uint32_t rgb)
{
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 255) / 255.0
                               green:((rgb >> 8) & 255) / 255.0
                                blue:(rgb & 255) / 255.0
                               alpha:1.0];
}

/* ---- paths ------------------------------------------------------------- */

NSString *mac_support_dir(void)
{
    static NSString *dir;
    if (!dir) {
        NSString *base = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,
                                                             NSUserDomainMask, YES)[0];
        dir = [base stringByAppendingPathComponent:@"ClawdPet"];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:NULL];
    }
    return dir;
}
