/*
 * Wardrobe: a pixel-art window above a pet with a big preview of it on the
 * left and every accessory on the right, one row per slot. Click one to put
 * it on (it replaces whatever was in that slot), click it again to take it
 * off; buttons for a random outfit and for taking everything off. Every
 * change goes straight to `changed`. Esc, the close box or clicking anywhere
 * else closes it. Only one is open at a time.
 */
#import "mac.h"

#define COL_INK    0x2B2B2Bu
#define COL_PAPER  0xFFFFFFu
#define COL_DIM    0xA8A8A8u
#define COL_LINE   0xE6E1D6u
#define COL_STAGE  0xF7F4EEu
#define COL_WORN   0xFBEDE7u
#define COL_ACCENT 0xD77757u
#define COL_HOT    0xC86A4Bu
#define COL_PRESS  0xB25A3Cu

/*
 * Layout in points (the same numbers the Windows wardrobe uses at 96 dpi):
 *
 *     .BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.
 *     B  换装                                [×]  B    title         y 10..32
 *     B  +---------+  头    [  ] [  ] [  ] [  ]   B    a row a slot  y 42.. (ROW_H each)
 *     B  |         |  脸    [  ] [  ] [  ]        B
 *     B  |  Clawd  |  嘴    [  ] [  ] [  ] [  ]   B
 *     B  |         |  脖子  [  ] [  ]             B
 *     B  |         |  手    [  ] [  ] [  ] [  ]   B
 *     B  +---------+        [  随机  ] [全部脱掉]  B    buttons       y 350..378
 *     .BBBBBBBBBBBBBBBBBBBBBB.BBBBBBBBBBBBBBBBBBBB.
 *                           B.B
 *                            B
 */
#define WD_W      496
#define WD_BH     392
#define TOP       42
#define PV_X0     14
#define PV_X1     152
#define LABEL_X   166
#define CELL_X    210
#define CELL_W    64
#define CELL_H    54
#define CELL_GAP  6
#define ROW_H     60
#define ICON_H    36   // top part of a cell; the name goes below
#define BTN_Y0    350
#define BTN_Y1    378
#define CLOSE_SZ  22

enum { HIT_NONE = -1, HIT_RANDOM = 100, HIT_CLEAR, HIT_CLOSE };   // or an ACC_*

static void fill(CGFloat x0, CGFloat y0, CGFloat x1, CGFloat y1, NSColor *c)
{
    [c set];
    NSRectFill(NSMakeRect(x0, y0, x1 - x0, y1 - y0));
}

// Chamfered box with a one-unit border, like the text bubbles.
static void box(CGFloat x0, CGFloat y0, CGFloat x1, CGFloat y1, CGFloat u,
                NSColor *border, NSColor *inside)
{
    fill(x0 + u, y0, x1 - u, y0 + u, border);
    fill(x0 + u, y1 - u, x1 - u, y1, border);
    fill(x0, y0 + u, x0 + u, y1 - u, border);
    fill(x1 - u, y0 + u, x1, y1 - u, border);
    fill(x0 + u, y0 + u, x1 - u, y1 - u, inside);
}

// Draws s with its left edge at x (or centred on x), vertically centred on cy.
static void draw_text(NSString *s, CGFloat size, NSFontWeight weight, uint32_t col,
                      CGFloat x, CGFloat cy, BOOL center)
{
    NSDictionary *attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:size weight:weight],
        NSForegroundColorAttributeName: mac_color(col),
    };
    NSAttributedString *as = [[NSAttributedString alloc] initWithString:s attributes:attrs];
    NSSize sz = as.size;
    if (center) x -= sz.width / 2;
    [as drawAtPoint:NSMakePoint(round(x), round(cy - sz.height / 2))];
}

