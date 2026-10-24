/*
 * Brick walls the user draws on the screen (menu: 放置围墙). Each wall is a
 * borderless panel of its own that stays until it is right-clicked away;
 * they are saved in the user defaults and handed to the pets with
 * pet_set_walls().
 */
#import "mac.h"
#import <QuartzCore/QuartzCore.h>

#include <stdlib.h>

@interface Wall : NSObject
@property(nonatomic) RECT r;   // top-left screen coordinates, points
@property(nonatomic) int k;    // logical pixel size it was drawn at
@property(nonatomic, strong) NSPanel *win;
@end

@implementation Wall
@end

static NSMutableArray<Wall *> *g_walls;
static NSSet<NSNumber *> *g_hidden_on;   // displays behind a fullscreen window

static void publish(void);
static void wall_menu(NSWindow *win, NSEvent *event);

@interface WallView : NSView
@end

@implementation WallView
- (BOOL)isOpaque { return NO; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { (void)event; return YES; }
- (void)rightMouseDown:(NSEvent *)event { wall_menu(self.window, event); }
@end

static NSPanel *make_panel(BOOL click_through, NSInteger level)
{
    NSPanel *w = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 1, 1)
                                            styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
    w.opaque = NO;
    w.backgroundColor = [NSColor clearColor];
    w.hasShadow = NO;
    w.level = level;
    w.ignoresMouseEvents = click_through;
    w.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                           NSWindowCollectionBehaviorStationary |
                           NSWindowCollectionBehaviorIgnoresCycle;
    w.animationBehavior = NSWindowAnimationBehaviorNone;
    w.releasedWhenClosed = NO;
    w.hidesOnDeactivate = NO;
    WallView *v = [[WallView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
    v.wantsLayer = YES;
    v.layer.opaque = NO;
    v.layer.contentsGravity = kCAGravityResize;
    v.layer.magnificationFilter = kCAFilterNearest;
    v.layer.minificationFilter = kCAFilterNearest;
    w.contentView = v;
    return w;
}

static void paint(NSPanel *win, RECT r, int k)
{
    int lw = (int)(r.right - r.left) / k, lh = (int)(r.bottom - r.top) / k;
    uint32_t *lp = malloc((size_t)lw * lh * 4);
    Canvas c = { lp, lw, lh };
    draw_wall(&c);
    uint32_t *scaled = malloc((size_t)lw * lh * k * k * 4);
    cv_scale_premul(&c, scaled, k);
    free(lp);
    CGImageRef img = mac_cgimage_take(scaled, lw * k, lh * k);

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    win.contentView.layer.contents = (__bridge id)img;
    [win setFrame:mac_cocoa_rect(r.left, r.top, r.right - r.left, r.bottom - r.top) display:NO];
    [CATransaction commit];
    CGImageRelease(img);
}

static void show_wall(Wall *w)
{
    RECT r = w.r;
    NSScreen *s = mac_screen_at((r.left + r.right) / 2.0, (r.top + r.bottom) / 2.0);
    if ([g_hidden_on containsObject:@(mac_display_of_screen(s))]) [w.win orderOut:nil];
    else [w.win orderFrontRegardless];
}

static BOOL valid(RECT r, int k)
{
    long w = r.right - r.left, h = r.bottom - r.top, len = w > h ? w : h, t = w > h ? h : w;
    if (k < 1 || k > 32 || w % k || h % k || t != WALL_T * k || len < WALL_MIN * k || len > WALL_MAX * k)
        return NO;
    NSPoint mid = mac_cocoa_rect((r.left + r.right) / 2.0, (r.top + r.bottom) / 2.0, 0, 0).origin;
    for (NSScreen *s in [NSScreen screens])
        if (NSPointInRect(mid, s.frame)) return YES;
    return NO;   // its screen is gone
}

static void add(RECT r, int k)
{
    if (g_walls.count >= MAX_WALLS || !valid(r, k)) return;
    Wall *w = [[Wall alloc] init];
    w.r = r;
    w.k = k;
    w.win = make_panel(NO, WALL_LEVEL);
    [g_walls addObject:w];
    paint(w.win, r, k);
    show_wall(w);
}

static void publish(void)
{
    RECT r[MAX_WALLS];
    NSMutableString *text = [NSMutableString string];
    int n = 0;
    for (Wall *w in g_walls) {
        RECT q = w.r;
        r[n++] = q;
        [text appendFormat:@"%ld,%ld,%ld,%ld,%d;", (long)q.left, (long)q.top, (long)q.right, (long)q.bottom, w.k];
    }
    pet_set_walls(r, n);
    [[NSUserDefaults standardUserDefaults] setObject:text forKey:@"Walls"];
}

/* ---- right-click menu -------------------------------------------------- */

@interface WallMenu : NSObject
@property(nonatomic, weak) NSWindow *win;
@end

@implementation WallMenu
- (void)removeOne:(id)sender
{
    (void)sender;
    for (Wall *w in [g_walls copy])
        if (w.win == _win) {
            [w.win orderOut:nil];
            [g_walls removeObject:w];
        }
    publish();
}

- (void)removeAll:(id)sender
{
    (void)sender;
    walls_clear();
}
@end

static void wall_menu(NSWindow *win, NSEvent *event)
{
    WallMenu *target = [[WallMenu alloc] init];
    target.win = win;
    NSMenu *m = [[NSMenu alloc] initWithTitle:@"围墙"];
    m.autoenablesItems = NO;
    NSMenuItem *one = [m addItemWithTitle:@"拆掉这面墙" action:@selector(removeOne:) keyEquivalent:@""];
    NSMenuItem *all = [m addItemWithTitle:@"拆掉所有围墙" action:@selector(removeAll:) keyEquivalent:@""];
    one.target = target;
    all.target = target;
    [NSMenu popUpContextMenu:m withEvent:event forView:win.contentView];   // returns once it closes
}

/* ---- drawing ----------------------------------------------------------- */

static NSMutableArray<NSPanel *> *g_canvases;   // one per screen, all but invisible
static NSPanel *g_preview;
static RECT g_preview_r;
static int g_draw_k;
static BOOL g_down;
static POINT g_from;
static void (^g_done)(void);
static id g_keymon, g_resign;

// The wall a drag from a to b makes: along whichever way it moved more,
// starting at a and centred on the line of the drag.
static BOOL span(POINT a, POINT b, int k, RECT *r)
{
    long dx = b.x - a.x, dy = b.y - a.y;
    BOOL horiz = labs(dx) >= labs(dy);
    long len = (horiz ? labs(dx) : labs(dy)) / k, t = WALL_T * k;
    if (len < WALL_MIN) return NO;
    if (len > WALL_MAX) len = WALL_MAX;
    len *= k;
    if (horiz) {
        r->left = (LONG)(dx >= 0 ? a.x : a.x - len);
        r->top = (LONG)(a.y - t / 2);
        r->right = (LONG)(r->left + len);
        r->bottom = (LONG)(r->top + t);
    } else {
        r->top = (LONG)(dy >= 0 ? a.y : a.y - len);
        r->left = (LONG)(a.x - t / 2);
        r->right = (LONG)(r->left + t);
        r->bottom = (LONG)(r->top + len);
    }
    return YES;
}

static void finish(void)
{
    if (!g_canvases) return;
    for (NSPanel *c in g_canvases) [c orderOut:nil];
    g_canvases = nil;
    [g_preview orderOut:nil];
    g_preview = nil;
    g_down = NO;
    if (g_keymon) [NSEvent removeMonitor:g_keymon];
    if (g_resign) [[NSNotificationCenter defaultCenter] removeObserver:g_resign];
    g_keymon = g_resign = nil;
    [NSCursor pop];
    void (^done)(void) = g_done;
    g_done = nil;
    if (done) done();
}

@interface CanvasPanel : NSPanel
@end

@implementation CanvasPanel
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
- (NSRect)constrainFrameRect:(NSRect)frameRect toScreen:(NSScreen *)screen
{
    (void)screen;
    return frameRect;   // cover the menu bar and the Dock too
}
@end

@interface CanvasView : NSView
@end

@implementation CanvasView
- (BOOL)acceptsFirstMouse:(NSEvent *)event { (void)event; return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)resetCursorRects
{
    [self addCursorRect:self.bounds cursor:[NSCursor crosshairCursor]];
}

- (void)mouseDown:(NSEvent *)event
{
    (void)event;
    g_down = YES;
    plat_cursor(&g_from);
}

- (void)mouseDragged:(NSEvent *)event
{
    (void)event;
    if (!g_down) return;
    POINT c;
    RECT r;
    plat_cursor(&c);
    if (!span(g_from, c, g_draw_k, &r)) {
        [g_preview orderOut:nil];
        return;
    }
    if (!g_preview) {
        g_preview = make_panel(YES, POP_LEVEL + 2);
        g_preview.alphaValue = 0.67;   // see-through until it is placed
        g_preview_r = (RECT){ 0, 0, 0, 0 };
    }
    if (memcmp(&r, &g_preview_r, sizeof r)) {
        g_preview_r = r;
        paint(g_preview, r, g_draw_k);
    }
    [g_preview orderFrontRegardless];
}

- (void)mouseUp:(NSEvent *)event
{
    (void)event;
    if (!g_down) return;
    g_down = NO;
    POINT c;
    RECT r;
    plat_cursor(&c);
    if (span(g_from, c, g_draw_k, &r)) {
        add(r, g_draw_k);
        publish();
        finish();
    } else {   // too short to be a wall: try again
        [g_preview orderOut:nil];
    }
}

- (void)rightMouseDown:(NSEvent *)event
{
    (void)event;
    finish();
}
@end

/* ---- public ------------------------------------------------------------ */

void walls_load(void)
{
    g_walls = [NSMutableArray array];
    NSString *text = [[NSUserDefaults standardUserDefaults] stringForKey:@"Walls"];
    for (NSString *one in [text componentsSeparatedByString:@";"]) {
        NSArray<NSString *> *f = [one componentsSeparatedByString:@","];
        if (f.count != 5) continue;
        RECT r = { f[0].intValue, f[1].intValue, f[2].intValue, f[3].intValue };
        add(r, f[4].intValue);   // skips walls on a screen that is gone
    }
    publish();
}

int walls_count(void)
{
    return (int)g_walls.count;
}

BOOL walls_drawing(void)
{
    return g_canvases != nil;
}

void walls_draw(int scale, void (^done)(void))
{
    if (g_canvases || g_walls.count >= MAX_WALLS) return;
    g_draw_k = scale;
    g_done = [done copy];
    g_down = NO;
    g_canvases = [NSMutableArray array];

    NSPoint mouse = [NSEvent mouseLocation];
    CanvasPanel *key = nil;
    for (NSScreen *s in [NSScreen screens]) {
        CanvasPanel *c = [[CanvasPanel alloc] initWithContentRect:s.frame
                                                        styleMask:NSWindowStyleMaskBorderless
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];
        c.opaque = NO;
        c.backgroundColor = [NSColor colorWithWhite:0 alpha:0.01];   // invisible, but it gets the mouse
        c.ignoresMouseEvents = NO;
        c.hasShadow = NO;
        c.level = POP_LEVEL + 1;
        c.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                               NSWindowCollectionBehaviorStationary;
        c.animationBehavior = NSWindowAnimationBehaviorNone;
        c.releasedWhenClosed = NO;
        c.hidesOnDeactivate = NO;
        c.contentView = [[CanvasView alloc] initWithFrame:NSMakeRect(0, 0, s.frame.size.width, s.frame.size.height)];
        [c setFrame:s.frame display:NO];
        [c orderFrontRegardless];
        [g_canvases addObject:c];
        if (!key || NSPointInRect(mouse, s.frame)) key = c;
    }

    [NSCursor.crosshairCursor push];
    g_keymon = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent *(NSEvent *e) {
        if (e.keyCode != 53) return e;   // esc
        finish();
        return nil;
    }];
    g_resign = [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidResignActiveNotification
                                                                 object:nil
                                                                  queue:nil
                                                             usingBlock:^(NSNotification *note) {
        (void)note;
        finish();   // switched to another app: stop drawing
    }];
    [NSApp activateIgnoringOtherApps:YES];
    [key makeKeyAndOrderFront:nil];
    [key makeFirstResponder:key.contentView];
}

void walls_clear(void)
{
    for (Wall *w in g_walls) [w.win orderOut:nil];
    [g_walls removeAllObjects];
    publish();
}

void walls_relayout(void)
{
    for (Wall *w in g_walls) {
        paint(w.win, w.r, w.k);
        show_wall(w);
    }
}

void walls_hide_on(NSSet<NSNumber *> *displays)
{
    if ((!displays.count && !g_hidden_on.count) || [displays isEqualToSet:g_hidden_on]) return;
    g_hidden_on = [displays copy];
    for (Wall *w in g_walls) show_wall(w);
}
