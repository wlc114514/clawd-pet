/*
 * Pomodoro time picker: a small pixel-art window above a pet with two
 * wheels (focus / break minutes) and a start button. Scroll, drag or click
 * a wheel to change it; arrow keys, Enter and Esc work too. Clicking
 * anywhere else closes it. Only one is open at a time.
 */
#import "mac.h"

#define COL_INK    0x2B2B2Bu
#define COL_PAPER  0xFFFFFFu
#define COL_DIM    0xA8A8A8u
#define COL_LINE   0xE6E1D6u
#define COL_ACCENT 0xD77757u
#define COL_HOT    0xC86A4Bu
#define COL_PRESS  0xB25A3Cu

/*
 * Layout in points (the same numbers the Windows picker uses at 96 dpi):
 *
 *     .BBBBBBBBBBBBBBBBBBBB.
 *     B   专注        休息   B     labels      y 10..28
 *     B    20          4    B     previous    y 32..56
 *     B  ──────      ────── B
 *     B   25 分        5 分  B     current     y 58..86
 *     B  ──────      ────── B
 *     B    30          6    B     next        y 88..112
 *     B  [   开始专注    ]   B     button      y 120..146
 *     .BBBBBBBB.BBBBBBBBBBBB.
 *              B.B
 *               B
 */
#define PK_W     184
#define PK_BH    156
#define ROW_PREV 44
#define ROW_CUR  72
#define ROW_NEXT 100
#define LINE_TOP 58
#define LINE_BOT 86
#define BTN_TOP  120
#define BTN_BOT  146

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
// Returns the width.
static CGFloat draw_text(NSString *s, CGFloat size, NSFontWeight weight, uint32_t col,
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
    return sz.width;
}

@interface PickerWindow : NSPanel
@end

@implementation PickerWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

typedef struct { NSString *label; int lo, hi, step, val; } Wheel;

// The model and the drawing live in Picker; the view just forwards events.
@interface Picker () {
    PickerWindow *_win;
    NSView *_view;
    void (^_done)(int, int);
    Wheel _wh[2];
    int _sel;                  // wheel the arrow keys control
    int _u, _W, _H, _tail;     // border unit, window size, tail column (units)
    BOOL _hot, _pressed;       // mouse over / pressing the button
    BOOL _drag;
    CGFloat _dragY;
    BOOL _moved;
    CGFloat _wheelAcc;
}
- (void)draw;
- (void)mouseDown:(NSEvent *)e;
- (void)mouseDragged:(NSEvent *)e;
- (void)mouseUp:(NSEvent *)e;
- (void)mouseMoved:(NSEvent *)e;
- (void)scrollWheel:(NSEvent *)e;
- (void)keyDown:(NSEvent *)e;
@end

@interface PickerView : NSView
@property(nonatomic, weak) Picker *owner;
@end

@implementation PickerView
- (BOOL)isOpaque { return NO; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)drawRect:(NSRect)dirty { (void)dirty; [_owner draw]; }
- (void)mouseDown:(NSEvent *)e { [_owner mouseDown:e]; }
- (void)mouseDragged:(NSEvent *)e { [_owner mouseDragged:e]; }
- (void)mouseUp:(NSEvent *)e { [_owner mouseUp:e]; }
- (void)mouseMoved:(NSEvent *)e { [_owner mouseMoved:e]; }
- (void)scrollWheel:(NSEvent *)e { [_owner scrollWheel:e]; }
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

static Picker *g_picker;   // only one open at a time

@implementation Picker

- (CGFloat)colX:(int)c
{
    return c ? _W * 3 / 4 - _u : _W / 4 + _u;
}

- (BOOL)inButton:(NSPoint)p
{
    return p.x >= 16 && p.x < _W - 16 && p.y >= BTN_TOP && p.y < BTN_BOT;
}