// Part of a canvas (sx, sy, sw by sh), each pixel k by k points, at (x, y).
static void blit(const Canvas *c, int sx, int sy, int sw, int sh, CGFloat x, CGFloat y, CGFloat k)
{
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sw; i++) {
            uint32_t s = c->px[(sy + j) * c->w + sx + i], a = s >> 24;
            if (!a) continue;
            [[NSColor colorWithSRGBRed:((s >> 16) & 255) / 255.0
                                 green:((s >> 8) & 255) / 255.0
                                  blue:(s & 255) / 255.0
                                 alpha:a / 255.0] set];
            NSRectFillUsingOperation(NSMakeRect(x + i * k, y + j * k, k, k), NSCompositingOperationSourceOver);
        }
}

@interface WardrobeWindow : NSPanel
@end

@implementation WardrobeWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

// The model and the drawing live in Wardrobe; the view just forwards events.
@interface Wardrobe () {
    WardrobeWindow *_win;
    NSView *_view;
    NSTimer *_timer;
    void (^_changed)(int, const unsigned char *);
    int _pet;
    unsigned char _wear[SLOTS];
    unsigned _rng;
    int _u, _W, _H, _tail;   // border unit, window size, tail column (units)
    int _hot, _pressed;      // HIT_* under the mouse / being pressed
    int _tick, _shown;       // animation clock; what the preview last showed
}
- (void)draw;
- (void)mouseDown:(NSEvent *)e;
- (void)mouseUp:(NSEvent *)e;
- (void)mouseMoved:(NSEvent *)e;
- (void)keyDown:(NSEvent *)e;
@end

@interface WardrobeView : NSView
@property(nonatomic, weak) Wardrobe *owner;
@end

@implementation WardrobeView
- (BOOL)isOpaque { return NO; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)drawRect:(NSRect)dirty { (void)dirty; [_owner draw]; }
- (void)mouseDown:(NSEvent *)e { [_owner mouseDown:e]; }
- (void)mouseUp:(NSEvent *)e { [_owner mouseUp:e]; }
- (void)mouseMoved:(NSEvent *)e { [_owner mouseMoved:e]; }
- (void)mouseDragged:(NSEvent *)e { [_owner mouseMoved:e]; }
- (void)keyDown:(NSEvent *)e { [_owner keyDown:e]; }

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    for (NSTrackingArea *a in self.trackingAreas) [self removeTrackingArea:a];
    NSTrackingArea *ta = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                     options:NSTrackingMouseMoved | NSTrackingActiveAlways |
                                                             NSTrackingInVisibleRect
                                                       owner:self
                                                    userInfo:nil];
    [self addTrackingArea:ta];
}
@end

static Wardrobe *g_wardrobe;   // only one open at a time

@implementation Wardrobe

/* ---- layout ----------------------------------------------------------- */

static NSRect cell_rect(int a)
{
    int s = acc_slot(a), col = 0;
    for (int b = 1; b < a; b++) col += acc_slot(b) == s;
    return NSMakeRect(CELL_X + col * (CELL_W + CELL_GAP), TOP + s * ROW_H, CELL_W, CELL_H);
}

static NSRect button_rect(int which)
{
    int x0 = which == HIT_RANDOM ? CELL_X : CELL_X + 2 * (CELL_W + CELL_GAP);
    return NSMakeRect(x0, BTN_Y0, 2 * CELL_W + CELL_GAP, BTN_Y1 - BTN_Y0);
}

- (NSRect)closeRect
{
    return NSMakeRect(_W - 12 - CLOSE_SZ, 10, CLOSE_SZ, CLOSE_SZ);
}

- (int)hit:(NSPoint)p
{
    for (int a = 1; a < ACC_COUNT; a++)
        if (NSPointInRect(p, cell_rect(a))) return a;
    for (int b = HIT_RANDOM; b <= HIT_CLEAR; b++)
        if (NSPointInRect(p, button_rect(b))) return b;
    return NSPointInRect(p, [self closeRect]) ? HIT_CLOSE : HIT_NONE;
}

/* ---- drawing ---------------------------------------------------------- */

