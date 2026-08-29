# Zeliard SDL2 port scaffold.  `make` builds port/zeliard (+ test_physics).
# SDL2 is optional: without pkg-config/sdl2-config the binary is headless-only
# (--screenshot/--script still work).  Override SDL_CFLAGS/SDL_LIBS by hand if
# the dev package lives somewhere unusual.
CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
SDL_CFLAGS ?= $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL_LIBS   ?= $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)

# machine-local overrides (SDL_CFLAGS / SDL_LIBS), not meant to be committed
-include local.mk

CORE = sar.o gfx.o map.o physics.o render.o png.o enemy.o ai.o ai_eai1.o combat.o
ifneq ($(strip $(SDL_LIBS)),)
  CFLAGS += -DHAVE_SDL $(SDL_CFLAGS)
  LIBS   += $(SDL_LIBS)
endif

all: zeliard test_physics test_combat

zeliard: main.o $(CORE)
	$(CC) -o $@ $^ $(LIBS)

test_physics: test_physics.o $(CORE)
	$(CC) -o $@ $^

test_combat: test_combat.o $(CORE)
	$(CC) -o $@ $^

%.o: %.c sar.h gfx.h map.h physics.h render.h png.h enemy.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: test_physics test_combat
	./test_physics
	./test_combat

# start position vs docs/screenshots/cavern.png, and the cavern-1 frog sprite
# vs docs/screenshots/cavern_enemy.png (needs python3 + Pillow)
verify: zeliard test_combat
	./zeliard --dir ../zeliard --screenshot 1 /tmp/zel_start.png
	python3 tools/compare_shot.py /tmp/zel_start.png ../docs/screenshots/cavern.png
	python3 tools/compare_shot.py /tmp/zel_start.png ../docs/screenshots/cavern.png \
		--box 84 163 100 6 --label "HUD LIFE bar"
	python3 tools/compare_shot.py /tmp/zel_start.png ../docs/screenshots/cavern.png \
		--box 76 187 36 8 --label "HUD GOLD digits"
	python3 tools/compare_shot.py /tmp/zel_start.png ../docs/screenshots/cavern.png \
		--box 152 187 30 8 --label "HUD ALMAS digits"
	./test_combat ../zeliard /tmp/zel_frog.png
	python3 tools/compare_shot.py /tmp/zel_frog.png ../docs/screenshots/cavern_enemy.png \
		--box 128 102 16 16 --label "frog sprite"
	python3 tools/compare_shot.py /tmp/zel_frog.png ../docs/screenshots/cavern_enemy.png \
		--box 192 14 16 8 --label "ceiling snail (lower half)"
	python3 tools/compare_shot.py /tmp/zel_frog.png ../docs/screenshots/cavern_enemy.png \
		--label "cavern_enemy playfield"

clean:
	rm -f *.o zeliard test_physics test_combat

.PHONY: all test verify clean