- (void)draw
{
    CGFloat u = _u, W = _W, H = _H, BH = H - 2 * u;
    NSColor *ink = mac_color(COL_INK), *paper = mac_color(COL_PAPER);
    NSColor *line = mac_color(COL_LINE), *accent = mac_color(COL_ACCENT);

    box(0, 0, W, BH, u, ink, paper);

    CGFloat t = _tail * u;
    fill(t, BH - u, t + u, BH, paper);
    fill(t - u, BH, t, BH + u, ink);
    fill(t, BH, t + u, BH + u, paper);
    fill(t + u, BH, t + 2 * u, BH + u, ink);
    fill(t, BH + u, t + u, BH + 2 * u, ink);

    for (int c = 0; c < 2; c++) {
        CGFloat cx = [self colX:c];
        NSColor *lc = c == _sel ? accent : line;
        fill(cx - 32, LINE_TOP, cx + 32, LINE_TOP + u, lc);
        fill(cx - 32, LINE_BOT, cx + 32, LINE_BOT + u, lc);
    }

    box(16, BTN_TOP, W - 16, BTN_BOT, u, ink,
        _pressed && _hot ? mac_color(COL_PRESS) : _hot ? mac_color(COL_HOT) : accent);

    for (int c = 0; c < 2; c++) {
        CGFloat cx = [self colX:c];
        int val = _wh[c].val, step = _wh[c].step, lo = _wh[c].lo, hi = _wh[c].hi;
        draw_text(_wh[c].label, 13, NSFontWeightSemibold, COL_INK, cx, 19, YES);
        if (val - step >= lo)
            draw_text([NSString stringWithFormat:@"%d", val - step], 15, NSFontWeightRegular,
                      COL_DIM, cx, ROW_PREV, YES);
        if (val + step <= hi)
            draw_text([NSString stringWithFormat:@"%d", val + step], 15, NSFontWeightRegular,
                      COL_DIM, cx, ROW_NEXT, YES);

        // The number stays centred; "分" hangs off its right side.
        NSString *num = [NSString stringWithFormat:@"%d", val];
        CGFloat w = draw_text(num, 24, NSFontWeightBold, COL_ACCENT, cx, ROW_CUR, YES);
        draw_text(@"分", 12, NSFontWeightRegular, COL_DIM, cx + w / 2 + 3, ROW_CUR + 3, NO);
    }
    draw_text(@"开始专注", 14, NSFontWeightBold, COL_PAPER, W / 2, (BTN_TOP + BTN_BOT) / 2, YES);
}

- (void)redraw
{
    [_view setNeedsDisplay:YES];
}

- (void)step:(int)c by:(int)n
{
    int v = _wh[c].val + n * _wh[c].step;
    if (v < _wh[c].lo) v = _wh[c].lo;
    if (v > _wh[c].hi) v = _wh[c].hi;
    _sel = c;
    _wh[c].val = v;
    [self redraw];
}

- (void)start
{
    void (^done)(int, int) = _done;
    int f = _wh[0].val, b = _wh[1].val;
    [Picker close];
    if (done) done(f, b);
}

- (void)mouseDown:(NSEvent *)e
{
    NSPoint p = [_view convertPoint:e.locationInWindow fromView:nil];
    if ([self inButton:p]) {
        _pressed = YES;
    } else if (p.y >= 32 && p.y < 114) {
        _drag = YES;
        _dragY = p.y;
        _moved = NO;
        _sel = p.x >= _W / 2;
    }
    [self redraw];
}

- (void)mouseDragged:(NSEvent *)e
{
    if (!_drag) return;
    NSPoint p = [_view convertPoint:e.locationInWindow fromView:nil];
    int n = (int)((_dragY - p.y) / 16);   // drag up for more
    if (n) {
        _dragY -= n * 16;
        _moved = YES;
        [self step:_sel by:n];
    }
}

- (void)mouseUp:(NSEvent *)e
{
    NSPoint p = [_view convertPoint:e.locationInWindow fromView:nil];
    BOOL was_pressed = _pressed, was_drag = _drag;
    _pressed = _drag = NO;
    if (was_pressed && [self inButton:p]) {
        [self start];
        return;
    }
    if (was_drag && !_moved) {   // click a neighbour to step to it
        if (p.y < LINE_TOP) [self step:_sel by:-1];
        else if (p.y > LINE_BOT) [self step:_sel by:1];
    }
    [self redraw];
}