// What the preview shows this tick: blinking now and then, looking about.
- (Pose)previewPose
{
    Pose q = pose_stand();
    static const int looks[4] = { 0, 1, 0, -1 };
    q.look = looks[(_tick / 50) % 4];
    if (_tick % 70 < 3) q.eyes = EYE_CLOSED;
    memcpy(q.wear, _wear, sizeof q.wear);
    q.tick = _tick;
    return q;
}

- (int)previewKey
{
    Pose q = [self previewPose];
    return q.eyes * 100 + (q.look + 1) * 10 + (q.tick / 8) % 2;
}

- (void)drawPreviewIn:(NSRect)r
{
    static uint32_t lp[CW * CH];
    Canvas c = { lp, CW, CH };
    Pose q = [self previewPose];
    cv_clear(&c);
    draw_clawd(&c, SPR_OX, SPR_OY, &q);

    // Columns 7..26 and rows 8..23 hold the sprite with any hat and cup.
    // The body is centred, so the wider side (the cup's) decides the size.
    int sx = SPR_OX + 1, sw = 20, sy = SPR_OY - 6, sh = FOOT_Y - sy;
    int mid = (SPR_OX + 9) - sx, side = sw - mid > mid ? sw - mid : mid;
    int k = (int)((r.size.width / 2 - 10) / side);
    if (k > (int)(r.size.height / (sh + 8))) k = (int)(r.size.height / (sh + 8));
    if (k < 1) k = 1;
    CGFloat x = NSMidX(r) - mid * k, feet = round(r.origin.y + r.size.height * 5 / 8);
    fill(r.origin.x + _u, feet, NSMaxX(r) - _u, feet + _u, mac_color(COL_LINE));   // the floor
    blit(&c, sx, sy, sw, sh, round(x), feet - sh * k, k);
}

