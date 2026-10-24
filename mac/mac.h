#pragma once

#import <Cocoa/Cocoa.h>
#include "pet.h"
#include "pomo.h"

/*
 * The macOS half of clawd-pet. The state machine and the pixel art in
 * src/pet.c and src/render.c are shared with the Windows build; everything
 * below is the Cocoa shell around them.
 */

// Windows the pet floats in, highest first: bubbles above pets, and pets
// above the Dock so they can stand on it.
#define PET_LEVEL  NSStatusWindowLevel
#define POP_LEVEL  (NSStatusWindowLevel + 1)
#define WALL_LEVEL (NSStatusWindowLevel - 1)   // below the pets, still above the Dock

/* ---- screen geometry ---------------------------------------------------
 *
 * The core works in top-left screen coordinates: x to the right, y down,
 * (0, 0) at the top-left corner of the union of every display. Cocoa uses
 * bottom-left coordinates, so everything crosses over here.
 */
void mac_screens_changed(void);                              // call when displays change
NSRect mac_cocoa_rect(double x, double y, double w, double h);
RECT mac_rect_from_cocoa(NSRect r);
NSScreen *mac_screen_at(double x, double y);
CGDirectDisplayID mac_display_of_screen(NSScreen *s);

/* ---- images ------------------------------------------------------------ */
CGImageRef mac_cgimage_take(uint32_t *px, int w, int h);      // premultiplied ARGB, takes ownership
NSImage *mac_nsimage(const uint32_t *px, int w, int h);       // ... or copies
NSColor *mac_color(uint32_t rgb);

/* ---- paths ------------------------------------------------------------- */
NSString *mac_support_dir(void);   // ~/Library/Application Support/ClawdPet, created

@class PetWindow;

/* ---- shared state ------------------------------------------------------ */
extern World g_world;
extern int g_stair_style;

/* ---- what the windows ask the app to do -------------------------------- */
void app_pet_clicked(void);                       // a pet was clicked
void app_show_menu(PetWindow *pw, NSEvent *event); // right-click menu
PetWindow *pet_window_pressed(void);              // the pet being dragged, or nil

/* ---- text bubbles ------------------------------------------------------ */
// A speech bubble with real text, in its own window above a pet. Click it
// to dismiss.
@interface TextBubble : NSObject
- (void)show:(NSString *)text ms:(int)ms;
- (void)hide;
- (void)suspend;                                  // hide, but keep the message
- (void)update:(POINT)anchor area:(RECT)area;
@end

/* ---- one Clawd --------------------------------------------------------- */
// A borderless panel showing the sprite, plus a click-through window of its
// own for the stairs it climbs.
@interface PetWindow : NSPanel
- (instancetype)initWithPet:(Pet)p;
- (Pet *)pet;
- (TextBubble *)bubble;
- (void)present;
- (void)presentStairs;
- (void)invalidate;                              // force a redraw and re-place
- (void)setHiddenByFullscreen:(BOOL)hidden;
@end

/* ---- walls ------------------------------------------------------------- */
// Brick walls the user draws (menu: 放置围墙). Each stays until it is
// right-clicked away; they are saved and handed to the pets.
void walls_load(void);
int  walls_count(void);
BOOL walls_drawing(void);
// Turns the screens into a canvas until one wall is drawn (drag), or Esc /
// right-click; then calls done. scale: logical pixel size for the bricks.
void walls_draw(int scale, void (^done)(void));
void walls_clear(void);
void walls_relayout(void);                          // the displays changed
void walls_hide_on(NSSet<NSNumber *> *displays);    // displays behind a fullscreen window

/* ---- pomodoro picker --------------------------------------------------- */
@interface Picker : NSObject
+ (void)openAt:(POINT)anchor area:(RECT)area focus:(int)focus break:(int)brk
          done:(void (^)(int focus, int brk))done;
+ (void)close;
@end
