# start: JASHN.usr
# make: --town 8 --town-col 4 --level 99 --life 4000 --sword 6 --shield 0 --keys 1 --save JASHN
# cave: 0,29,6,0,0x1B
# what: Arena tail of fman.grp (#114): the no-shield hero walking right and swinging in MPA0 (jashiin_damage route, shield 0).  Cell 0xE5 of fman.grp is 3 bytes short, so the original reads arena:7FF0..7FF2 after the file; they hold the residue of the town load (20 44 01 in every town, verified for 10 towns and two routes with a memory watch), which 4EDD then converts in place.  The port assumed 00 80 02 and drew one wrong pixel at (157,125) from frame 330.
# allow-sfx: 370 -- Jashiin's shot-1 effect (3A, boss_mao2 launch_shot1 / A8E5): the original writes it at these boundaries, the port requests none; fields and pictures match, filed as #136
L3 .20 U3 .294 R8 RX4 .2 RX4 .2 R6 RX4 .2 RX4 .2 LX4 .2 RX4 .2 R8 RX4 .2 RX4 .20
