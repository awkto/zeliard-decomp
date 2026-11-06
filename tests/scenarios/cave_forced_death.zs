# start: FLAG49.usr
# make: --town 2 --town-col 4 --level 3 --life 255 --shield 1 --keys 1 --flag 49=FF --save FLAG49
# what: entering a cavern from a town with [49] (Jashiin's end-game flag) set (#113): the cave mouth (cave_door's route: R13, U2) loads MP10, fight.bin's 7D2D..7D61 -> 6042 restart reaches 6266 `test [49],FF / jnz 98FC` and the hero dies on his first frame there (the dying frames, hero_die with 99B4: Felishika's Castle, entered alive: 6055), the same forced death the exit door of MPA0 gives (jashiin_kill).
L2 .40 R13 .3 U2 .300
