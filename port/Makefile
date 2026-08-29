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

CORE = sar.o gfx.o map.o physics.o render.o png.o enemy.o ai.o combat.o shots.o sound.o \
       ai_eai1.o ai_eai2.o ai_eai3.o ai_eai4.o ai_eai5.o ai_eai6.o ai_eai7.o ai_eai8.o town.o \
       boss.o boss_crab.o boss_tako.o boss_tori.o boss_zela.o boss_meda.o boss_lega.o boss_generic.o \
       text.o player.o shop.o status.o shell.o nav.o playthrough.o opl2.o msd.o audio.o
ifneq ($(strip $(SDL_LIBS)),)
  CFLAGS += -DHAVE_SDL $(SDL_CFLAGS)
  LIBS   += $(SDL_LIBS)
endif
LIBS += -lm

all: zeliard test_physics test_combat test_town test_boss test_shop test_status test_playthrough test_audio

zeliard: main.o $(CORE)
	$(CC) -o $@ $^ $(LIBS)

test_physics: test_physics.o $(CORE)
	$(CC) -o $@ $^ $(LIBS)

test_combat: test_combat.o $(CORE)
	$(CC) -o $@ $^ $(LIBS)

test_town: test_town.o $(CORE)
	$(CC) -o $@ $^ $(LIBS)

test_boss: test_boss.o $(CORE)
	$(CC) -o $@ $^ $(LIBS)

test_shop: test_shop.o $(CORE)
	$(CC) -o $@ $^ $(LIBS)

test_status: test_status.o $(CORE)
	$(CC) -o $@ $^ $(LIBS)

test_playthrough: test_playthrough.o $(CORE)
	$(CC) -o $@ $^ $(LIBS)

test_audio: test_audio.o $(CORE)
	$(CC) -o $@ $^ $(LIBS)

%.o: %.c sar.h gfx.h map.h physics.h render.h png.h enemy.h town.h boss.h text.h player.h shop.h status.h shell.h nav.h playthrough.h opl2.h msd.h audio.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: test_physics test_combat test_town test_boss test_shop test_status test_playthrough test_audio
	./test_physics
	./test_combat
	./test_town
	./test_boss
	./test_shop
	./test_status
	./test_playthrough ../zeliard --quiet
	./test_audio ../zeliard

# the same two routes with the step-by-step log (issue #26's acceptance run)
playthrough: test_playthrough
	./test_playthrough ../zeliard

# start position vs docs/screenshots/cavern.png, and the cavern-1 frog sprite
# vs docs/screenshots/cavern_enemy.png (needs python3 + Pillow)
verify: zeliard test_combat test_town test_shop test_status
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
	# the town at the right end of Muralla (scroll_col 179, Garland on map column 209,
	# walk frame 3 facing right) vs docs/screenshots/town.png
	./zeliard --dir ../zeliard --town 1 --town-scr 179 --town-col 209 --town-anim 3 0 \
		--screenshot 1 /tmp/zel_town.png --frames 1
	python3 tools/compare_shot.py /tmp/zel_town.png ../docs/screenshots/town.png \
		--box 48 102 32 40 --label "town tiles, screen cols 0-3"
	python3 tools/compare_shot.py /tmp/zel_town.png ../docs/screenshots/town.png \
		--box 104 102 24 40 --label "town tiles, screen cols 7-9"
	python3 tools/compare_shot.py /tmp/zel_town.png ../docs/screenshots/town.png \
		--box 144 102 112 40 --label "town tiles, screen cols 12-25"
	python3 tools/compare_shot.py /tmp/zel_town.png ../docs/screenshots/town.png \
		--box 256 118 16 24 --label "town hero sprite (tman, frame 3)"
	# the Muralla weapon shop (door at column 39) with its greeting and main
	# menu up, vs docs/screenshots/shop_armour.png
	./zeliard --dir ../zeliard --town 1 --town-col 40 --headless --frames 400 \
		--script "U2 .200" --screenshot 40 /tmp/zel_shop.png
	python3 tools/compare_shot.py /tmp/zel_shop.png ../docs/screenshots/shop_armour.png \
		--box 56 23 96 52 --label "armour shop portrait"
	python3 tools/compare_shot.py /tmp/zel_shop.png ../docs/screenshots/shop_armour.png \
		--box 52 96 216 55 --label "shop text box (proportional font)"
	python3 tools/compare_shot.py /tmp/zel_shop.png ../docs/screenshots/shop_armour.png \
		--box 164 29 96 55 --label "shop menu widget + cursor"
	# the status / inventory screen (select.bin) opened with Enter in
	# Felishika's Castle on a fresh player, vs docs/screenshots/menu.png
	./zeliard --dir ../zeliard --town 0 --headless --frames 40 \
		--script "E3 .30" --screenshot 6 /tmp/zel_status.png
	python3 tools/compare_shot.py /tmp/zel_status.png ../docs/screenshots/menu.png \
		--box 48 14 224 144 --label "status screen (whole playfield)"
	python3 tools/compare_shot.py /tmp/zel_status.png ../docs/screenshots/menu.png \
		--box 180 63 92 94 --label "status INVENTORY window (sword picture + name)"

clean:
	rm -f *.o zeliard test_physics test_combat test_town test_boss test_shop test_status test_playthrough test_audio

.PHONY: all test verify playthrough clean
