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
       gd.o cutscene.o tear.o \
       video.o video_mcga.o video_cga.o video_ega.o video_hgc.o video_tandy.o
ifneq ($(strip $(SDL_LIBS)),)
  CFLAGS += -DHAVE_SDL $(SDL_CFLAGS)
  LIBS   += $(SDL_LIBS)
endif
LIBS += -lm

# `make SAN=1 test` builds and links everything with AddressSanitizer and
# UndefinedBehaviorSanitizer.  The flags have to reach the *link* too, which is
# why this is a switch rather than something to pass in CFLAGS by hand.  -O1
# keeps the stack traces readable; the tests run 200k+ simulated frames, so the
# slowdown is worth measuring before wiring it into a tight loop.
ifeq ($(SAN),1)
  CFLAGS := $(filter-out -O2,$(CFLAGS)) -O1 -g -fno-omit-frame-pointer \
            -fsanitize=address,undefined -fno-sanitize-recover=all
  LIBS   += -fsanitize=address,undefined
endif

all: zeliard test_physics test_combat test_town test_boss test_shop test_status test_playthrough test_audio test_cutscene test_video

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

test_video: test_video.o $(CORE)
	$(CC) -o $@ $^ $(LIBS)

%.o: %.c sar.h gfx.h map.h physics.h render.h video.h png.h enemy.h town.h boss.h text.h player.h shop.h status.h shell.h nav.h playthrough.h opl2.h msd.h audio.h gd.h cutscene.h tear.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: test_physics test_combat test_town test_boss test_shop test_status test_playthrough test_audio test_cutscene test_video
	./test_physics
	./test_combat
	./test_town
	./test_boss
	./test_shop
	./test_status
	./test_playthrough ../zeliard --quiet
	./test_audio ../zeliard
	./test_cutscene ../zeliard
	./test_video ../zeliard

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
	# ... and MP20 thirty columns *in*: the first ground truth taken after a
	# scripted walk rather than at a map's entry frame.  Satono column 210 ->
	# hold Right off the right edge -> MP20 (6,62) -> hold Right ~7 s: Garland
	# falls into the column-16 pit and walks the rows 9-14 corridor east until
	# an enemy (the C010 record at (38,13)) stops him at (37,12).  This is the
	# only capture of MP20's *interior* -- a different tile band, a different
	# parallax offset (scroll 21,2) and the row wrap that puts the entry band
	# (rows 55-63) and the corridors (rows 0-14) in one strip.  See
	# port/README.md "Walking cavern 2 under DOSBox".
	./zeliard --dir ../zeliard --map 2 --pos 37 12 --level 40 --life 2000 --sword 6 \
		--shield 6 --gold 40000 --headless --frames 3 --screenshot 2 /tmp/zel_cav2c.png
	python3 tools/compare_shot.py /tmp/zel_cav2c.png ../docs/screenshots/cavern2_corridor.png \
		--box 48 14 96 144 --label "MP20 corridor: everything west of Garland"
	python3 tools/compare_shot.py /tmp/zel_cav2c.png ../docs/screenshots/cavern2_corridor.png \
		--box 48 14 224 78 --label "MP20 corridor: the ceiling band above him"
	python3 tools/compare_shot.py /tmp/zel_cav2c.png ../docs/screenshots/cavern2_corridor.png \
		--box 48 126 224 32 --label "MP20 corridor: the row-15 floor under him"
	python3 tools/compare_shot.py /tmp/zel_cav2c.png ../docs/screenshots/cavern2_corridor.png \
		--box 224 14 48 144 --label "MP20 corridor: the east end (columns 43-48)"
	python3 tools/compare_shot.py /tmp/zel_cav2c.png ../docs/screenshots/cavern2_corridor.png \
		--box 51 174 136 10 --label "MP20 corridor: HUD PLACE \"Cavern of Peligro\""
	python3 tools/compare_shot.py /tmp/zel_cav2c.png ../docs/screenshots/cavern2_corridor.png \
		--box 272 0 48 200 --label "MP20 corridor: the right stone frame"
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
	# Helada Town and cavern 4, the first ground truth for the second half of
	# the game (issue #28): a .usr the port wrote is restored under DOS with F7
	# (docs/DOSBOX_RECIPE.md §9) straight into Helada, and walking off its left
	# edge is the town's own cave record -> MP40 "Cavern of Glacial".
	./zeliard --dir ../zeliard --town 4 --town-col 86 --level 20 --life 900 --sword 4 \
		--shield 4 --gold 20000 --headless --frames 2 --screenshot 1 /tmp/zel_helada.png
	python3 tools/compare_shot.py /tmp/zel_helada.png ../docs/screenshots/town_helada.png \
		--box 48 14 224 40 --label "Helada: the ckpd far strip after an F7 restore (phase 0)"
	python3 tools/compare_shot.py /tmp/zel_helada.png ../docs/screenshots/town_helada.png \
		--box 48 30 224 56 --label "Helada: the ckpd cave backdrop"
	python3 tools/compare_shot.py /tmp/zel_helada.png ../docs/screenshots/town_helada.png \
		--box 48 142 224 16 --label "Helada: the near ground strips after a restore (phase 0)"
	python3 tools/compare_shot.py /tmp/zel_helada.png ../docs/screenshots/town_helada.png \
		--box 51 174 136 10 --label "Helada HUD PLACE box + \"Helada Town\""
	python3 tools/compare_shot.py /tmp/zel_helada.png ../docs/screenshots/town_helada.png \
		--box 51 186 66 10 --label "Helada HUD GOLD box (20000)"
	python3 tools/compare_shot.py /tmp/zel_helada.png ../docs/screenshots/town_helada.png \
		--box 84 163 100 6 --label "Helada HUD LIFE bar (900)"
	./zeliard --dir ../zeliard --town 4 --town-col 4 --level 20 --life 900 --sword 4 \
		--shield 4 --gold 20000 --headless --frames 120 --script "L20 .90" \
		--screenshot 60 /tmp/zel_cav4.png
	python3 tools/compare_shot.py /tmp/zel_cav4.png ../docs/screenshots/cavern4.png \
		--box 48 14 224 80 --label "MP40 playfield above the hero (MPP5 ice tileset)"
	python3 tools/compare_shot.py /tmp/zel_cav4.png ../docs/screenshots/cavern4.png \
		--box 48 134 224 24 --label "MP40 floor"
	python3 tools/compare_shot.py /tmp/zel_cav4.png ../docs/screenshots/cavern4.png \
		--box 51 174 136 10 --label "MP40 HUD PLACE box + \"Cavern of Glacial\""
	python3 tools/compare_shot.py /tmp/zel_cav4.png ../docs/screenshots/cavern4.png \
		--box 51 186 66 10 --label "MP40 HUD GOLD box (20000)"
	python3 tools/compare_shot.py /tmp/zel_cav4.png ../docs/screenshots/cavern4.png \
		--box 0 0 48 200 --label "MP40: the left stone frame"
	# cavern 5 (MP50 "Cavern of Peligro"... the HUD calls it by cavern 5's name)
	# from Tumba Town's left edge, and Tumba itself
	./zeliard --dir ../zeliard --town 5 --town-col 4 --level 24 --life 1100 --sword 5 \
		--shield 5 --gold 30000 --headless --frames 2 --screenshot 1 /tmp/zel_tumba.png
	python3 tools/compare_shot.py /tmp/zel_tumba.png ../docs/screenshots/town_tumba.png \
		--box 48 14 224 40 --label "Tumba: the backdrop after an F7 restore"
	python3 tools/compare_shot.py /tmp/zel_tumba.png ../docs/screenshots/town_tumba.png \
		--box 48 142 224 16 --label "Tumba: the near ground strips (phase 0)"
	python3 tools/compare_shot.py /tmp/zel_tumba.png ../docs/screenshots/town_tumba.png \
		--box 51 174 136 10 --label "Tumba HUD PLACE box + \"Tumba Town\""
	./zeliard --dir ../zeliard --town 5 --town-col 4 --level 24 --life 1100 --sword 5 \
		--shield 5 --gold 30000 --headless --frames 130 --script "L20 .100" \
		--screenshot 60 /tmp/zel_cav5.png
	python3 tools/compare_shot.py /tmp/zel_cav5.png ../docs/screenshots/cavern5.png \
		--box 48 14 224 80 --label "MP50 playfield above the hero"
	python3 tools/compare_shot.py /tmp/zel_cav5.png ../docs/screenshots/cavern5.png \
		--box 48 134 224 24 --label "MP50 floor"
	python3 tools/compare_shot.py /tmp/zel_cav5.png ../docs/screenshots/cavern5.png \
		--box 51 174 136 10 --label "MP50 HUD PLACE box"
	python3 tools/compare_shot.py /tmp/zel_cav5.png ../docs/screenshots/cavern5.png \
		--box 51 186 66 10 --label "MP50 HUD GOLD box (30000)"
	# cavern 6 (MP60 "Cavern of Tesoro") at Dorado Town's own cave record
	# (315,49), where walking off Dorado's right edge lands
	./zeliard --dir ../zeliard --map 0x0e --pos 315 49 --level 28 --life 1300 --sword 5 \
		--shield 5 --gold 40000 --headless --frames 3 --screenshot 2 /tmp/zel_cav6.png
	python3 tools/compare_shot.py /tmp/zel_cav6.png ../docs/screenshots/cavern6.png \
		--box 56 14 26 120 --label "MP60 (MPP7 brick tileset), left of the first chain"
	python3 tools/compare_shot.py /tmp/zel_cav6.png ../docs/screenshots/cavern6.png \
		--box 106 14 50 78 --label "MP60 above the DORADO door"
	python3 tools/compare_shot.py /tmp/zel_cav6.png ../docs/screenshots/cavern6.png \
		--box 170 14 32 120 --label "MP60 right of the door"
	python3 tools/compare_shot.py /tmp/zel_cav6.png ../docs/screenshots/cavern6.png \
		--box 51 174 136 10 --label "MP60 HUD PLACE box + \"Cavern of Tesoro\""
	python3 tools/compare_shot.py /tmp/zel_cav6.png ../docs/screenshots/cavern6.png \
		--box 272 0 48 200 --label "MP60: the right stone frame"
	# Pureza Town and cavern 8 (MP80), the deepest ground truth so far: the
	# same restore, then off Pureza's left edge into the town's cave record
	./zeliard --dir ../zeliard --town 8 --town-col 4 --level 34 --life 1800 --sword 6 \
		--shield 6 --gold 60000 --headless --frames 2 --screenshot 1 /tmp/zel_pureza.png
	python3 tools/compare_shot.py /tmp/zel_pureza.png ../docs/screenshots/town_pureza.png \
		--box 48 14 224 40 --label "Pureza: the backdrop after an F7 restore"
	python3 tools/compare_shot.py /tmp/zel_pureza.png ../docs/screenshots/town_pureza.png \
		--box 48 142 224 16 --label "Pureza: the near ground strips (phase 0)"
	python3 tools/compare_shot.py /tmp/zel_pureza.png ../docs/screenshots/town_pureza.png \
		--box 51 174 136 10 --label "Pureza HUD PLACE box + \"Pureza Town\""
	python3 tools/compare_shot.py /tmp/zel_pureza.png ../docs/screenshots/town_pureza.png \
		--box 84 163 100 6 --label "Pureza HUD LIFE bar (1800)"
	./zeliard --dir ../zeliard --map 0x17 --pos 111 21 --level 34 --life 1800 --sword 6 \
		--shield 6 --gold 60000 --headless --frames 3 --screenshot 2 /tmp/zel_cav8.png
	python3 tools/compare_shot.py /tmp/zel_cav8.png ../docs/screenshots/cavern8.png \
		--box 48 14 40 80 --label "MP80 playfield, left of the hanging sprite"
	python3 tools/compare_shot.py /tmp/zel_cav8.png ../docs/screenshots/cavern8.png \
		--box 106 14 166 80 --label "MP80 playfield, right of it"
	python3 tools/compare_shot.py /tmp/zel_cav8.png ../docs/screenshots/cavern8.png \
		--box 48 134 224 24 --label "MP80 floor"
	python3 tools/compare_shot.py /tmp/zel_cav8.png ../docs/screenshots/cavern8.png \
		--box 51 174 136 10 --label "MP80 HUD PLACE box"
	python3 tools/compare_shot.py /tmp/zel_cav8.png ../docs/screenshots/cavern8.png \
		--box 51 186 66 10 --label "MP80 HUD GOLD box (60000)"
	# Llama Town -- the whole 320x200 screen, restored straight in front of the
	# town door at column 222 -- and the room behind that door: MP73, cavern 7's
	# boss "Paguro", the first capture of a boss the port had never been checked
	# against.
	./zeliard --dir ../zeliard --town 7 --town-col 222 --level 34 --life 1800 --sword 6 \
		--shield 6 --gold 60000 --headless --frames 2 --screenshot 1 /tmp/zel_llama.png
	python3 tools/compare_shot.py /tmp/zel_llama.png ../docs/screenshots/town_llama.png \
		--label "Llama Town: the whole screen after an F7 restore (hero masked)"
	python3 tools/compare_shot.py /tmp/zel_llama.png ../docs/screenshots/town_llama.png \
		--box 48 14 224 40 --label "Llama: the backdrop and far strip"
	python3 tools/compare_shot.py /tmp/zel_llama.png ../docs/screenshots/town_llama.png \
		--box 48 142 224 16 --label "Llama: the near ground strips (phase 0)"
	python3 tools/compare_shot.py /tmp/zel_llama.png ../docs/screenshots/town_llama.png \
		--box 51 174 136 10 --label "Llama HUD PLACE box + \"Llama Town\""
	# MP73 is now entered the way the capture was made: standing in front of
	# Llama Town's column-222 door and pressing Up.  Llama's cave record for the
	# room is (27,13), but a cave record's row is relative to the screen row of
	# 10 that town.bin 7005 hard-codes, while fight.bin 7D2D pins the hero's
	# screen row at the map's own [C016] row_bias -- 12 in every boss room -- so
	# the hero lands at 13 - 10 + 12 = 15, standing on the floor.  Reading the
	# record's row as the map row (what the port used to do) dropped him two rows
	# and 69CB gave him an extra step in his facing on the way down, which is the
	# one-column drift issue #28 could not explain; see port/README.md.
	./zeliard --dir ../zeliard --town 7 --town-col 222 --level 34 --life 1800 --sword 6 \
		--shield 6 --gold 60000 --headless --frames 60 --script "U2 .50" \
		--screenshot 40 /tmp/zel_paguro.png
	python3 tools/compare_shot.py /tmp/zel_paguro.png ../docs/screenshots/boss_paguro.png \
		--box 48 14 108 120 --label "MP73 boss room, left of Paguro (from the wall)"
	python3 tools/compare_shot.py /tmp/zel_paguro.png ../docs/screenshots/boss_paguro.png \
		--box 48 14 16 186 --label "MP73's left wall, corner and all (screen x 48-63)"
	python3 tools/compare_shot.py /tmp/zel_paguro.png ../docs/screenshots/boss_paguro.png \
		--box 190 14 82 120 --label "MP73 boss room, right of him"
	python3 tools/compare_shot.py /tmp/zel_paguro.png ../docs/screenshots/boss_paguro.png \
		--box 48 130 224 28 --label "MP73 boss room floor, wall to wall"
	python3 tools/compare_shot.py /tmp/zel_paguro.png ../docs/screenshots/boss_paguro.png \
		--box 51 186 66 10 --label "MP73 HUD name box + \"Paguro\""
	python3 tools/compare_shot.py /tmp/zel_paguro.png ../docs/screenshots/boss_paguro.png \
		--box 272 0 48 200 --label "MP73: the right stone frame"
	# cavern 7 (MP70 "Cavern of Caliente", MPP7 lava) through Llama Town's
	# column-8 door, and cavern 8's second map (MP81 "Cavern of Milagro")
	# through Esco village's column-205 door -- the last two maps a town door
	# reaches (docs/DOSBOX_RECIPE.md 9.1).  The sparkles in MP70's sky and the
	# three hanging enemies at the top of MP81 are at a different animation
	# phase than the capture, so they are outside the boxes.
	./zeliard --dir ../zeliard --map 0x12 --pos 152 7 --level 34 --life 1800 --sword 6 \
		--shield 6 --gold 60000 --headless --frames 4 --screenshot 2 /tmp/zel_cav7.png
	python3 tools/compare_shot.py /tmp/zel_cav7.png ../docs/screenshots/cavern7.png \
		--box 48 14 224 78 --label "MP70 playfield above the sparkles (MPP7 lava tileset)"
	python3 tools/compare_shot.py /tmp/zel_cav7.png ../docs/screenshots/cavern7.png \
		--box 48 120 224 38 --label "MP70 floor and the LLAMA door"
	python3 tools/compare_shot.py /tmp/zel_cav7.png ../docs/screenshots/cavern7.png \
		--box 51 174 136 10 --label "MP70 HUD PLACE box + \"Cavern of Caliente\""
	python3 tools/compare_shot.py /tmp/zel_cav7.png ../docs/screenshots/cavern7.png \
		--box 51 186 66 10 --label "MP70 HUD GOLD box (60000)"
	python3 tools/compare_shot.py /tmp/zel_cav7.png ../docs/screenshots/cavern7.png \
		--box 84 163 100 6 --label "MP70 HUD LIFE bar (1800)"
	python3 tools/compare_shot.py /tmp/zel_cav7.png ../docs/screenshots/cavern7.png \
		--box 0 0 48 200 --label "MP70: the left stone frame"
	./zeliard --dir ../zeliard --map 0x18 --pos 123 6 --level 38 --life 2000 --sword 6 \
		--shield 6 --gold 70000 --headless --frames 4 --screenshot 2 /tmp/zel_cav8b.png
	python3 tools/compare_shot.py /tmp/zel_cav8b.png ../docs/screenshots/cavern8b.png \
		--box 48 14 224 8 --label "MP81 ceiling, above the hanging enemies"
	python3 tools/compare_shot.py /tmp/zel_cav8b.png ../docs/screenshots/cavern8b.png \
		--box 48 38 224 56 --label "MP81 playfield (MPP8), below them"
	python3 tools/compare_shot.py /tmp/zel_cav8b.png ../docs/screenshots/cavern8b.png \
		--box 48 118 224 40 --label "MP81 floor and the ESCO door"
	python3 tools/compare_shot.py /tmp/zel_cav8b.png ../docs/screenshots/cavern8b.png \
		--box 51 174 136 10 --label "MP81 HUD PLACE box + \"Cavern of Milagro\""
	# --- the shopkeepers' [A002] idle hooks (issue #28) -----------------
	# Every overlay but omoypro hangs a portrait animation off town.bin's
	# idle_poll, clocked by [FF50].  Each shop below was captured under DOSBox
	# through an F7 restore that puts Garland on the door's own column
	# (docs/DOSBOX_RECIPE.md 9.3), as a burst of ~40 shots 0.1 s apart, and
	# every distinct frame of every cycle is diffed against the port here.
	# The armour shop: shop_armour.png caught the smith mid-blink (the eye
	# cells at 128,79 are AAD0's fourth pair, NOT the ones the portrait map
	# has there) and shop_armour2.png is the same screen 0.3 s later with his
	# eyes open.  Measured period 1.00-1.01 s over five blinks; A90F predicts
	# 4 phases x 0x1E steps x 2 ticks = 240 ticks = 1.014 s.
	./zeliard --dir ../zeliard --town 1 --town-col 40 --headless --frames 400 \
		--script "U2 .200" --screenshot 34 /tmp/zel_shop_blink.png
	python3 tools/compare_shot.py /tmp/zel_shop_blink.png ../docs/screenshots/shop_armour.png \
		--box 128 79 16 8 --label "armrpro A90F: the smith mid-blink (AAD0 pair 3)"
	python3 tools/compare_shot.py /tmp/zel_shop_blink.png ../docs/screenshots/shop_armour.png \
		--box 48 14 224 144 --label "armour shop: the WHOLE playfield, mid-blink"
	python3 tools/compare_shot.py /tmp/zel_shop.png ../docs/screenshots/shop_armour2.png \
		--box 128 79 16 8 --label "armrpro A90F: the smith's eyes open (AAD0 pairs 0-2)"
	python3 tools/compare_shot.py /tmp/zel_shop.png ../docs/screenshots/shop_armour2.png \
		--box 48 14 224 144 --label "armour shop: the WHOLE playfield, eyes open"
	# churpro A1D7: the only hook with no enable flag — two little maps
	# (A234 at 128,55 and A27C at 168,55) step 0 -> 1 -> 2 every 0x20 ticks,
	# a 96-tick = 0.406 s cycle; measured 0.40-0.41 s.  Muralla's church door
	# is column 59.
	./zeliard --dir ../zeliard --town 1 --town-col 59 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 30 /tmp/zel_church0.png
	./zeliard --dir ../zeliard --town 1 --town-col 59 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 32 /tmp/zel_church1.png
	./zeliard --dir ../zeliard --town 1 --town-col 59 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 33 /tmp/zel_church2.png
	python3 tools/compare_shot.py /tmp/zel_church0.png ../docs/screenshots/shop_church.png \
		--box 128 55 24 16 --label "churpro A1FA phase 0 (the altar candles)"
	python3 tools/compare_shot.py /tmp/zel_church0.png ../docs/screenshots/shop_church.png \
		--box 168 55 16 16 --label "churpro A246 phase 0 (the priest)"
	python3 tools/compare_shot.py /tmp/zel_church1.png ../docs/screenshots/shop_church2.png \
		--box 128 55 24 16 --label "churpro A1FA phase 1"
	python3 tools/compare_shot.py /tmp/zel_church1.png ../docs/screenshots/shop_church2.png \
		--box 168 55 16 16 --label "churpro A246 phase 1"
	python3 tools/compare_shot.py /tmp/zel_church2.png ../docs/screenshots/shop_church3.png \
		--box 128 55 24 16 --label "churpro A1FA phase 2"
	python3 tools/compare_shot.py /tmp/zel_church2.png ../docs/screenshots/shop_church3.png \
		--box 168 55 16 16 --label "churpro A246 phase 2"
	# drugpro A644: the bubbling pot, 6x6 cells at (104,23), three phases at
	# 0x14 hook steps of 2 ticks = 40 ticks each (0.507 s a cycle; measured
	# 0.50 s).  Phase 0 is the portrait's own cells, which is why the map at
	# A5E4 has A69C's first row in it.  Muralla's drug door is column 111.
	./zeliard --dir ../zeliard --town 1 --town-col 111 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 24 /tmp/zel_drug0.png
	./zeliard --dir ../zeliard --town 1 --town-col 111 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 25 /tmp/zel_drug1.png
	./zeliard --dir ../zeliard --town 1 --town-col 111 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 27 /tmp/zel_drug2.png
	python3 tools/compare_shot.py /tmp/zel_drug0.png ../docs/screenshots/shop_drug.png \
		--box 48 14 224 144 --label "drug shop phase 0: the WHOLE playfield"
	python3 tools/compare_shot.py /tmp/zel_drug1.png ../docs/screenshots/shop_drug2.png \
		--box 48 14 224 144 --label "drug shop phase 1: the WHOLE playfield"
	python3 tools/compare_shot.py /tmp/zel_drug2.png ../docs/screenshots/shop_drug3.png \
		--box 48 14 224 144 --label "drug shop phase 2: the WHOLE playfield"
	python3 tools/compare_shot.py /tmp/zel_drug0.png ../docs/screenshots/shop_drug.png \
		--box 104 23 48 48 --label "drugpro A644 phase 0 (the pot)"
	python3 tools/compare_shot.py /tmp/zel_drug1.png ../docs/screenshots/shop_drug2.png \
		--box 104 23 48 48 --label "drugpro A644 phase 1"
	python3 tools/compare_shot.py /tmp/zel_drug2.png ../docs/screenshots/shop_drug3.png \
		--box 104 23 48 48 --label "drugpro A644 phase 2"
	# bankpro A728: the teller's idle runs only for the five 0x3F-tick passes
	# of the entry (A063..A08F) and after a 1000-gold deposit; his two cell
	# maps are 0x28 bytes apart at [AD1F] and swap every 0x1E ticks.  Then op 0
	# (A0C0) plays the four-map A82F "stands up" list at 0x28 ticks a frame.
	# Muralla's bank door is column 138.
	./zeliard --dir ../zeliard --town 1 --town-col 138 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 20 /tmp/zel_bank0.png
	./zeliard --dir ../zeliard --town 1 --town-col 138 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 21 /tmp/zel_bank1.png
	./zeliard --dir ../zeliard --town 1 --town-col 138 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 32 /tmp/zel_bank2.png
	./zeliard --dir ../zeliard --town 1 --town-col 138 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 36 /tmp/zel_bank3.png
	python3 tools/compare_shot.py /tmp/zel_bank0.png ../docs/screenshots/shop_bank.png \
		--box 72 31 64 40 --label "bankpro A728 idle, map A773 phase 0"
	python3 tools/compare_shot.py /tmp/zel_bank1.png ../docs/screenshots/shop_bank2.png \
		--box 72 31 64 40 --label "bankpro A728 idle, map A773 phase 1"
	python3 tools/compare_shot.py /tmp/zel_bank2.png ../docs/screenshots/shop_bank3.png \
		--box 72 31 64 40 --label "bankpro A813: the third frame of the A82F stand-up"
	python3 tools/compare_shot.py /tmp/zel_bank3.png ../docs/screenshots/shop_bank4.png \
		--box 72 31 64 40 --label "bankpro A813: the teller settled, idle off"
	# innapro A22F: the one place the town uses KRN_RANDOM — a fresh coin flip
	# every 0x28 ticks picks the innkeeper's eyes from A279's two 2x2 maps.
	# The port's RNG is not the original's, so the *sequence* cannot match;
	# both frames do, pixel for pixel.  Satono's inn door is column 128.
	./zeliard --dir ../zeliard --town 2 --town-col 128 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 23 /tmp/zel_inn0.png
	./zeliard --dir ../zeliard --town 2 --town-col 128 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 25 /tmp/zel_inn1.png
	python3 tools/compare_shot.py /tmp/zel_inn0.png ../docs/screenshots/shop_inn.png \
		--box 64 39 16 16 --label "innapro A22F: the innkeeper's eyes open"
	python3 tools/compare_shot.py /tmp/zel_inn0.png ../docs/screenshots/shop_inn.png \
		--box 48 14 224 144 --label "inn: the WHOLE playfield"
	python3 tools/compare_shot.py /tmp/zel_inn1.png ../docs/screenshots/shop_inn2.png \
		--box 64 39 16 16 --label "innapro A22F: the innkeeper's eyes shut"
	# kenjpro AB47: the sage's blink, one cell at [BB12]+0x718 = (112,47),
	# toggled every 0x14 steps of 2 ticks.  Muralla's sage door is column 172.
	./zeliard --dir ../zeliard --town 1 --town-col 172 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 23 /tmp/zel_sage0.png
	./zeliard --dir ../zeliard --town 1 --town-col 172 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 25 /tmp/zel_sage1.png
	python3 tools/compare_shot.py /tmp/zel_sage0.png ../docs/screenshots/shop_sage.png \
		--box 112 47 8 8 --label "kenjpro AB47: the sage's eye, ABFB[0]"
	python3 tools/compare_shot.py /tmp/zel_sage1.png ../docs/screenshots/shop_sage2.png \
		--box 112 47 8 8 --label "kenjpro AB47: the sage's eye, ABFB[1]"
	# kingpro A302: the blink (A360's 26-step sequence into A37A's three eye
	# maps at 136,47) and the lip-sync mouth (A3D4's two 2x5 maps at 136,63,
	# toggled every 6 steps of 4 ticks = 0.101 s; measured 0.10 s).  The king's
	# door is column 52 of Felishika's Castle.
	./zeliard --dir ../zeliard --town 0 --town-col 52 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 27 /tmp/zel_king0.png
	./zeliard --dir ../zeliard --town 0 --town-col 52 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 28 /tmp/zel_king1.png
	./zeliard --dir ../zeliard --town 0 --town-col 52 --level 12 --life 600 --sword 3 \
		--shield 4 --gold 20000 --headless --frames 200 --script "U2 .200" \
		--screenshot 31 /tmp/zel_king2.png
	python3 tools/compare_shot.py /tmp/zel_king0.png ../docs/screenshots/shop_king.png \
		--box 136 63 40 16 --label "kingpro A3A6: the mouth shut"
	python3 tools/compare_shot.py /tmp/zel_king1.png ../docs/screenshots/shop_king2.png \
		--box 136 63 40 16 --label "kingpro A3A6: the mouth open"
	# (the blink and the mouth are independent counters, so the port's frame 27
	# carries the capture's mouth-shut with the *next* eye map and frame 28 the
	# other way round — the two boxes are crossed on purpose)
	python3 tools/compare_shot.py /tmp/zel_king1.png ../docs/screenshots/shop_king.png \
		--box 136 47 32 8 --label "kingpro A338: the eyes, A37A map 0"
	python3 tools/compare_shot.py /tmp/zel_king0.png ../docs/screenshots/shop_king2.png \
		--box 136 47 32 8 --label "kingpro A338: the eyes, A37A map 1"
	python3 tools/compare_shot.py /tmp/zel_king2.png ../docs/screenshots/shop_king3.png \
		--box 136 47 32 8 --label "kingpro A338: the eyes, A37A map 2"
	python3 tools/compare_shot.py /tmp/zel_cav8b.png ../docs/screenshots/cavern8b.png \
		--box 51 186 66 10 --label "MP81 HUD GOLD box (70000)"
	python3 tools/compare_shot.py /tmp/zel_cav8b.png ../docs/screenshots/cavern8b.png \
		--box 121 186 66 10 --label "MP81 HUD ALMAS box"
	python3 tools/compare_shot.py /tmp/zel_cav8b.png ../docs/screenshots/cavern8b.png \
		--box 272 0 48 200 --label "MP81: the right stone frame"
	# --- the other four video drivers (issue #32) -----------------------
	# The engine renders one 320x200 buffer of PC-88 colour *pairs* (l*8+r) --
	# byte for byte what GMMCGA.BIN puts in A000 -- and port/video_*.c is the
	# stage that turns a pair into its own driver's pixels: the @290B / @2994 /
	# @2999 tables for CGA / Hercules / Tandy, and nothing at all for EGA, whose
	# [0x2044] is a `ret` and whose two halves stay two 640-wide pixels.
	#
	# Ground truth is the same MP10 entry frame as `cavern.png`, captured with
	# RESOURCE.CFG's videoDrv set to each driver:
	#   KEYS="<docs/DOSBOX_RECIPE.md recipe B>" \
	#     port/tools/capture_video_mode.sh MODE /tmp/z 56
	# at native resolution (320x200, 640x200 and 720x348), so no capture is
	# rescaled into or out of a comparison.
	#
	# The **whole playfield** is pixel-exact in every mode.  What is left over
	# is mole.bin's screen furniture and the HUD's labels and place name, which
	# every driver draws with its own colour constants rather than from a pair
	# (port/README.md "What the pair buffer cannot reach").  The parts of the
	# HUD that *are* a pair -- the LIFE bar and the two digit runs -- are in.
	./zeliard --dir ../zeliard --video cga --screenshot 1 /tmp/zel_cga.png
	python3 tools/compare_shot.py /tmp/zel_cga.png ../docs/screenshots/cavern_cga.png \
		--box 48 14 224 144 --label "CGA mode 5: the WHOLE MP10 playfield"
	python3 tools/compare_shot.py /tmp/zel_cga.png ../docs/screenshots/cavern_cga.png \
		--box 84 163 100 6 --label "CGA: HUD LIFE bar"
	python3 tools/compare_shot.py /tmp/zel_cga.png ../docs/screenshots/cavern_cga.png \
		--box 76 187 36 8 --label "CGA: HUD GOLD digits"
	python3 tools/compare_shot.py /tmp/zel_cga.png ../docs/screenshots/cavern_cga.png \
		--box 152 187 30 8 --label "CGA: HUD ALMAS digits"
	# cga2 is the same GMCGA.BIN output shown in mode 6: every 2-bit pixel
	# becomes its own two mono pixels, so the screen is 640 wide
	./zeliard --dir ../zeliard --video cga2 --screenshot 1 /tmp/zel_cga2.png
	python3 tools/compare_shot.py /tmp/zel_cga2.png ../docs/screenshots/cavern_cga2.png \
		--box 96 14 448 144 --label "CGA mode 6: the WHOLE MP10 playfield (2-px dither)"
	python3 tools/compare_shot.py /tmp/zel_cga2.png ../docs/screenshots/cavern_cga2.png \
		--box 168 163 200 6 --label "cga2: HUD LIFE bar"
	python3 tools/compare_shot.py /tmp/zel_cga2.png ../docs/screenshots/cavern_cga2.png \
		--box 152 187 72 8 --label "cga2: HUD GOLD digits"
	./zeliard --dir ../zeliard --video tandy --screenshot 1 /tmp/zel_tandy.png
	python3 tools/compare_shot.py /tmp/zel_tandy.png ../docs/screenshots/cavern_tandy.png \
		--box 48 14 224 144 --label "Tandy mode 9: the WHOLE MP10 playfield"
	python3 tools/compare_shot.py /tmp/zel_tandy.png ../docs/screenshots/cavern_tandy.png \
		--box 76 187 36 8 --label "Tandy: HUD GOLD digits"
	python3 tools/compare_shot.py /tmp/zel_tandy.png ../docs/screenshots/cavern_tandy.png \
		--box 152 187 30 8 --label "Tandy: HUD ALMAS digits"
	# EGA is 640x200 and keeps both halves of every pair.  Only the hero is
	# outside the boxes: gfega outlines a sprite in 640ths where gfmcga can only
	# do it in 320ths, so twelve pixels of Garland's edge have no pair to come
	# from (port/README.md).
	./zeliard --dir ../zeliard --video ega --screenshot 1 /tmp/zel_ega.png
	python3 tools/compare_shot.py /tmp/zel_ega.png ../docs/screenshots/cavern_ega.png \
		--box 96 14 448 80 --label "EGA: MP10 playfield above the hero"
	python3 tools/compare_shot.py /tmp/zel_ega.png ../docs/screenshots/cavern_ega.png \
		--box 96 118 448 40 --label "EGA: MP10 playfield below him"
	python3 tools/compare_shot.py /tmp/zel_ega.png ../docs/screenshots/cavern_ega.png \
		--box 96 94 192 24 --label "EGA: the band left of him"
	python3 tools/compare_shot.py /tmp/zel_ega.png ../docs/screenshots/cavern_ega.png \
		--box 336 94 208 24 --label "EGA: and right of him"
	python3 tools/compare_shot.py /tmp/zel_ega.png ../docs/screenshots/cavern_ega.png \
		--box 96 0 448 14 --label "EGA: the strip above the playfield (mole.bin)"
	python3 tools/compare_shot.py /tmp/zel_ega.png ../docs/screenshots/cavern_ega.png \
		--box 168 163 200 6 --label "EGA: HUD LIFE bar"
	python3 tools/compare_shot.py /tmp/zel_ega.png ../docs/screenshots/cavern_ega.png \
		--box 152 187 72 8 --label "EGA: HUD GOLD digits"
	# Hercules is 720x348: 40 px of left margin, and the row helper @2E11 puts
	# game row y on line 4*((y+28)/3) + (y+28)%3, so rows 14..157 are lines
	# 56..247 and x 48..271 is 136..583.
	./zeliard --dir ../zeliard --video hgc --screenshot 1 /tmp/zel_hgc.png
	python3 tools/compare_shot.py /tmp/zel_hgc.png ../docs/screenshots/cavern_hgc.png \
		--box 136 56 448 192 --label "HGC: the WHOLE MP10 playfield (3 rows over 4 lines)"
	python3 tools/compare_shot.py /tmp/zel_hgc.png ../docs/screenshots/cavern_hgc.png \
		--box 208 254 200 8 --label "HGC: HUD LIFE bar"
	python3 tools/compare_shot.py /tmp/zel_hgc.png ../docs/screenshots/cavern_hgc.png \
		--box 192 286 72 11 --label "HGC: HUD GOLD digits"
	python3 tools/compare_shot.py /tmp/zel_hgc.png ../docs/screenshots/cavern_hgc.png \
		--box 344 286 60 11 --label "HGC: HUD ALMAS digits"
	# ... and the same six modes in the *town*: Muralla at the map's right edge
	# (scroll 179, Garland on column 209, walk frame 3), the state `town.png`
	# already pins, captured with the same recipe minus the door taps
	# (`KEYS="6:Return 9:Return 16:Return 20:+Right 48:-Right"`, shot at 56 s).
	#
	# EGA is the mode that comes out whole here, and for a reason: its
	# `[0x2044]` does nothing, so *nothing* in the town needs a colour table and
	# the whole playfield lands except 43 pixels of sprite outline (the same
	# 640ths-vs-320ths edge as in the cavern).  In CGA / HGC / Tandy the town's
	# art is 2 bpp and each driver colours it through its *own* 16-entry table
	# in gt*.bin, which is not the pair table's image — see port/README.md
	# "What the pair buffer cannot reach".  What is checked there is the HUD,
	# whose bar and digits are pairs and are exact.
	./zeliard --dir ../zeliard --video ega --town 1 --town-scr 179 --town-col 209 \
		--town-anim 3 0 --frames 1 --screenshot 1 /tmp/zel_town_ega.png
	python3 tools/compare_shot.py /tmp/zel_town_ega.png ../docs/screenshots/town_ega.png \
		--box 96 14 448 66 --label "EGA Muralla: everything above the sprites"
	python3 tools/compare_shot.py /tmp/zel_town_ega.png ../docs/screenshots/town_ega.png \
		--box 96 142 448 16 --label "EGA Muralla: the near ground strips"
	python3 tools/compare_shot.py /tmp/zel_town_ega.png ../docs/screenshots/town_ega.png \
		--box 96 80 48 62 --label "EGA Muralla: left of the NPC"
	python3 tools/compare_shot.py /tmp/zel_town_ega.png ../docs/screenshots/town_ega.png \
		--box 240 80 264 62 --label "EGA Muralla: between the NPC and Garland"
	python3 tools/compare_shot.py /tmp/zel_town_ega.png ../docs/screenshots/town_ega.png \
		--box 168 163 200 6 --label "EGA Muralla: HUD LIFE bar"
	python3 tools/compare_shot.py /tmp/zel_town_ega.png ../docs/screenshots/town_ega.png \
		--box 152 187 72 8 --label "EGA Muralla: HUD GOLD digits"
	./zeliard --dir ../zeliard --video cga --town 1 --town-scr 179 --town-col 209 \
		--town-anim 3 0 --frames 1 --screenshot 1 /tmp/zel_town_cga.png
	python3 tools/compare_shot.py /tmp/zel_town_cga.png ../docs/screenshots/town_cga.png \
		--box 84 163 100 6 --label "CGA Muralla: HUD LIFE bar"
	python3 tools/compare_shot.py /tmp/zel_town_cga.png ../docs/screenshots/town_cga.png \
		--box 76 187 36 8 --label "CGA Muralla: HUD GOLD digits"
	python3 tools/compare_shot.py /tmp/zel_town_cga.png ../docs/screenshots/town_cga.png \
		--box 152 187 30 8 --label "CGA Muralla: HUD ALMAS digits"
	./zeliard --dir ../zeliard --video cga2 --town 1 --town-scr 179 --town-col 209 \
		--town-anim 3 0 --frames 1 --screenshot 1 /tmp/zel_town_cga2.png
	python3 tools/compare_shot.py /tmp/zel_town_cga2.png ../docs/screenshots/town_cga2.png \
		--box 168 163 200 6 --label "cga2 Muralla: HUD LIFE bar"
	python3 tools/compare_shot.py /tmp/zel_town_cga2.png ../docs/screenshots/town_cga2.png \
		--box 152 187 72 8 --label "cga2 Muralla: HUD GOLD digits"
	./zeliard --dir ../zeliard --video hgc --town 1 --town-scr 179 --town-col 209 \
		--town-anim 3 0 --frames 1 --screenshot 1 /tmp/zel_town_hgc.png
	python3 tools/compare_shot.py /tmp/zel_town_hgc.png ../docs/screenshots/town_hgc.png \
		--box 208 254 200 8 --label "HGC Muralla: HUD LIFE bar"
	python3 tools/compare_shot.py /tmp/zel_town_hgc.png ../docs/screenshots/town_hgc.png \
		--box 192 286 72 11 --label "HGC Muralla: HUD GOLD digits"
	python3 tools/compare_shot.py /tmp/zel_town_hgc.png ../docs/screenshots/town_hgc.png \
		--box 344 286 60 11 --label "HGC Muralla: HUD ALMAS digits"
	./zeliard --dir ../zeliard --video tandy --town 1 --town-scr 179 --town-col 209 \
		--town-anim 3 0 --frames 1 --screenshot 1 /tmp/zel_town_tandy.png
	python3 tools/compare_shot.py /tmp/zel_town_tandy.png ../docs/screenshots/town_tandy.png \
		--box 76 187 36 8 --label "Tandy Muralla: HUD GOLD digits"
	python3 tools/compare_shot.py /tmp/zel_town_tandy.png ../docs/screenshots/town_tandy.png \
		--box 152 187 30 8 --label "Tandy Muralla: HUD ALMAS digits"
	# and the gd decoders themselves against tools/grp2png.py's, the reference
	# implementation that produced docs/screenshots/intro_art.png
	python3 tools/compare_gdart.py ../zeliard
	# The town captures above are all entry frames, which is how #37/#38 got
	# through 186 comparisons.  These three are the walk a player actually takes.
	./zeliard --dir ../zeliard --town 0 --screenshot 1 /tmp/zel_castle.png --frames 1
	python3 tools/compare_shot.py /tmp/zel_castle.png ../docs/screenshots/town_castle.png \
		--box 48 14 224 104 --label "castle: ympd panorama + map rows 0-3"
	python3 tools/compare_shot.py /tmp/zel_castle.png ../docs/screenshots/town_castle.png \
		--box 48 102 80 40 --label "castle: the tiles left of Garland"
	python3 tools/compare_shot.py /tmp/zel_castle.png ../docs/screenshots/town_castle.png \
		--box 48 142 224 16 --label "castle: the near ground strips (phase 0, just painted)"
	python3 tools/compare_shot.py /tmp/zel_castle.png ../docs/screenshots/town_castle.png \
		--box 48 158 224 42 --label "castle: the HUD"
	./zeliard --dir ../zeliard --town 0 --script "R400" --frames 401 \
		--screenshot 5 /tmp/zel_walkin.png
	python3 tools/compare_shot.py /tmp/zel_walkin.png ../docs/screenshots/town_edge_walkin.png \
		--box 48 14 224 88 --label "Muralla walk-in: the mountains survive the edge exit"
	python3 tools/compare_shot.py /tmp/zel_walkin.png ../docs/screenshots/town_edge_walkin.png \
		--box 48 142 224 16 --label "Muralla walk-in: cmap's parallax phase carried over"
	python3 tools/compare_shot.py /tmp/zel_walkin.png ../docs/screenshots/town_edge_walkin.png \
		--box 48 158 224 42 --label "Muralla walk-in: the HUD"
	./zeliard --dir ../zeliard --town 0 --script "R400" --frames 401 \
		--screenshot 39 /tmp/zel_walked.png
	python3 tools/compare_shot.py /tmp/zel_walked.png ../docs/screenshots/town_edge_walked.png \
		--box 48 14 224 104 --label "Muralla 22 columns in: panorama + map rows 0-3"
	python3 tools/compare_shot.py /tmp/zel_walked.png ../docs/screenshots/town_edge_walked.png \
		--box 48 142 224 16 --label "Muralla 22 columns in: the near ground strips"
	python3 tools/compare_shot.py /tmp/zel_walked.png ../docs/screenshots/town_edge_walked.png \
		--box 48 158 224 42 --label "Muralla 22 columns in: the HUD"
	# --- the sword mid-swing (issue #35) -------------------------------
	./zeliard --dir ../zeliard --map 0 --pos 61 7 --headless --frames 8 \
		--script "X8" --screenshot 5 /tmp/zel_sword.png
	python3 tools/compare_shot.py /tmp/zel_sword.png ../docs/screenshots/cavern_sword.png \
		--box 160 94 32 24 --label "mid-swing: the blade (sword.grp section 0)"
	python3 tools/compare_shot.py /tmp/zel_sword.png ../docs/screenshots/cavern_sword.png \
		--box 144 94 15 24 --label "mid-swing: Garland's sword-arm pose (fman 36/80)"
	python3 tools/compare_shot.py /tmp/zel_sword.png ../docs/screenshots/cavern_sword.png \
		--box 48 94 96 24 --label "mid-swing: the playfield west of him"
	python3 tools/compare_shot.py /tmp/zel_sword.png ../docs/screenshots/cavern_sword.png \
		--box 192 94 80 24 --label "mid-swing: the playfield east of the blade"
	python3 tools/compare_shot.py /tmp/zel_sword.png ../docs/screenshots/cavern_sword.png \
		--box 48 14 224 80 --label "mid-swing: the band above Garland"
	python3 tools/compare_shot.py /tmp/zel_sword.png ../docs/screenshots/cavern_sword.png \
		--box 48 118 224 40 --label "mid-swing: the band below Garland"
	python3 tools/compare_shot.py /tmp/zel_sword.png ../docs/screenshots/cavern_sword.png \
		--box 159 94 1 15 --label "mid-swing: his last sprite column, above (159,109)"
	python3 tools/compare_shot.py /tmp/zel_sword.png ../docs/screenshots/cavern_sword.png \
		--box 159 110 1 8 --label "mid-swing: his last sprite column, below (159,109)"
	python3 tools/compare_shot.py /tmp/zel_sword.png ../docs/screenshots/cavern_sword.png \
		--box 0 158 320 42 --label "mid-swing: the HUD strip"
	python3 tools/compare_shot.py /tmp/zel_sword.png ../docs/screenshots/cavern_sword.png \
		--box 0 0 48 158 --label "mid-swing: the left stone frame"
	python3 tools/compare_shot.py /tmp/zel_sword.png ../docs/screenshots/cavern_sword.png \
		--box 272 0 48 158 --label "mid-swing: the right stone frame"
	python3 tools/compare_shot.py /tmp/zel_sword.png ../docs/screenshots/cavern_sword.png \
		--box 48 0 224 14 --label "mid-swing: the strip above the playfield"

