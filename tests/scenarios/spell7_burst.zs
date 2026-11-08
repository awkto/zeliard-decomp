# start: SP7B.usr
# make: --town 1 --town-col 205 --level 25 --life 3000 --sword 6 --shield 4 --keys 3 --worn 1 --magic 7=5 --save SP7B
# what: the screen-wide spell (spell 7, #122): the cast (fight.bin 8918) calls gfmcga 42F7 -- an XOR flash of the playfield, three sets of nine expanding rectangles drawn in 0x36 and again in 0, a flash back.  54 timer waits (exit 4471, 3 ticks each at speed 5), pictured as side_*_w4471.png; the oracle patch pictures 4471 although the wait is under 8 ticks
U3 .40 M1 .40 R4 M1 .30
