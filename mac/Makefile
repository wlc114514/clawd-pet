# Builds build/Clawd.app: the same pet, in Cocoa.
#
#   make            build the app
#   make run        build and launch it
#   make clean      remove build/
#
# The state machine and the pixel art are shared with the Windows build
# (../src), so only the Cocoa shell lives here.

APP      = build/Clawd.app
BIN      = $(APP)/Contents/MacOS/Clawd
SRC_C    = ../src/pet.c ../src/render.c
SRC_M    = app.m pet_window.m bubble.m picker.m plat_mac.m walls.m
HEADERS  = mac.h ../src/pet.h ../src/render.h ../src/platform.h ../src/pomo.h

CC       = clang
CFLAGS   = -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
           -fobjc-arc -I../src \
           -mmacosx-version-min=11.0
LDFLAGS  = -framework Cocoa -framework QuartzCore -framework CoreGraphics \
           -framework ApplicationServices

.PHONY: all run clean

all: $(BIN)

$(BIN): $(SRC_C) $(SRC_M) $(HEADERS) Info.plist
	@mkdir -p $(APP)/Contents/MacOS
	$(CC) $(CFLAGS) -o $(BIN) $(SRC_C) $(SRC_M) $(LDFLAGS)
	@cp Info.plist $(APP)/Contents/Info.plist
	@codesign --force --sign - $(APP) 2>/dev/null || true
	@echo "built $(APP)"

run: $(BIN)
	open $(APP)

clean:
	rm -rf build
