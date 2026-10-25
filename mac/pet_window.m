/*
 * One Clawd on screen: a borderless, non-activating panel that shows the
 * sprite, and a second click-through panel for the stairs it climbs.
 */
#import "mac.h"
#import <QuartzCore/QuartzCore.h>

#include <limits.h>
#include <math.h>
#include <string.h>

@interface PetView : NSView
@property(nonatomic, weak) PetWindow *owner;
@end

@implementation PetView
- (BOOL)isOpaque { return NO; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { (void)event; return YES; }
- (void)mouseDown:(NSEvent *)event { [_owner mouseDown:event]; }
- (void)mouseDragged:(NSEvent *)event { [_owner mouseDragged:event]; }
- (void)mouseUp:(NSEvent *)event { [_owner mouseUp:event]; }
- (void)rightMouseDown:(NSEvent *)event { [_owner rightMouseDown:event]; }
@end

@interface PetWindow (Mouse)
- (void)mouseDown:(NSEvent *)event;
- (void)mouseDragged:(NSEvent *)event;
- (void)mouseUp:(NSEvent *)event;
- (void)rightMouseDown:(NSEvent *)event;
@end

static PetWindow *g_press;   // the pet being pressed, if any

PetWindow *pet_window_pressed(void)
{
    return g_press;
}

@interface PetWindow () {
    Pet _pet;
    TextBubble *_bubble;

    uint32_t _px[CW * CH];
    uint32_t _prev[CW * CH];
    int _prevX, _prevY;
    BOOL _dirty;

    // the stairs, in a window of their own
    NSPanel *_stWin;
    NSView *_stView;
    uint32_t _stPx[STAIRS_CW * STAIRS_CH];
    uint32_t _stPrev[STAIRS_CW * STAIRS_CH];
    int _stW, _stH, _stK, _stX, _stY;

    // while it is in the air: a bigger panel just below it, there to the mouse
    // but not to the eye, so it is easier to catch
    NSPanel *_catchWin;

    BOOL _press, _dragging;
    POINT _pressPt;
    BOOL _hidden;
}
@end

@implementation PetWindow

- (instancetype)initWithPet:(Pet)p
{
    int k = p.scale;
    self = [super initWithContentRect:NSMakeRect(0, 0, CW * k, CH * k)
                            styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                              backing:NSBackingStoreBuffered
                                defer:NO];
    if (!self) return nil;
    _pet = p;
    _prevX = _prevY = INT_MIN;
    _dirty = YES;

    self.opaque = NO;
    self.backgroundColor = [NSColor clearColor];
    self.hasShadow = NO;
    self.level = PET_LEVEL;
    // Rides along across Spaces but stays off fullscreen ones, so a game or
    // a video hides it the way the Windows build does.
    self.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                              NSWindowCollectionBehaviorStationary |
                              NSWindowCollectionBehaviorIgnoresCycle;
    self.animationBehavior = NSWindowAnimationBehaviorNone;
    self.releasedWhenClosed = NO;
    self.hidesOnDeactivate = NO;

    PetView *v = [[PetView alloc] initWithFrame:NSMakeRect(0, 0, CW * k, CH * k)];
    v.owner = self;
    v.wantsLayer = YES;
    v.layer.opaque = NO;
    v.layer.contentsGravity = kCAGravityResize;
    v.layer.magnificationFilter = kCAFilterNearest;
    v.layer.minificationFilter = kCAFilterNearest;
    self.contentView = v;

    _bubble = [[TextBubble alloc] init];

    [self present];
    [self orderFrontRegardless];
    return self;
}

- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }

// Let the pet stand on the Dock and wander over the menu bar's strip.
- (NSRect)constrainFrameRect:(NSRect)frameRect toScreen:(NSScreen *)screen
{
    (void)screen;
    return frameRect;
}

- (Pet *)pet { return &_pet; }
- (TextBubble *)bubble { return _bubble; }

- (BOOL)hiddenByFullscreen
{
    return _hidden;
}

- (void)setHiddenByFullscreen:(BOOL)hidden
{
    if (hidden == _hidden) return;
    _hidden = hidden;
    _dirty = YES;
    if (hidden) {
        [_bubble suspend];
        [self destroyStairs];   // rebuilt from the pet's state when it comes back
        [_catchWin orderOut:nil];
        [self orderOut:nil];
    } else {
        [self present];
        [self orderFrontRegardless];
    }
}

- (void)invalidate
{
    _dirty = YES;
    _prevX = _prevY = INT_MIN;
}

- (void)present
{
    int k = _pet.scale;
    Canvas cv = { _px, CW, CH };
    pet_render(&_pet, &cv);

    int x = (int)floor(_pet.x + 0.5), y = (int)floor(_pet.y + 0.5);
    if (!_dirty && x == _prevX && y == _prevY && !memcmp(_px, _prev, sizeof _px)) return;
    _dirty = NO;
    _prevX = x;
    _prevY = y;
    memcpy(_prev, _px, sizeof _px);

    uint32_t *scaled = malloc((size_t)CW * CH * k * k * 4);
    cv_scale_premul(&cv, scaled, k);
    CGImageRef img = mac_cgimage_take(scaled, CW * k, CH * k);

    NSRect want = mac_cocoa_rect(x, y, CW * k, CH * k);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.contentView.layer.contents = (__bridge id)img;
    if (!NSEqualRects(self.frame, want)) [self setFrame:want display:NO];
    [CATransaction commit];
    CGImageRelease(img);
}