# --------------------------------------------------------------- fuzzing ---
# `make fuzz` builds the libFuzzer harnesses in fuzz/ (needs clang; gcc has no
# libFuzzer).  They fuzz the resource parsers -- sar_decompress, map_parse and
# the .grp section walkers / file-driven loaders -- because those follow
# file-internal offsets, and a web build (#44) turns "a user's own game dump"
# into "files dropped onto a page".
#
# `make fuzz-corpus` seeds FUZZ_CORPUS (default fuzz/corpus) from ../zeliard.
# The seeds are byte ranges of the copyrighted game files, so the corpus is
# gitignored and must NEVER be committed; without game files the step skips
# and the fuzzers start from an empty corpus, which is what CI's smoke run
# does.  `make fuzz-smoke` runs each harness for FUZZ_TIME seconds (default 30)
# and fails on any crash/overflow/leak.  For a real session run the binaries
# by hand:  ./fuzz/fuzz_grp -close_fd_mask=3 fuzz/corpus/grp
FUZZ_CC     ?= clang
FUZZ_CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fno-omit-frame-pointer
FUZZ_SAN     = -fsanitize=address,undefined -fno-sanitize-recover=all
FUZZ_CORPUS ?= fuzz/corpus
FUZZ_TIME   ?= 30
FUZZ_OBJ     = $(CORE:%.o=fuzz/obj/%.o)