- (void)drawIcon:(int)a in:(NSRect)r
{
    static uint32_t lp[CW * CH];
    Canvas c = { lp, CW, CH };
    Pose q = pose_stand();
    q.wear[acc_slot(a)] = (unsigned char)a;

    // Its size, over both frames of any animation.
    int x0 = CW, y0 = CH, x1 = -1, y1 = -1;
    for (int t = 0; t < 16; t += 8) {
        q.tick = t;
        cv_clear(&c);
        draw_outfit(&c, SPR_OX, SPR_OY, &q);
        for (int y = 0; y < CH; y++)
            for (int x = 0; x < CW; x++)
                if (lp[y * CW + x] >> 24) {
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
    }
    int w = x1 - x0 + 1, h = y1 - y0 + 1, k = 4;
    if (k > (CELL_W - 10) / w) k = (CELL_W - 10) / w;
    if (k > (ICON_H - 8) / h) k = (ICON_H - 8) / h;
    if (k < 1) k = 1;

    q.tick = _tick;
    cv_clear(&c);
    draw_outfit(&c, SPR_OX, SPR_OY, &q);
    CGFloat cx = NSMidX(r), cy = r.origin.y + ICON_H / 2 + 2;
    blit(&c, x0, y0, w, h, round(cx - w * k / 2.0), round(cy - h * k / 2.0), k);
}

- (void)draw
{
    CGFloat u = _u, W = _W, H = _H, BH = H - 2 * u;
    NSColor *ink = mac_color(COL_INK), *paper = mac_color(COL_PAPER), *line = mac_color(COL_LINE);
    NSColor *accent = mac_color(COL_ACCENT), *worn_bg = mac_color(COL_WORN);

    box(0, 0, W, BH, u, ink, paper);
    CGFloat t = _tail * u;
    fill(t, BH - u, t + u, BH, paper);
    fill(t - u, BH, t, BH + u, ink);
    fill(t, BH, t + u, BH + u, paper);
    fill(t + u, BH, t + 2 * u, BH + u, ink);
    fill(t, BH + u, t + u, BH + 2 * u, ink);

    // The preview, on a little stage.
    box(PV_X0, TOP, PV_X1, BTN_Y1, u, line, mac_color(COL_STAGE));
    [self drawPreviewIn:NSMakeRect(PV_X0, TOP, PV_X1 - PV_X0, BTN_Y1 - TOP)];

    draw_text(@"换装", 15, NSFontWeightBold, COL_INK, PV_X0 + 2, 21, NO);
    for (int s = 0; s < SLOTS; s++)
        draw_text(@(acc_slot_name(s)), 13, NSFontWeightSemibold, COL_INK, LABEL_X, TOP + s * ROW_H + CELL_H / 2, NO);

    for (int a = 1; a < ACC_COUNT; a++) {
        NSRect r = cell_rect(a);
        CGFloat x0 = r.origin.x, y0 = r.origin.y, x1 = NSMaxX(r), y1 = NSMaxY(r);
        BOOL worn = _wear[acc_slot(a)] == a;
        NSColor *border = worn ? accent : _hot == a ? mac_color(COL_DIM) : line;
        box(x0, y0, x1, y1, u, border, worn ? worn_bg : paper);
        if (worn) box(x0 + u, y0 + u, x1 - u, y1 - u, u, accent, worn_bg);   // thicker
        [self drawIcon:a in:r];
        draw_text(@(acc_name(a)), 11, worn ? NSFontWeightSemibold : NSFontWeightRegular,
                  worn ? COL_PRESS : COL_INK, NSMidX(r), y0 + ICON_H + (CELL_H - ICON_H) / 2 - u / 2, YES);
    }

    for (int b = HIT_RANDOM; b <= HIT_CLEAR; b++) {
        NSRect r = button_rect(b);
        BOOL on = _hot == b;
        NSColor *fc = b == HIT_RANDOM ? (on && _pressed == b ? mac_color(COL_PRESS) : on ? mac_color(COL_HOT) : accent)
                                      : (on ? line : paper);
        box(r.origin.x, r.origin.y, NSMaxX(r), NSMaxY(r), u, ink, fc);
        draw_text(b == HIT_RANDOM ? @"随机搭配" : @"全部脱掉", 13, NSFontWeightBold,
                  b == HIT_RANDOM ? COL_PAPER : COL_INK, NSMidX(r), NSMidY(r), YES);
    }

    NSRect cr = [self closeRect];
    if (_hot == HIT_CLOSE) box(cr.origin.x, cr.origin.y, NSMaxX(cr), NSMaxY(cr), u, line, line);
    for (int i = 0; i < 5; i++) {   // ×, in border units
        CGFloat cx = NSMidX(cr) - u / 2, cy = NSMidY(cr) - u / 2, d = (i - 2) * u;
        fill(cx + d, cy + d, cx + d + u, cy + d + u, ink);
        fill(cx + d, cy - d, cx + d + u, cy - d + u, ink);
    }
    _shown = [self previewKey];
}

- (void)redraw
{
    [_view setNeedsDisplay:YES];
}

/* ---- behaviour -------------------------------------------------------- */

- (void)changed
{
    [self redraw];
    if (_changed) _changed(_pet, _wear);
}

- (void)press:(int)what
{
    if (what > ACC_NONE && what < ACC_COUNT) {
        unsigned char *slot = &_wear[acc_slot(what)];
        *slot = *slot == what ? ACC_NONE : (unsigned char)what;
        [self changed];
    } else if (what == HIT_RANDOM) {
        unsigned char was[SLOTS];
        memcpy(was, _wear, sizeof was);
        static const unsigned char none[SLOTS];
        do outfit_roll(_wear, &_rng);   // something new, and not nothing
        while (!memcmp(was, _wear, sizeof was) || !memcmp(none, _wear, sizeof none));
        [self changed];
    } else if (what == HIT_CLEAR) {
        memset(_wear, 0, sizeof _wear);
        [self changed];
    } else if (what == HIT_CLOSE) {
        [Wardrobe close];
    }
}

- (void)timerFired:(NSTimer *)t
{
    (void)t;
    _tick++;
    if ([self previewKey] != _shown) [self redraw];
}

- (void)mouseDown:(NSEvent *)e
{
    _pressed = [self hit:[_view convertPoint:e.locationInWindow fromView:nil]];
    [self redraw];
}

- (void)mouseUp:(NSEvent *)e
{
    int was = _pressed;
    _pressed = HIT_NONE;
    if (was != HIT_NONE && was == [self hit:[_view convertPoint:e.locationInWindow fromView:nil]])
        [self press:was];
    else
        [self redraw];
}

- (void)mouseMoved:(NSEvent *)e
{
    int h = [self hit:[_view convertPoint:e.locationInWindow fromView:nil]];
    if (h != _hot) {
        _hot = h;
        [self redraw];
    }
}

- (void)keyDown:(NSEvent *)e
{
    if (e.keyCode == 53 || e.keyCode == 36 || e.keyCode == 76) [Wardrobe close];   // esc, return, enter
}

- (void)windowResignedKey:(NSNotification *)note
{
    (void)note;
    [Wardrobe close];
}

+ (void)close
{
    Wardrobe *w = g_wardrobe;
    if (!w) return;
    g_wardrobe = nil;
    [[NSNotificationCenter defaultCenter] removeObserver:w];
    [w->_timer invalidate];
    w->_timer = nil;
    [w->_win orderOut:nil];
    w->_win = nil;
    w->_view = nil;
}

+ (int)pet
{
    return g_wardrobe ? g_wardrobe->_pet : 0;
}

+ (void)openAt:(POINT)anchor area:(RECT)area pet:(int)petId wear:(const unsigned char *)wear
       changed:(void (^)(int, const unsigned char *))changed
{
    [Wardrobe close];

    Wardrobe *w = [[Wardrobe alloc] init];
    g_wardrobe = w;
    w->_changed = [changed copy];
    w->_pet = petId;
    memcpy(w->_wear, wear, sizeof w->_wear);
    w->_rng = (unsigned)plat_ticks_ms() | 1;
    w->_hot = w->_pressed = HIT_NONE;
    w->_u = 2;
    w->_W = WD_W;
    w->_H = WD_BH + 2 * w->_u;

    POINT pos = { anchor.x - w->_W / 2, anchor.y - w->_H };
    if (pos.x + w->_W > area.right) pos.x = area.right - w->_W;
    if (pos.x < area.left) pos.x = area.left;
    if (pos.y < area.top) pos.y = area.top;
    w->_tail = (anchor.x - pos.x) / w->_u;
    if (w->_tail < 2) w->_tail = 2;
    if (w->_tail > w->_W / w->_u - 3) w->_tail = w->_W / w->_u - 3;

    w->_win = [[WardrobeWindow alloc] initWithContentRect:mac_cocoa_rect(pos.x, pos.y, w->_W, w->_H)
                                                styleMask:NSWindowStyleMaskBorderless
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    w->_win.opaque = NO;
    w->_win.backgroundColor = [NSColor clearColor];
    w->_win.hasShadow = NO;
    w->_win.level = POP_LEVEL;
    w->_win.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                 NSWindowCollectionBehaviorStationary;
    w->_win.animationBehavior = NSWindowAnimationBehaviorNone;
    w->_win.releasedWhenClosed = NO;
    w->_win.hidesOnDeactivate = NO;

    WardrobeView *view = [[WardrobeView alloc] initWithFrame:NSMakeRect(0, 0, w->_W, w->_H)];
    view.owner = w;
    w->_view = view;
    w->_win.contentView = view;

    [[NSNotificationCenter defaultCenter] addObserver:w
                                             selector:@selector(windowResignedKey:)
                                                 name:NSWindowDidResignKeyNotification
                                               object:w->_win];
    w->_timer = [NSTimer scheduledTimerWithTimeInterval:0.05
                                                 target:w
                                               selector:@selector(timerFired:)
                                               userInfo:nil
                                                repeats:YES];

    [NSApp activateIgnoringOtherApps:YES];
    [w->_win makeKeyAndOrderFront:nil];
    [w->_win makeFirstResponder:view];
}

@end
