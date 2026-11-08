# start: FLAG49.usr
# make: --town 2 --town-col 4 --level 3 --life 255 --shield 1 --keys 1 --flag 49=FF --save FLAG49
# what: entering a cavern from a town with [49] (Jashiin's end-game flag) set (#113): the cave mouth (cave_door's route: R13, U2) loads MP10, fight.bin's 7D2D..7D61 -> 6042 restart reaches 6266 `test [49],FF / jnz 98FC` and the hero dies on his first frame there (the dying frames, hero_die with 99B4: Felishika's Castle, entered alive: 6055), the same forced death the exit door of MPA0 gives (jashiin_kill).
# (music_stopped: with [49] set the town entry's INT 60h AX=0 (town.bin 61F9) comes one frame after the first boundary in the oracle (MUSLOG: f=1, FF26 FF -> 0), the port starts the town score while it loads; every other FF24-FF26 byte is still compared, and music_stopped is compared in the 360 frames after the cave mouth only in the other scenarios)
# allow: music_stopped any
L2 .40 R13 .3 U2 .300