- (void)mouseMoved:(NSEvent *)e
{
    NSPoint p = [_view convertPoint:e.locationInWindow fromView:nil];
    BOOL hot = [self inButton:p];
    if (hot != _hot) {
        _hot = hot;
        [self redraw];
    }
}

- (void)scrollWheel:(NSEvent *)e
{
    NSPoint p = [_view convertPoint:e.locationInWindow fromView:nil];
    int c = p.x >= _W / 2;
    if (c != _sel) {
        _wheelAcc = 0;
        _sel = c;
    }
    if (e.hasPreciseScrollingDeltas) _wheelAcc += e.scrollingDeltaY / 10.0;
    else _wheelAcc += e.scrollingDeltaY > 0 ? 1 : -1;   // one notch, one step

    int n = 0;
    for (; _wheelAcc >= 1; _wheelAcc -= 1) n--;   // away from you: smaller
    for (; _wheelAcc <= -1; _wheelAcc += 1) n++;
    if (n) [self step:c by:n];
    else [self redraw];
}

- (void)keyDown:(NSEvent *)e
{
    switch (e.keyCode) {
    case 126: [self step:_sel by:-1]; break;      // up
    case 125: [self step:_sel by:1]; break;       // down
    case 123: case 124: case 48:                  // left, right, tab
        _sel = !_sel;
        [self redraw];
        break;
    case 36: case 76: [self start]; break;        // return, enter
    case 53: [Picker close]; break;               // esc
    default: break;
    }
}

- (void)windowResignedKey:(NSNotification *)note
{
    (void)note;
    [Picker close];
}

+ (void)close
{
    Picker *p = g_picker;
    if (!p) return;
    g_picker = nil;
    [[NSNotificationCenter defaultCenter] removeObserver:p];
    [p->_win orderOut:nil];
    p->_win = nil;
    p->_view = nil;
}

+ (void)openAt:(POINT)anchor area:(RECT)area focus:(int)focus break:(int)brk
          done:(void (^)(int, int))done
{
    [Picker close];

    Picker *p = [[Picker alloc] init];
    g_picker = p;
    p->_done = [done copy];
    p->_wh[0] = (Wheel){ @"专注", FOCUS_LO, FOCUS_HI, FOCUS_STEP, focus };
    p->_wh[1] = (Wheel){ @"休息", BREAK_LO, BREAK_HI, 1, brk };
    p->_u = 2;
    p->_W = PK_W;
    p->_H = PK_BH + 2 * p->_u;

    POINT pos = { anchor.x - p->_W / 2, anchor.y - p->_H };
    if (pos.x + p->_W > area.right) pos.x = area.right - p->_W;
    if (pos.x < area.left) pos.x = area.left;
    if (pos.y < area.top) pos.y = area.top;
    p->_tail = (anchor.x - pos.x) / p->_u;
    if (p->_tail < 2) p->_tail = 2;
    if (p->_tail > p->_W / p->_u - 3) p->_tail = p->_W / p->_u - 3;

    p->_win = [[PickerWindow alloc] initWithContentRect:mac_cocoa_rect(pos.x, pos.y, p->_W, p->_H)
                                              styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    p->_win.opaque = NO;
    p->_win.backgroundColor = [NSColor clearColor];
    p->_win.hasShadow = NO;
    p->_win.level = POP_LEVEL;
    p->_win.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                 NSWindowCollectionBehaviorStationary;
    p->_win.animationBehavior = NSWindowAnimationBehaviorNone;
    p->_win.releasedWhenClosed = NO;
    p->_win.hidesOnDeactivate = NO;

    PickerView *view = [[PickerView alloc] initWithFrame:NSMakeRect(0, 0, p->_W, p->_H)];
    view.owner = p;
    p->_view = view;
    p->_win.contentView = view;

    [[NSNotificationCenter defaultCenter] addObserver:p
                                             selector:@selector(windowResignedKey:)
                                                 name:NSWindowDidResignKeyNotification
                                               object:p->_win];

    [NSApp activateIgnoringOtherApps:YES];
    [p->_win makeKeyAndOrderFront:nil];
    [p->_win makeFirstResponder:view];
}

@end
