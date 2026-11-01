# start: CALIK.usr
# make: --town 7 --town-col 8 --level 25 --life 999 --sword 6 --shield 4 --keys 1 --save CALIK
# what: Llama's column-8 door -> MP70 (152,7) -> (176,43) -> MP72 (101,57) (cavern 7): east along the bottom corridor into the rope gap at (108,58), the fall through the ring onto the top corridor, (116,11)'s rope hole down to the middle corridor and west along it into the current at (88..90,25) that pushes the hero back east.  The current cells are one-way walls (67A3/6942) everywhere *but* cavern 7 (#87: the port had the test the other way round).  Script from --pilot "town-cave 8; door 176,43; cell 90,25" plus a hand-made walk/jump west
U3 R21 UR2 .6 UR2 R8 X1 .3 R9 U37 L3 R1 U2 L3 .5 L10 U3 R7 .2 D14 R1 .8 R5 .2 D14 L2 .8 L4 UL2 .6 L2 X1 .9 L3 X1 .9 L15 L8 UL9 L6 .10
