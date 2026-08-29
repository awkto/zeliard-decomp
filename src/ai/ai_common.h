/*
 * ai_common.h — shared ABI of the Zeliard enemy-AI overlays (eai1..8.bin and the
 * boss overlays crab/tako/tori/zela/meda/lega/drgn/akma/mao1/mao2/zel2.bin,
 * ZELRES3[1..19]).  Every overlay is a raw image loaded to BASE:A000 by
 * fight.bin 7EBB (request table 9CBC); CS = DS = BASE, the data arena segment
 * is at [cs:FF2C].  NOT COMPILABLE: pseudo-C that mirrors the 8086 code.
 *
 * All facts below were verified against fight.bin (disasm/overlays/fight.asm)
 * and gfmcga.bin (disasm/overlays/gfmcga.asm); addresses are given for every
 * contract.
 *
 * ------------------------------------------------------------------------
 * 1. Overlay header (A000..A02F) — read by fight.bin / gfmcga
 * ------------------------------------------------------------------------
 *   [A000] u16  entry      per-enemy update routine.  Called by fight.bin 8DF7
 *                          (jmp [cs:A000]) for every live enemy that is inside the
 *                          ring: SI = &record, DI = ring address of the sprite's
 *                          current (row, rcol) cell (marker already removed),
 *                          [FF4A] = object index.  Returns with ret.
 *                          Boss maps (FF34 or [E6] set): called ONCE per frame
 *                          instead of the whole enemy pass (8D1D) with no
 *                          registers set up, and once right after loading (7C27).
 *   [A002] u16  boss_info  0 in eai1..8.  Boss overlays (see 6150/6162/6FE1/71E1):
 *                          +0 u16, +2 u8   used by the boss AI itself (start col/row)
 *                          +3 u16  passed in BX to video [200A] and [200C] at level start
 *                          +5 u16  EXP awarded when boss_defeated && boss_state == FF (71E8)
 *                          +7 u8   hero screen column the camera pulls to (6FE8; 12 elsewhere)
 *                          +8 u8   -> 9F01 boss_knock_left (knockback always to the left)
 *                          +9 u16  pointer passed in SI to video [2010] at level start (615B)
 *                          +B u16  gold awarded with the EXP (71F1)   <- FIGHT.md says +9: wrong
 *                          +D..    private to the boss AI
 *   [A004] u16  (unused, 0 in every overlay)
 *   [A006] u16  drop_lists -> u16[8] per class -> u8[4] drop item ids (97E2..9805):
 *                          index = KRN_RANDOM()&3, always 0 for a down-thrust kill.
 *                          ids: 0 corpse only, 1 vanish, 4 coin (1 G), 5 coin (10 G),
 *                          9 potion (0x19 full), 0xB shoes/coin 100 G class (item 0x1B), ...
 *                          (fight.bin 90E6: the object becomes item type 0x70|id)
 *   A008   u8[8]  exp per class (type & 0xF), added by 96C1 on death
 *   A010   u8[16] contact damage per (type & 0xF), summed by fight.bin 7675 per frame
 *   A020   16 bytes unused
 *   A030   u16[32] frame tables, facing LEFT  (hit bit 7 clear)   } read by gfmcga 36D8
 *   A070   u16[32] frame tables, facing RIGHT (hit bit 7 set)     }
 *          Index = type & 0x1F: 0..7 live classes, 8..F the same class while dying
 *          (type |= 0x68 -> class+8), 0x10..0x1F items (0x10 corpse, 0x13 treasure box,
 *          0x14/0x15 coins, 0x16 key, 0x18/0x19 potions, ...).  Each entry points to a
 *          list of 5-byte frames {palette, cellTL, cellTR, cellBL, cellBR}; the frame
 *          drawn is entry[(phase & 0xF)].  palette 0..4 selects the 4-colour set
 *          (gfmcga tables 4F98..4FD8); while (hit & 0x20) and not in a boss map the
 *          renderer adds 3 to it (hit flash).  Cell 0 = transparent.
 *   The rest of the overlay is free: frame lists, drop lists, then code.
 *
 * ------------------------------------------------------------------------
 * 2. The enemy record (16 bytes in the map's C010 table, struct obj in fight.c)
 * ------------------------------------------------------------------------
 */
typedef unsigned char u8; typedef unsigned short u16;