- (void)destroyStairs
{
    if (!_stWin) return;
    [_stWin orderOut:nil];
    _stWin = nil;
    _stView = nil;
}

- (void)presentStairs
{
    Canvas cv = { _stPx, 0, 0 };
    POINT pos;
    if (!pet_stairs(&_pet, g_stair_style, &cv, &pos)) {
        [self destroyStairs];
        return;
    }
    int k = _pet.stairs.scale;
    BOOL first = !_stWin;
    if (first) {
        _stWin = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 1, 1)
                                            styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
        _stWin.opaque = NO;
        _stWin.backgroundColor = [NSColor clearColor];
        _stWin.hasShadow = NO;
        _stWin.level = PET_LEVEL;
        _stWin.ignoresMouseEvents = YES;   // clicks go through to whatever is behind
        _stWin.collectionBehavior = self.collectionBehavior;
        _stWin.animationBehavior = NSWindowAnimationBehaviorNone;
        _stWin.releasedWhenClosed = NO;
        _stWin.hidesOnDeactivate = NO;
        _stView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
        _stView.wantsLayer = YES;
        _stView.layer.opaque = NO;
        _stView.layer.contentsGravity = kCAGravityResize;
        _stView.layer.magnificationFilter = kCAFilterNearest;
        _stView.layer.minificationFilter = kCAFilterNearest;
        _stWin.contentView = _stView;
        _stW = _stH = _stK = 0;
    }

    size_t bytes = (size_t)cv.w * cv.h * 4;
    if (first || cv.w != _stW || cv.h != _stH || k != _stK ||
        pos.x != _stX || pos.y != _stY || memcmp(_stPx, _stPrev, bytes)) {
        _stW = cv.w;
        _stH = cv.h;
        _stK = k;
        _stX = pos.x;
        _stY = pos.y;
        memcpy(_stPrev, _stPx, bytes);

        uint32_t *scaled = malloc((size_t)cv.w * cv.h * k * k * 4);
        cv_scale_premul(&cv, scaled, k);
        CGImageRef img = mac_cgimage_take(scaled, cv.w * k, cv.h * k);

        NSRect want = mac_cocoa_rect(pos.x, pos.y, cv.w * k, cv.h * k);
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        _stView.layer.contents = (__bridge id)img;
        if (!NSEqualRects(_stWin.frame, want)) [_stWin setFrame:want display:NO];
        [CATransaction commit];
        CGImageRelease(img);
    }

    if (!_stWin.isVisible) [_stWin orderFrontRegardless];
    // Just below its pet.
    [_stWin orderWindow:NSWindowBelow relativeTo:self.windowNumber];
}

- (void)presentCatch
{
    RECT r;
    if (!pet_catch_area(&_pet, &r)) {
        // Not while it is being held: the panel that took the press gets the rest of it.
        if (_catchWin.isVisible && !(_press && g_press == self)) [_catchWin orderOut:nil];
        return;
    }
    if (!_catchWin) {
        _catchWin = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 1, 1)
                                               styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
        _catchWin.opaque = NO;
        _catchWin.backgroundColor = [NSColor colorWithWhite:0 alpha:0.01];   // hit by the mouse, unseen
        _catchWin.hasShadow = NO;
        _catchWin.level = PET_LEVEL;
        _catchWin.collectionBehavior = self.collectionBehavior;
        _catchWin.animationBehavior = NSWindowAnimationBehaviorNone;
        _catchWin.releasedWhenClosed = NO;
        _catchWin.hidesOnDeactivate = NO;
        PetView *v = [[PetView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
        v.owner = self;   // clicks on it are clicks on its pet
        _catchWin.contentView = v;
    }
    NSRect want = mac_cocoa_rect(r.left, r.top, r.right - r.left, r.bottom - r.top);
    if (!NSEqualRects(_catchWin.frame, want)) [_catchWin setFrame:want display:NO];
    if (!_catchWin.isVisible) [_catchWin orderFrontRegardless];
    [_catchWin orderWindow:NSWindowBelow relativeTo:self.windowNumber];
}

/* ---- mouse ------------------------------------------------------------- */

- (void)mouseDown:(NSEvent *)event
{
    (void)event;
    g_press = self;
    _press = YES;
    _dragging = NO;
    plat_cursor(&_pressPt);
    if (pet_catch(&_pet, _pressPt)) _dragging = YES;   // in the air: caught there and then
}

- (void)mouseDragged:(NSEvent *)event
{
    (void)event;
    if (!_press || _dragging) return;
    POINT c;
    plat_cursor(&c);
    if (abs((int)(c.x - _pressPt.x)) > 4 || abs((int)(c.y - _pressPt.y)) > 4) {
        _dragging = YES;
        pet_begin_drag(&_pet, _pressPt);
    }
}

- (void)mouseUp:(NSEvent *)event
{
    (void)event;
    if (g_press != self) return;
    BOOL was_drag = _dragging;
    g_press = nil;
    _press = NO;
    _dragging = NO;
    if (was_drag) {
        pet_end_drag(&_pet);
    } else {
        app_pet_clicked();
        pet_click(&_pet);
    }
}

- (void)rightMouseDown:(NSEvent *)event
{
    app_show_menu(self, event);
}

@end
