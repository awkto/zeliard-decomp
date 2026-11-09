# start: JASHD.usr
# make: --town 8 --town-col 4 --level 99 --life 4000 --sword 6 --shield 4 --keys 1 --save JASHD
# cave: 0,29,6,0,0x1B
# what: Jashiin regeneration landing exactly on 800 HP (#114): the first 600 frames of jashiin_damage, then idle.  AB95..AB9D only leave phase 2 when HP+80 overshoots 800; exactly 800 stores it and stays in phase 2 (AC32/AC22 untouched, part type 0x01 not 0x61), where the port had resumed phase 1 and set the 0x60 bits.
# allow-sfx: 370,473,501 -- Jashiin's shot-1 effect (3A, boss_mao2 launch_shot1 / A8E5): the original writes it at these boundaries, the port requests none; fields and pictures match, filed as #136
L3 .20 U3 .296 RX1 URX2 X2 .1 X1 .1 UL7 RX1 URX4 RX1 URX4 LX1 .1 UL4 L2 LX2 ULX2 LX1 ULX2 LX1 ULX2 LX2 ULX1 L1 ULX1 L7 .15 R6 URX2 R1 URX5 X1 URX1 X2 URX1 U1 ULX1 UR1 U3 .7 URX2 X1 URX1 X2 URX1 U1 ULX1 UR1 U3 UL4 .2 ULX2 LX1 ULX2 LX2 ULX1 L1 RX1 UL3 R2 RX3 URX8 X1 UX1 .1 UX1 .13 LX5 ULX8 U1 ULX2 UX2 RX1 URX6 X3 URX2 .1 RX1 R1 .1 R2 UR2 R1 UR1 R5 UR2 URX3 .1 URX1 X1 UX3 LX1 UX1 ULX5 X2 ULX1 U1 ULX1 L4 UL2 L1 UL1 L2 .1 L4 LX3 ULX3 LX1 ULX2 LX2 ULX1 RX1 URX6 X3 URX2 .1 RX1 R1 .1 R2 UR1 .400
