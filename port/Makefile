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
       boss.o boss_crab.o boss_tako.o boss_tori.o boss_zela.o boss_meda.o boss_lega.o \
       boss_drgn.o boss_akma.o boss_mao1.o boss_mao2.o boss_generic.o \
       text.o player.o shop.o status.o shell.o nav.o playthrough.o opl2.o msd.o audio.o \
       gd.o cutscene.o tear.o
ifneq ($(strip $(SDL_LIBS)),)
  CFLAGS += -DHAVE_SDL $(SDL_CFLAGS)
  LIBS   += $(SDL_LIBS)
endif
LIBS += -lm

all: zeliard test_physics test_combat test_town test_boss test_shop test_status test_playthrough test_audio test_cutscene

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

test_cutscene: test_cutscene.o $(CORE)
	$(CC) -o $@ $^ $(LIBS)

%.o: %.c sar.h gfx.h map.h physics.h render.h png.h enemy.h town.h boss.h text.h player.h shop.h status.h shell.h nav.h playthrough.h opl2.h msd.h audio.h gd.h cutscene.h tear.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: test_physics test_combat test_town test_boss test_shop test_status test_playthrough test_audio test_cutscene
	./test_physics
	./test_combat
	./test_town
	./test_boss
	./test_shop
	./test_status
	./test_playthrough ../zeliard --quiet
	./test_audio ../zeliard
	./test_cutscene ../zeliard

# the same two routes with the step-by-step log (issue #26's acceptance run)
playthrough: test_playthrough
	./test_playthrough ../zeliard

