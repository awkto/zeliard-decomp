# start: LVN.usr
# make: --town 1 --town-col 172 --exp 20 --save LVN
# what: Muralla sage, See Power with too little EXP (20 of 50): verdict 0, no level-up (#123)
# note: starts from LVN.usr, not LVNO.usr: the save's file name alone moves the oracle's first [A002] pass of the Sage by one tick (the restore spends different cycles), and with LVNO the hook step at the end of the first wait is lost (38 frames of the blink out of phase); same as BANK.usr for bank_deposit (#124, HARNESS.md s1a)
# allow-px: 112,50,117,55 frames 51-65 -- #125: the sage's blink toggle on the same edge as a frame boundary: the oracle's ISR landed between the hook's [FF50] read and the poll site, so the boundary picture has the old eyes (4 pixels of the eyes cell); a phase race inside a 64-cycle loop iteration that the port does not model (HARNESS.md s1a)
# allow-px: 112,50,117,55 frames 157-157 -- #125: the sage's blink toggle on the same edge as a frame boundary: the oracle's ISR landed between the hook's [FF50] read and the poll site, so the boundary picture has the old eyes (4 pixels of the eyes cell); a phase race inside a 64-cycle loop iteration that the port does not model (HARNESS.md s1a)
U2 .5 X1 .5 D1 .3 X1 .5 X1 .20 X1 .20 X1 .20 X1 .20 X1 .20 X1 .20 X1 .20 M1 .30 X1 .20 X1 .20
