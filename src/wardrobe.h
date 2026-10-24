#pragma once
#include <windows.h>
#include "render.h"

// Wardrobe: a pixel-art window above a pet with a big preview of it on the
// left and every accessory on the right, one row per slot. Click one to put
// it on (it replaces whatever was in that slot), click it again to take it
// off; buttons for a random outfit and for taking everything off. Every
// change goes straight to `changed`. Esc, the close box or clicking anywhere
// else closes it. Only one is open at a time.
typedef void (*WardrobeChanged)(int pet_id, const unsigned char *wear);

void wardrobe_register(HINSTANCE inst);
// anchor: screen point the tail should point at; area: rectangle to stay inside.
void wardrobe_open(POINT anchor, RECT area, UINT dpi, int pet_id, const unsigned char *wear,
                   WardrobeChanged changed);
int  wardrobe_pet(void);   // id of the pet being dressed, 0 if closed
void wardrobe_close(void);