# start position vs docs/screenshots/cavern.png, and the cavern-1 frog sprite
# vs docs/screenshots/cavern_enemy.png (needs python3 + Pillow)
verify: zeliard test_combat test_town test_shop test_status test_cutscene
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
	# cavern 2 (MP20 "Cavern of Peligro", MPP2) entered from Satono Town's right
	# edge, six columns past the door (README "Ground truth")
	./zeliard --dir ../zeliard --map 2 --pos 12 62 --level 12 --life 600 --sword 3 --shield 4 \
		--gold 20000 --headless --frames 3 --screenshot 1 /tmp/zel_cav2.png
	python3 tools/compare_shot.py /tmp/zel_cav2.png ../docs/screenshots/cavern2.png \
		--label "MP20 playfield (MPP2 tileset)"
	python3 tools/compare_shot.py /tmp/zel_cav2.png ../docs/screenshots/cavern2.png \
		--box 84 163 100 6 --label "MP20 HUD LIFE bar"
	python3 tools/compare_shot.py /tmp/zel_cav2.png ../docs/screenshots/cavern2.png \
		--box 76 187 36 8 --label "MP20 HUD GOLD digits (20000)"
	python3 tools/compare_shot.py /tmp/zel_cav2.png ../docs/screenshots/cavern2.png \
		--box 152 187 30 8 --label "MP20 HUD ALMAS digits"
	# cavern 3 (MP30 "Cavern of Madera", MPP3) at the BOSQUE door, with two
	# enp5 enemies on screen
	./zeliard --dir ../zeliard --map 5 --pos 185 19 --level 14 --life 700 --sword 3 --shield 4 \
		--gold 20000 --headless --frames 3 --screenshot 1 /tmp/zel_cav3.png
	python3 tools/compare_shot.py /tmp/zel_cav3.png ../docs/screenshots/cavern3.png \
		--label "MP30 playfield (MPP3 tileset + 2 enemies)"
	python3 tools/compare_shot.py /tmp/zel_cav3.png ../docs/screenshots/cavern3.png \
		--box 84 163 100 6 --label "MP30 HUD LIFE bar"
	python3 tools/compare_shot.py /tmp/zel_cav3.png ../docs/screenshots/cavern3.png \
		--box 76 187 36 8 --label "MP30 HUD GOLD digits (20000)"
	# the Cangrejo encounter (MP1D) — the boss renderer, its part buffer and the
	# boss-room tiles.  Only Cangrejo's left claw and Garland's own frame are at a
	# different animation phase than the capture, so they are outside the boxes.
	./zeliard --dir ../zeliard --map 1 --pos 57 15 --level 12 --life 600 --sword 3 --shield 4 \
		--headless --frames 22 --script ".60" --screenshot 20 /tmp/zel_boss.png
	python3 tools/compare_shot.py /tmp/zel_boss.png ../docs/screenshots/boss_cangrejo.png \
		--box 48 14 224 84 --label "MP1D boss room, above the sprites"
	python3 tools/compare_shot.py /tmp/zel_boss.png ../docs/screenshots/boss_cangrejo.png \
		--box 88 98 64 36 --label "Cangrejo body (CRAB parts)"
	python3 tools/compare_shot.py /tmp/zel_boss.png ../docs/screenshots/boss_cangrejo.png \
		--box 168 98 104 36 --label "MP1D boss room, right of the hero"
	python3 tools/compare_shot.py /tmp/zel_boss.png ../docs/screenshots/boss_cangrejo.png \
		--box 48 134 224 24 --label "MP1D boss room floor"
	# the HUD strip (y 158..199): the four [2004] gauge troughs, the [200E]
	# LIFE/PLACE/GOLD/ALMAS labels and the [2010] place name.  In a boss room
	# PLACE becomes ENEMY and the boss's own [A002]+9 name record takes the
	# GOLD box.
	python3 tools/compare_shot.py /tmp/zel_start.png ../docs/screenshots/cavern.png \
		--box 51 162 33 10 --label "HUD LIFE box + label"
	python3 tools/compare_shot.py /tmp/zel_start.png ../docs/screenshots/cavern.png \
		--box 51 174 136 10 --label "HUD PLACE box + \"Cavern of Malicia\""
	python3 tools/compare_shot.py /tmp/zel_start.png ../docs/screenshots/cavern.png \
		--box 51 186 66 10 --label "HUD GOLD box + label"
	python3 tools/compare_shot.py /tmp/zel_start.png ../docs/screenshots/cavern.png \
		--box 121 186 66 10 --label "HUD ALMAS box + label"
	python3 tools/compare_shot.py /tmp/zel_town.png ../docs/screenshots/town.png \
		--box 51 174 136 10 --label "town HUD PLACE box + \"Muralla Town\""
	python3 tools/compare_shot.py /tmp/zel_town.png ../docs/screenshots/town.png \
		--box 51 186 66 10 --label "town HUD GOLD box"
	python3 tools/compare_shot.py /tmp/zel_cav2.png ../docs/screenshots/cavern2.png \
		--box 51 174 136 10 --label "MP20 HUD PLACE box + \"Cavern of Peligro\""
	python3 tools/compare_shot.py /tmp/zel_cav3.png ../docs/screenshots/cavern3.png \
		--box 51 174 136 10 --label "MP30 HUD PLACE box + \"Cavern of Madera\""
	python3 tools/compare_shot.py /tmp/zel_boss.png ../docs/screenshots/boss_cangrejo.png \
		--box 51 175 33 8 --label "boss HUD ENEMY label"
	python3 tools/compare_shot.py /tmp/zel_boss.png ../docs/screenshots/boss_cangrejo.png \
		--box 51 186 66 10 --label "boss HUD name box + \"Cangrejo\""
	# Satono Town and Felishika's Castle, from the two captures the .usr
	# round-trip produced (README "Ground truth")
	./zeliard --dir ../zeliard --town 2 --town-col 5 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 2 --screenshot 1 /tmp/zel_satono.png
	python3 tools/compare_shot.py /tmp/zel_satono.png ../docs/screenshots/town_satono.png \
		--box 51 174 136 10 --label "Satono HUD PLACE box + \"Satono Town\""
	python3 tools/compare_shot.py /tmp/zel_satono.png ../docs/screenshots/town_satono.png \
		--box 51 186 66 10 --label "Satono HUD GOLD box (20000)"
	python3 tools/compare_shot.py /tmp/zel_satono.png ../docs/screenshots/town_satono.png \
		--box 84 163 100 6 --label "Satono HUD LIFE bar (600)"
	./zeliard --dir ../zeliard --town 0 --headless --frames 2 --screenshot 1 /tmp/zel_castle.png
	python3 tools/compare_shot.py /tmp/zel_castle.png ../docs/screenshots/restore_menu.png \
		--box 51 174 136 10 --label "castle HUD PLACE box + \"Felishika's Castle\""
	python3 tools/compare_shot.py /tmp/zel_castle.png ../docs/screenshots/restore_menu.png \
		--box 51 162 33 10 --label "castle HUD LIFE box"
	# mole.bin's boot-time screen furniture: the stone frame down both sides,
	# the strip above the playfield and the grey HUD panel with its item-slot
	# frames.  It is identical in all nine captures, so the whole 320x200
	# screen can be diffed where the port has the same state as the capture.
	python3 tools/compare_shot.py /tmp/zel_start.png ../docs/screenshots/cavern.png \
		--box 0 0 320 200 --label "MP10: the WHOLE screen, frame and all"
	python3 tools/compare_shot.py /tmp/zel_town.png ../docs/screenshots/town.png \
		--box 0 0 320 200 --label "Muralla: the WHOLE screen (ympd backdrop + parallax)"
	python3 tools/compare_shot.py /tmp/zel_status.png ../docs/screenshots/menu.png \
		--box 0 0 320 200 --label "status screen: the WHOLE screen"
	python3 tools/compare_shot.py /tmp/zel_castle.png ../docs/screenshots/restore_menu.png \
		--box 48 158 224 42 --label "castle HUD panel (mole.bin) + gauges"
	python3 tools/compare_shot.py /tmp/zel_boss.png ../docs/screenshots/boss_cangrejo.png \
		--box 0 0 48 200 --label "boss room: the left stone frame"
	python3 tools/compare_shot.py /tmp/zel_shop.png ../docs/screenshots/shop_armour.png \
		--box 272 0 48 200 --label "armour shop: the right stone frame"
	python3 tools/compare_shot.py /tmp/zel_cav2.png ../docs/screenshots/cavern2.png \
		--box 48 0 224 14 --label "MP20: the strip above the playfield"
	# ckpd.bin, the underground backdrop, and the three parallax strips
	python3 tools/compare_shot.py /tmp/zel_satono.png ../docs/screenshots/town_satono.png \
		--box 48 30 224 48 --label "Satono: the ckpd cave backdrop (y30..77)"
	python3 tools/compare_shot.py /tmp/zel_satono.png ../docs/screenshots/town_satono.png \
		--box 48 14 224 16 --label "Satono: the ckpd far parallax strip (y14..29)"
	python3 tools/compare_shot.py /tmp/zel_satono.png ../docs/screenshots/town_satono.png \
		--box 48 142 224 16 --label "Satono: the near ground strips (y142..157)"
	python3 tools/compare_shot.py /tmp/zel_town.png ../docs/screenshots/town.png \
		--box 48 142 224 16 --label "Muralla: the near strips at scroll_col 179 (8/16 px a step)"
	# --- the cutscenes (issue #30) -------------------------------------
	# opdemo's three acts through port/gd.c + port/cutscene.c, diffed over the
	# WHOLE 320x200 screen against the four DOSBox intro captures.  The frame
	# numbers are the port's own deterministic clock (4 ticks of the 236.7 Hz
	# [FF1A] counter per rendered frame), found once by scanning the act.
	./zeliard --dir ../zeliard --intro-act 1 --screenshot 1660 /tmp/zel_prologue.png
	python3 tools/compare_shot.py /tmp/zel_prologue.png ../docs/screenshots/intro_prologue.png \
		--box 0 0 320 200 --label "intro act 1: the prologue scroller over nec.grp, WHOLE screen"
	./zeliard --dir ../zeliard --intro-act 1 --screenshot 3521 /tmp/zel_demon.png
	python3 tools/compare_shot.py /tmp/zel_demon.png ../docs/screenshots/intro_demon.png \
		--box 0 0 320 200 --label "intro act 1: the demon + \"Beware, for \", WHOLE screen"
	./zeliard --dir ../zeliard --intro-act 1 --screenshot 5025 /tmp/zel_title.png
	python3 tools/compare_shot.py /tmp/zel_title.png ../docs/screenshots/title.png \
		--box 0 0 320 200 --label "intro act 1: the ZELIARD title screen, WHOLE screen"
	./zeliard --dir ../zeliard --intro-act 3 --screenshot 1044 /tmp/zel_balcony.png
	python3 tools/compare_shot.py /tmp/zel_balcony.png ../docs/screenshots/demo_balcony.png \
		--box 0 0 320 200 --label "storm demo: waku + ame + the narration, WHOLE screen"
	./zeliard --dir ../zeliard --intro-act 3 --screenshot 24272 /tmp/zel_dialogue.png
	python3 tools/compare_shot.py /tmp/zel_dialogue.png ../docs/screenshots/demo_dialogue.png \
		--box 0 0 320 200 --label "storm demo: the throne-room dialogue + lip-sync, WHOLE screen"
	# and the gd decoders themselves against tools/grp2png.py's, the reference
	# implementation that produced docs/screenshots/intro_art.png
	python3 tools/compare_gdart.py ../zeliard

clean:
	rm -f *.o zeliard test_physics test_combat test_town test_boss test_shop test_status test_playthrough test_audio test_cutscene

.PHONY: all test verify playthrough clean