struct enemy {
    u16 col;        /* +0  map column; high byte 0xFF = inactive (0xFF00 on removal) */
    u8  row;        /* +2  ring row (0..63) of the sprite's top-left cell (2x2 cells) */
    u8  rcol;       /* +3  ring column, recomputed by fight.bin 8D38 every frame; the hero's
                           top-left ring column is hero_scr_col+4 = 0x10, his body column 0x11 */
    u8  type;       /* +4  bits 0-3 class; 0x08 dying; 0x10 item; 0x20 sword-immune;
                           0x40 no contact damage; 0x80 solid to the hero */
    u8  hit;        /* +5  bits 0-4 hit source (1 sword, 2..8 magic, 9 orb, 0 stomp);
                           0x20 HIT_STUN: hit landed this frame (fight.bin 8DB9 sets it from
                           the pending bit and clears it again next frame); 0x40 pending;
                           0x80 FACING_RIGHT (AI/renderer private: selects the A070 tables) */
    u8  phase;      /* +6  low nibble = animation frame; upper bits are AI timers */
    u8  flags;      /* +7  bits 0-3 drop id; 0x10 tall (two records); 0x20 event object;
                           0x40 clear row of [link] on death; 0x80 hero-overlap latch */
    u8  hp;         /* +8  0 at spawn: every AI writes its initial value on the first update */
    u8  next;       /* +9  AI state byte (free use) */
    u8  link;       /* +A  AI counter (free use) */
    u16 home_col;   /* +B  respawn column, 0xFFFF never */
    u8  home_row;   /* +D */
    u8  home_type;  /* +E */
    u8  timer;      /* +F  respawn timer, incremented by fight.bin 8D90 (spawn on wrap) */
};
#define FACING_RIGHT 0x80      /* hit bit 7 */
#define HIT_STUN     0x20      /* hit bit 5 */

/* ------------------------------------------------------------------------
 * 3. fight.bin services (near vectors at BASE:6000 + 2*n, call [cs:6000+2n]).
 *    All take SI = &enemy (preserved) and clobber AX/BX/CX/DI unless noted.
 *    "blocked" = returns CF=1.  Directions: 0 R, 1 RU, 2 U, 3 LU, 4 L, 5 LD,
 *    6 D, 7 RD (same numbering as projectile flags & 7).
 * ------------------------------------------------------------------------ */
/* vec 2  9723  fight_step_dir(AL & 7)   -> one of vectors 4..11 (table 972F)               */
bool fight_step_dir(u8 dir);
/* vec 3  973F  fight_probe_dir(AL & 7)  -> one of vectors 12..19 (table 974B), no move      */
bool fight_probe_dir(u8 dir);
/* vec 4..11 91E5/91F6/920A/9222/9234/9243/9255/926C: probe (vectors 12..19), then move.
 *    Ring-edge refusals: R/RU/RD need rcol < 0x22; L/LU/LD need rcol >= 2;
 *    U/D need rcol != 0 and != 0x23.  col wraps at map width (927F/9293),
 *    row is masked & 0x3F (92A4/92AC).  CF=1 blocked, record untouched.          */
bool fight_step_right(void), fight_step_right_up(void), fight_step_up(void), fight_step_left_up(void),
     fight_step_left(void), fight_step_left_down(void), fight_step_down(void), fight_step_right_down(void);
/* vec 12..19  probes for a 2x2 sprite at (row, rcol).  "P" = cell_passable_ai() must hold,
 *    "S" = additionally blocked by any sprite marker in that cell.  Blocked -> CF=1.
 *    12 92B4 right:      P(row,c+2) P(row+1,c+2)         S(row-1,c+2)
 *    13 930A left:       P(row,c-1) P(row+1,c-1)         S(row-1..row+1, c-2)
 *    14 9362 up:         P(row-1,c) P(row-1,c+1)         S(row-2, c-1..c+1)
 *    15 939A down:       P(row+2,c) P(row+2,c+1)         S(row+2, c-1)
 *    16 93C5 right-up:   P(row,c+2) P(row-1,c+2) P(row-1,c+1)   S(row-2, c..c+2)
 *    17 940C right-down: P(row+1,c+2) P(row+2,c+2) P(row+2,c+1) S(row,c+2) S(row+2,c)
 *    18 9452 left-up:    P(row,c-1) P(row-1,c-1) P(row-1,c)     S(row,c-2) S(row-1,c-2) S(row-2,c-2..c)
 *    19 949A left-down:  P(row+1,c-1) P(row+2,c-1) P(row+2,c)   S(row,c-2) S(row+1,c-2) S(row+2,c-2)
 *    In cavern 5 (C012 == 5) the right probe is also blocked by a CURRENT-LEFT tile and the
 *    left probe by a CURRENT-RIGHT tile in the two P cells (92F4/934A via 76F6).       */
