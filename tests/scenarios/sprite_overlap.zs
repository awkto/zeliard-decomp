# start: DRAGONK.usr
# make: --town 7 --town-col 262 --level 25 --life 3000 --sword 6 --shield 4 --keys 1 --worn 1 --save DRAGONK
# cave: 0,21,15,0,22
# what: Llama's right-edge mouth (record 0) poked to lead straight into the Dragon's room MP7D (21,15) (system map 22): the hero stands still while DRGN's head and flame parts overlap, so gfmcga 3468's FE/FF claim on the E900 bytes under a sprite (word xchg 3472/347A: TL<-FE, TR/BL/BR<-FF) and 356E's skip of a cell whose saved byte is FF/FC decide the picture (#109); drawing every cell regardless (render.c draw_enemies without the skip) changes 25 frames (without the BL or the BR claim alone: also 25)
R7 U2 .30 .180
