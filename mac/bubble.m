/*
 * Chinese text bubble: a pixel-art speech bubble in a window of its own,
 * floating above a pet. Click it to dismiss.
 */
#import "mac.h"

#define COL_INK   0x2B2B2Bu
#define COL_PAPER 0xFFFFFFu
#define TB_U      2        // border unit, in points

@interface BubbleView : NSView
@property(nonatomic) CGFloat u;
@property(nonatomic) int uw, uh, tail;   // box size and tail column, in border units
@property(nonatomic, copy) NSAttributedString *text;
@property(nonatomic) NSSize textSize;
@property(nonatomic, weak) TextBubble *owner;
@end

@implementation BubbleView
- (BOOL)isOpaque { return NO; }
- (BOOL)isFlipped { return YES; }

/*
 * Chamfered box with a 1-unit ink border, and a little stepped tail under
 * column `tail`:
 *
 *    .BBBBBBBB.
 *    B........B
 *    B..text..B
 *    .BBB.BBBB.
 *      B.B
 *       B
 */
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    CGFloat u = _u;
    NSColor *ink = mac_color(COL_INK), *paper = mac_color(COL_PAPER);

    for (int uy = 0; uy < _uh + 2; uy++)
        for (int ux = 0; ux < _uw; ux++) {
            uint32_t col = 0;
            if (uy < _uh) {
                int edge_x = ux == 0 || ux == _uw - 1, edge_y = uy == 0 || uy == _uh - 1;
                if (edge_x && edge_y) continue;
                col = (edge_x || edge_y) ? COL_INK : COL_PAPER;
                if (uy == _uh - 1 && ux == _tail) col = COL_PAPER;
            } else if (uy == _uh) {
                if (ux == _tail) col = COL_PAPER;
                else if (ux == _tail - 1 || ux == _tail + 1) col = COL_INK;
            } else if (ux == _tail) {
                col = COL_INK;
            }
            if (!col) continue;
            [(col == COL_INK ? ink : paper) set];
            NSRectFill(NSMakeRect(ux * u, uy * u, u, u));
        }

    CGFloat box_h = _uh * u;
    [_text drawAtPoint:NSMakePoint(floor((self.bounds.size.width - _textSize.width) / 2),
                                  floor((box_h - _textSize.height) / 2))];
}

- (void)mouseDown:(NSEvent *)event
{
    (void)event;
    [_owner hide];
}
@end

@interface TextBubble () {
    NSPanel *_win;
    BubbleView *_view;
    NSAttributedString *_text;
    NSSize _textSize;
    ULONGLONG _until;   // deadline, 0 = not showing
    int _u, _uw, _uh, _w, _h, _tail;
    BOOL _shown;
}
@end

@implementation TextBubble

static NSAttributedString *tb_text(NSString *s, NSSize *size)
{
    NSMutableParagraphStyle *para = [[NSMutableParagraphStyle alloc] init];
    para.alignment = NSTextAlignmentCenter;
    para.lineBreakMode = NSLineBreakByClipping;
    NSDictionary *attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: mac_color(COL_INK),
        NSParagraphStyleAttributeName: para,
    };
    NSAttributedString *as = [[NSAttributedString alloc] initWithString:s attributes:attrs];
    NSRect r = [as boundingRectWithSize:NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX)
                                options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading];
    *size = NSMakeSize(ceil(r.size.width), ceil(r.size.height));
    return as;
}

- (void)makeWindow
{
    _win = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 1, 1)
                                      styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
    _win.opaque = NO;
    _win.backgroundColor = [NSColor clearColor];
    _win.hasShadow = NO;
    _win.level = POP_LEVEL;   // above the pets
    _win.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                              NSWindowCollectionBehaviorStationary |
                              NSWindowCollectionBehaviorIgnoresCycle;
    _win.animationBehavior = NSWindowAnimationBehaviorNone;
    _win.releasedWhenClosed = NO;
    _win.hidesOnDeactivate = NO;

    _view = [[BubbleView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
    _view.owner = self;
    _view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _win.contentView = _view;
}

- (void)show:(NSString *)text ms:(int)ms
{
    if (!_win) [self makeWindow];

    _text = tb_text(text, &_textSize);
    _u = TB_U;
    _uw = (int)ceil((_textSize.width + 10 * _u) / _u);
    _uh = (int)ceil((_textSize.height + 7 * _u) / _u);
    _w = _uw * _u;
    _h = (_uh + 2) * _u;   // two extra rows for the tail

    _until = plat_ticks_ms() + ms;
    _tail = -1;
    _shown = NO;
    _view.u = _u;
    _view.uw = _uw;
    _view.uh = _uh;
    _view.text = _text;
    _view.textSize = _textSize;
    _view.tail = 0;
    [_win setContentSize:NSMakeSize(_w, _h)];
    [_view setNeedsDisplay:YES];
}

- (void)suspend
{
    if (_win && _shown) [_win orderOut:nil];
    _shown = NO;
}

- (void)hide
{
    _until = 0;
    [self suspend];
}

- (void)update:(POINT)anchor area:(RECT)area
{
    if (!_until) return;
    if (plat_ticks_ms() >= _until) {
        [self hide];
        return;
    }
    if (!_win || !_text) return;

    POINT pos = { anchor.x - _w / 2, anchor.y - _h };
    if (pos.x + _w > area.right) pos.x = area.right - _w;
    if (pos.x < area.left) pos.x = area.left;
    if (pos.y < area.top) pos.y = area.top;

    int tail = (anchor.x - pos.x) / _u;
    if (tail < 2) tail = 2;
    if (tail > _uw - 3) tail = _uw - 3;
    if (tail != _tail) {
        _tail = tail;
        _view.tail = tail;
        [_view setNeedsDisplay:YES];
    }

    NSRect want = mac_cocoa_rect(pos.x, pos.y, _w, _h);
    if (!NSEqualRects(_win.frame, want)) [_win setFrame:want display:NO];
    if (!_shown) {
        [_win orderFrontRegardless];
        _shown = YES;
    }
}

@end
