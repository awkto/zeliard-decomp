# start: JASHE.usr
# make: --town 8 --town-col 4 --level 99 --life 700 --sword 6 --shield 0 --keys 1 --save JASHE
# cave: 0,29,6,0,0x1B
# what: Hero death to Jashiin at LV99 (#114): LIFE 700, no shield; the autopilot prefix of jashiin_damage, then idle until the death text.  99BB: the death EXP bonus is 8-bit 0x7F-2*level, so LV99 gets 0xB9 = 185 (the port clamped to 0).
# allow-sfx: 370,473,516 -- Jashiin's shot-1 effect (3A, boss_mao2 launch_shot1 / A8E5): the original writes it at these boundaries, the port requests none; fields and pictures match, filed as #136
L3 .20 U3 .296 RX1 URX2 X2 .1 X1 .1 UL7 RX1 URX4 RX1 URX4 LX1 .1 UL4 L2 LX2 ULX2 LX1 ULX2 LX1 ULX2 LX2 ULX1 L1 ULX1 L7 .15 R6 URX2 R1 URX5 X1 URX1 X2 URX1 U1 ULX1 UR1 U3 .7 URX2 X1 URX1 X2 URX1 U1 ULX1 UR1 U3 UL4 .2 ULX2 LX1 ULX2 LX2 .90
