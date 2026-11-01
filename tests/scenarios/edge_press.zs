# start: EDGEP.usr
# make: --town 1 --town-col 12 --save EDGEP
# what: #79: Space pressed on the frame before each town edge (Muralla -> the castle at f12, back at f35): on the edge frame the town does not take it (f11/f34 FF1D = FF); change_town_map 6D30's [10C] loads (STICK 0CDC) and 60C3 drop it before the new map's first frame
L10 LX1 L9 .5 R8 RX1 R5 .10