/* vec 20  6D6E  ring_addr:      AL = row, AH = col -> DI = E000 + (row&63)*0x24 + col (BX kept) */
/* vec 21  6D82  ring_wrap_down: if SI >= E900 then SI -= 0x900                                */
/* vec 22  6D8E  ring_wrap_up:   if SI <  E000 then SI += 0x900                                */
/* vec 23  94E1  cell_passable_ai: AL = cell -> ZF=1 passable.  cell < 0x49: passable iff in the
 *    24-byte list at arena:8000 (so DCHR 0x40..0x48 fixtures are solid); 0x49..0x7F: passable
 *    (DCHR items/doors do NOT block enemies); 0x80.. (sprite markers): NOT passable.
 *    (FIGHT.md §3/§7 has this reversed.)                                                     */
/* vec 24  97A0  fight_on_hazard: cell (row+2, rcol) -> 73C0: ZF=1 if it is in the hazard list
 *    8020 (AH = 0xFF and ZF=0 otherwise)                                                     */
/* vec 25  96D5  fight_enemy_killed: phase = 0, type |= 0x68, hit &= 0x80 (second record of a
 *    tall enemy too), sound 7 if within 19 rows of the window.  NO exp, NO drop.             */
/* vec 26  97B5  fight_take_damage: damage_for_source(hit & 0x1F) (vec 28); hp -= it, sound 6;
 *    at 0: roll a drop from [[A006] + (type&7)*2] unless flags&0xF already set, exp +=
 *    A008[type&0xF] (96C1), then vec 25.  Every AI calls it when it sees hit & 0x20.      */
/* vec 27  96A1  map_col_to_ring: AX = map col -> BL = ring col, CF=1 if not inside the ring  */
/* vec 28  9851  damage_for_source: AL = source -> AH = damage (see fight.c)                  */
/* vec 29  8611  shot_spawn: BX -> 13-byte struct shot template, appended to EB80 (max 31).
 *    {col, row, cell, age, life, flags(dir&7 | 0x08 through walls | 0x40 scripted), damage,
 *     u16 drawn(0), u16 script, last_col, last_row}.  Shots move 1 cell/frame (85A5).      */
/* vec 30  83DB  shots_clear: erase and drop every projectile                                 */
/* vec 31  98C5  find_spare_object: DI = first C010 record with home_col == 0xFFFF that is
 *    either inactive with row == 0x7F, or active, inside the ring and not an item;
 *    DL = its index.  If none, DI -> the 0xFFFF terminator (CF is always 0).               */
/* vec 32  975B  fight_ride_current: if cell (row+1, rcol) or (row+1, rcol+1) is an updraft /
 *    current-left / current-right tile (76F6: CL = 0/1/2), POP the caller's return address
 *    and do two steps up / left / right (9788 table), returning to the caller's caller.    */
/* kernel [cs:11A]  KRN_RANDOM: AX = running sum seeded by the tick counter (STICK 0918)     */

/* fight.bin globals the AIs read (see docs/STATE_PAGE.md) */
extern u8  hero_map_row;      /* FF35  ring row of the hero's top-left (3x3 sprite) */
extern u8  hero_hit_flash;    /* FF36  hero took damage this frame */
extern u8  hero_scr_col;      /* 0083  hero top-left screen column; ring col = +4 */
extern u8  hero_scr_row;      /* 0084 */
extern u8  scroll_row;        /* 0082 */
extern u8  hero_flags;        /* 00C2  bit0 facing left */
extern u8  crouching;         /* FF38 */
extern u8  sfx_request;       /* FF75 */
extern u8  boss_cutscene, boss_dying, boss_defeated;   /* FF2E FF2F FF30 */
extern u8  boss_state;        /* EDA0  0xFF = boss defeated (fight.bin 71DA awards EXP/gold) */
extern u8  ring[64][0x24];    /* E000 */
extern u8 *win;               /* FF31 */