fuzz/obj/%.o: %.c
	@mkdir -p fuzz/obj
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SAN) -fsanitize=fuzzer-no-link -c -o $@ $<

fuzz/fuzz_%: fuzz/fuzz_%.c $(FUZZ_OBJ)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SAN) -fsanitize=fuzzer -o $@ $^ -lm

fuzz/fuzz_seed: fuzz/fuzz_seed.c fuzz/obj/sar.o
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SAN) -o $@ $^

fuzz: fuzz/fuzz_sar fuzz/fuzz_map fuzz/fuzz_grp

fuzz-corpus: fuzz/fuzz_seed
	@if [ -f ../zeliard/ZELRES1.SAR ] || [ -f ../zeliard/zelres1.sar ]; then \
	  ./fuzz/fuzz_seed ../zeliard $(FUZZ_CORPUS); \
	else \
	  echo "SKIP: fuzz-corpus needs the original game files in zeliard/;"; \
	  echo "      the fuzzers will start from an empty corpus."; \
	fi

fuzz-smoke: fuzz fuzz-corpus
	mkdir -p $(FUZZ_CORPUS)/sar $(FUZZ_CORPUS)/map $(FUZZ_CORPUS)/grp
	./fuzz/fuzz_sar -max_total_time=$(FUZZ_TIME) -close_fd_mask=1 $(FUZZ_CORPUS)/sar
	./fuzz/fuzz_map -max_total_time=$(FUZZ_TIME) -close_fd_mask=1 $(FUZZ_CORPUS)/map
	./fuzz/fuzz_grp -max_total_time=$(FUZZ_TIME) -close_fd_mask=3 $(FUZZ_CORPUS)/grp

clean:
	rm -f *.o zeliard test_physics test_combat test_town test_boss test_shop test_status test_playthrough test_audio test_cutscene test_video
	rm -rf fuzz/obj fuzz/fuzz_sar fuzz/fuzz_map fuzz/fuzz_grp fuzz/fuzz_seed

.PHONY: all test verify playthrough clean fuzz fuzz-corpus fuzz-smoke
