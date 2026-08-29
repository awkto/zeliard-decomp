/*
 * fight.c — hand-cleaned decompilation of fight.bin (ZELRES2[0], 16174 bytes),
 * the Zeliard cavern game loop.  Slot-A overlay loaded raw to BASE:6000.
 *
 * NOT COMPILABLE.  Pseudo-C written from the Ghidra dump + ndisasm listing.
 * Every routine carries its original address; constants cite the instruction
 * they come from.  Register conventions of the original are noted where the
 * AI overlays (eai*.bin @A000) depend on them.
 *
 * Conventions used below
 *   BASE:xxxx      the shared code/data segment (CS = DS = BASE)
 *   arena:xxxx     the 64 KB graphics/data arena at [cs:FF2C] (= BASE+0x1000)
 *   ring           36 cols x 64 rows of cell bytes at BASE:E000, stride 0x24
 *   screen copy    28 cols x 19 rows at BASE:E900, stride 0x1C
 *   cell value     0 empty, 1..0x3F tileset (MPPx.GRP), 0x40..0x7F DCHR.GRP,
 *                  0x80|i = sprite marker for object i of the C010 table
 *
 * Coordinate model (see docs/FIGHT.md):
 *   scroll_col (0x80)     map column shown in ring column 0
 *   scroll_row (0x82)     ring row shown on screen row 0; win = &ring[scroll_row][0]
 *   hero_scr_col (0x83)   hero top-left on screen, ring column = hero_scr_col+4
 *   hero_scr_row (0x84)   hero top-left screen row; hero is 3x3 cells (24x24 px)
 *   hero_cell()           = win + hero_scr_row*0x24 + hero_scr_col + 4   (6DB1)
 */

/* ------------------------------------------------------------------------ */
/* Kernel / driver entry points (slot numbers; names are best guesses)       */
/* ------------------------------------------------------------------------ */
#define KRN_LOAD            (*(void(*)())0x10C)   /* AL=mode, AH=res, ES:DI, DS:SI request */
#define KRN_IDLE_0          (*(void(*)())0x110)   /* 06AC: F-key/pause services, run while waiting */
#define KRN_IDLE_1          (*(void(*)())0x112)   /* 0723 */
#define KRN_IDLE_2          (*(void(*)())0x114)   /* 07B6: speed menu, writes FF33 */
#define KRN_IDLE_3          (*(void(*)())0x116)   /* 0881 */
#define KRN_IDLE_4          (*(void(*)())0x118)   /* 08EF */
#define KRN_RANDOM          (*(u8 (*)())0x11A)    /* 0918: returns frame counter FF1B (AL used &3) */
#define KRN_QUIT_QUERY      (*(bool(*)())0x11E)   /* 092D: CF=1 when the quit key (FF18 bit 14) confirmed */
#define VID_TEXTBOX_FILL    (*(void(*)())0x2000)
#define VID_2002            (*(void(*)())0x2002)
#define VID_DRAW_BAR        (*(void(*)())0x2004)
#define VID_2006            (*(void(*)())0x2006)
#define VID_HP_DISPLAY      (*(void(*)())0x2008)   /* redraw HP after [0x90] changes */
#define VID_GOLD_DISPLAY    (*(void(*)())0x2014)
#define VID_EXP_DISPLAY     (*(void(*)())0x2016)
#define VID_MAGIC_DISPLAY   (*(void(*)())0x2018)
#define VID_HIT_FLASH       (*(void(*)())0x201A)   /* called when the hero takes contact damage */
#define VID_TEXT            (*(void(*)())0x202A)
#define VID_CONVERT_CELLS   (*(void(*)())0x2044)   /* DS:SI bank, CX cells -> 6-bit MCGA pairs */
#define GF_DRAW_BG          (*(void(*)())0x3000)   /* fight renderer (gfmcga) vectors */
#define GF_DRAW_HERO        (*(void(*)())0x3002)   /* also runs the sword anim: FF43/FF44/FF46 */
#define GF_ERASE_SPRITES    (*(void(*)())0x3004)
#define GF_FLUSH            (*(void(*)())0x3006)
#define GF_BLIT_CELL        (*(void(*)())0x3008)
#define GF_SAVE_CELL        (*(void(*)())0x300A)
#define GF_RESTORE_CELL     (*(void(*)())0x300E)
#define GF_BOSS_DEATH_FX    (*(void(*)())0x3010)
#define GF_3012             (*(void(*)())0x3012)
#define GF_LOAD_HERO_ANIM   (*(void(*)())0x301A)
#define GF_301E             (*(void(*)())0x301E)
#define GF_CONVERT_2BPP     (*(void(*)())0x3028)   /* DS:SI=bank, BP=mask dest, CX=cells */
#define GF_302A             (*(void(*)())0x302A)

/* ------------------------------------------------------------------------ */
/* Player record (BASE:0000 page = STDPLY.BIN "standard player")              */
/* ------------------------------------------------------------------------ */
u8   g_force_death;        /* 0x49  != 0 -> die immediately (6266); also picks town-return path (99AD). uncertain */
u8   g_immortal;           /* 0x7F  != 0 -> HP 0 does not kill (718C). uncertain */
u16  scroll_col;           /* 0x80  map column in ring column 0 (default 0x1E) */
u8   scroll_row;           /* 0x82  ring row on screen row 0 */
u8   hero_scr_col;         /* 0x83  0..27, default 10 (0x0C in caverns, 7D28) */
u8   hero_scr_row;         /* 0x84  0..18, default 10 (= [C016] row bias) */
u8   gold_total_hi;        /* 0x85  } 24-bit lifetime gold (916B), zeroed on death. uncertain */
u16  gold_total;           /* 0x86  } */
u16  gold;                 /* 0x8B */
u8   hero_level;           /* 0x8D  strength term of every damage formula (9851). uncertain name */
u16  exp;                  /* 0x8E */
u16  hp;                   /* 0x90  default 0x50 */
u8   sword;                /* 0x92  1..6 (0 = none: no attacking, 6E3B) */
u8   shield;               /* 0x93  0 = none */
u16  shield_hp;            /* 0x94  durability, "Shield broken" when it reaches 0 (761A) */
u8   keys;                 /* 0x98 */
u8   lion_keys;            /* 0x99 */
u8   glory_crest;          /* 0x9B */
u8   hero_crest;           /* 0x9C */
u8   magic_sel;            /* 0x9D  1..7 selected spell, 0 = none */
u8   shoes;                /* 0x9E  1 Feruza(jump 4 rows) 2 Pirika(no tile damage) 3 Silkarn(no conveyor kick) 4 Ruzeria(no ice slide) 5 = heat immune */
u8   inventory[10];        /* 0xA1  list of item ids, first 0 = free slot (90B9) */
u8   magic_count[7];       /* 0xAB */
u16  max_hp;               /* 0xB2  default 0x50 */
u8   magic_max[7];         /* 0xB4 */
u8   hero_flags;           /* 0xC2  bit0 FACE_LEFT, bit1 WALKING */
#define FACE_LEFT 1
#define WALKING   2
u8   door_side;            /* 0xC3  bit6 of the door record: enter facing left */
u8   cur_map;              /* 0xC4  system resource # of the current map (AH for KRN_LOAD mode 1) */
u8   town_map;             /* 0xC5  map to return to (default 0x81) */
u16  hp_regen_pending;     /* 0xC6  each frame: hp += 8 while > 0 (70E0) */
u8   music_idx;            /* 0xC8  index into the 9E53 request table */
u8   attack_bonus;         /* 0xE4  sword damage x (attack_bonus+1) (9898). uncertain */
u8   boss_room;            /* 0xE6  level record flags bit6 */
u8   hero_anim;            /* 0xE7  0x80 idle; walking increments &0x7F; 0 = jump pose; 0..2 death */
u8   hero_dead;            /* 0xE8 */

/* ------------------------------------------------------------------------ */
/* Global state page FF00 (see docs/STATE_PAGE.md)                            */
/* ------------------------------------------------------------------------ */
u8   snd_proximity;        /* FF08  distance-to-entrance volume (774E) */
u8   tick;                 /* FF1A  +1 per 236.7 Hz timer tick (kernel), zeroed per frame */
u16  frame_counter;        /* FF1B */
u8   btn1_edge, btn2_edge; /* FF1D, FF1E  button press edges set by kernel INT9/joystick */
u8   music_fade;           /* FF24 */
u16  arena_seg;            /* FF2C */
u8   boss_cutscene;        /* FF2E  boss AI: hero frozen / intro running */
u8   boss_dying;           /* FF2F  boss AI: play death fx */
u8   boss_defeated;        /* FF30  boss AI: skip enemy update */
u8  *win;                  /* FF31  ring pointer of screen row 0 col 0 */
u8   speed;                /* FF33  frame = 4*speed ticks (default 5) */
u8   boss_map;             /* FF34  level record flags bit7 */
u8   hero_map_row;         /* FF35  (hero_scr_row + scroll_row) & 63 */
u8   hero_hit_flash;       /* FF36 */
u8   hero_hidden;          /* FF37 */
u8   crouching;            /* FF38 */
u8   on_ladder;            /* FF39  0xFF on ladder, 0x80 knocked off */
u8   hero_entering;        /* FF3A */
u8   casting;              /* FF3C */
u8   vstate;               /* FF3D  0 grounded, 0xFF rising, 0x7F falling/airborne, 0x80 knocked */
#define V_GROUND 0x00
#define V_RISE   0xFF
#define V_FALL   0x7F
#define V_KNOCK  0x80
u8   magic_active;         /* FF3E */
u8   anim_arg;             /* FF3F  passed to renderer (attack variant / cast phase) */
u8   anim_flag;            /* FF40 */
u8   anim_kind;            /* FF41 */
u8   conveyor;             /* FF42  0 none, 1 pushes right, 2 pushes left */
u8   attacking;            /* FF43  set 6F01, cleared by renderer 3F1A */
u8   sword_frame;          /* FF44  renderer */
u8   attack_type;          /* FF45  0 slash, 1 upward, 2 down-thrust */
u8   attack_var;           /* FF46  0/2, renderer increments it as the swing frame */
u8   thrust_latch;         /* FF47 */
u8   joy_dirs, joy_btns;   /* FF48, FF49 kernel INT61 scratch */
u8   obj_index;            /* FF4A  index of the object being processed */
u8   menu_result;          /* FF4B  8 = warp back to town */
u8   sfx_request;          /* FF75  sound effect id for the sound driver */

/* ------------------------------------------------------------------------ */
/* fight.bin locals 9EED..9F2D                                                */
/* ------------------------------------------------------------------------ */
u8   msg_timer_a, msg_timer_b;     /* 9EED, 9EEE */
u8   msg_box_a, msg_box_b;         /* 9EEF, 9EF0  message boxes open (7210) */
u8   msg_box_rows;                 /* 9EF1 */
u16  msg_x; u8 msg_y;              /* 9EF2, 9EF4 */
u8   menu_debounce;                /* 9EF5 */
u8   lvl_flags, lvl_gfx, lvl_tiles, lvl_ai, lvl_enemies, lvl_end; /* 9EF6..9EFB copy of the level record */
u8   loaded_tiles, loaded_enemies; /* 9EFE, 9EFF */
u8   music_restart;                /* 9F02 */
u8   hero_home_row;                /* 9F00  screen row the world scrolls to keep the hero on */
u8   boss_knock_left;              /* 9F01  boss rooms: [[A002]+8]; forces knockback to the left */
u8  *stream_right;                 /* 9F03  tile stream ptr of the column after ring col 35 */
u8  *stream_left;                  /* 9F05  tile stream ptr of the column before ring col 0 */
u8   fixture_anim;                 /* 9F07 */
u8   fall_rows;                    /* 9F08  rows fallen since leaving the ground */
u8   rise_rows;                    /* 9F09  rows risen in the current jump */
u8   crouch_release;               /* 9F0A */
u8   diag_jump;                    /* 9F0B */
u8   conveyor_kick;                /* 9F0C  frames of 1-cell/frame conveyor push after a jump */
u8   max_rise;                     /* 9F0D  2, or 4 with shoes==1 (6F9B) */
u8   hit_side[4];                  /* 9F0E..9F11  contact columns col-1,col,col+1,col+2 */
u16  contact_damage;               /* 9F12 */
u8   hero_hit;                     /* 9F14 */
u8   on_updraft;                   /* 9F15 */
u8   conveyor_phase;               /* 9F16 */
u8   on_hazard;                    /* 9F17 */
u8   regen_tick;                   /* 9F18  reset by any action; hp += 2 every 16 idle frames */
u8   door_msg_latch;               /* 9F19 */
u16  entry_col; u8 entry_row;      /* 9F1A, 9F1C  destination of a map transition */
u8   entry_flags;                  /* 9F1D  bit7 = play the Roka demo overlay first */
u8   post_boss_pending;            /* 9F1E */
u8   projectile_count;             /* 9F1F */
u8   ice_slide;                    /* 9F20  remaining slide steps (ice cavern only) */
u8   ice_steps;                    /* 9F21  steps walked in one direction */
u8   walk_dir;                     /* 9F22  1 right, 2 left, 0 none (this frame) */
u8   slide_dir;                    /* 9F23  bit0: 1 = right */
u8   prev_facing;                  /* 9F24 */
u8   heat_timer;                   /* 9F25  cavern 7 */
u8   boss_intro;                   /* 9F26 */
u8   intro_done;                   /* 9F27 */
u8   death_anim_a, death_anim_b;   /* 9F28, 9F29 */
u8   magic_hit_any;                /* 9F2A */
u8   cast_timer;                   /* 9F2B */
u8   dist_col, dist_row;           /* 9F2C, 9F2D */

/* ------------------------------------------------------------------------ */
/* E000+ work areas                                                           */
/* ------------------------------------------------------------------------ */
u8   ring[64][0x24];               /* E000 */
u8   screen[19][0x1C];             /* E900: 0xFD force redraw, 0xFF hero, 0xFC/0xFE message box */
struct magic  magic[4];            /* EB15  4 x 16 bytes, hero's spell sprites (2x2 cells) */
struct orb    orbs[4];             /* EB60  4 x 7 bytes, orbiting spheres */
struct shot   shots[31];           /* EB80  13-byte enemy projectiles, list ends with col == 0xFF */
u8   under_sprite[128];            /* ED20  cell saved under sprite marker i */
u8   boss_state;                   /* EDA0  0xFF = boss defeated (71DA) */

/* Map header (loaded to BASE:C000 by KRN_LOAD mode 1) — see ARCHITECTURE.md */
#define MAP_LEVEL     (*(u8 **)0xC000)
#define MAP_WIDTH     (*(u16 *)0xC002)
#define MAP_FIXA      (*(u8 **)0xC004)   /* elevators: {u16 col, u8 row} cells 0x40-0x42 */
#define MAP_FIXB      (*(u8 **)0xC006)   /* {u16 col, u8 row} cells 0x43-0x45 */
#define MAP_FIXC      (*(u8 **)0xC008)   /* {u16 col|var<<14, u8 row|state<<6, u8[4]} */
#define MAP_DOORS     (*(struct door **)0xC00A)
#define MAP_PATCHES   (*(u8 **)0xC00C)
#define MAP_VIDINIT   (*(u8 **)0xC00E)
#define MAP_OBJECTS   (*(struct obj **)0xC010)
#define MAP_CAVERN    (*(u8 *)0xC012)
#define MAP_START_COL (*(u16 *)0xC013)
#define MAP_START_ROW (*(u8 *)0xC015)
#define MAP_ROW_BIAS  (*(u8 *)0xC016)
#define MAP_TEXTS     (*(u16 **)0xC017)  /* chest-message pointers (903C) */
#define MAP_STREAM_END (*(u8 **)0xC019)
#define MAP_STREAM    ((u8 *)0xC01B)

/* Tileset bank cell 0 (arena:8000) is not graphics but 8 lists (6DF3, 6BC4, 73C0, 76F6) */
#define PASSABLE_LIST   ((u8 far *)0x8000)  /* 24 entries: cell values the hero can enter (0 is in it) */
#define CONVEYOR_L_LIST ((u8 far *)0x8018)  /* 4 entries, 0-terminated: cells that push the hero left  */
#define CONVEYOR_R_LIST ((u8 far *)0x801C)  /* 4 entries: push right */
#define HAZARD_LIST     ((u8 far *)0x8020)  /* 4 entries: damaging cells (lava/spikes) */
#define UPDRAFT_LIST    ((u8 far *)0x8024)  /* 4 entries: lifts the hero 1 row/frame */
#define CURRENT_L_LIST  ((u8 far *)0x8028)  /* 4 entries: pushes 2 cells/frame left (one-way wall in cavern 7) */
#define CURRENT_R_LIST  ((u8 far *)0x802C)  /* 4 entries: pushes 2 cells/frame right */

#define LADDER(v)   ((u8)((v) - 1) < 2)     /* 6BBD: cell values 1 and 2 are ladder cells */
#define DOOR_CELL   0x4A                    /* 7A8C: DCHR cell 10 marks a door column */

/* Object (enemy/item) record: 16 bytes in the C010 table.  Tall enemies use two
 * consecutive records (+7 bit4), the second one at +0x10 for the lower half. */
struct obj {
    u16 col;        /* +0  map column; high byte 0xFF = inactive (0xFF00 written on death, 914C) */
    u8  row;        /* +2  ring row (0..63) of the sprite's top-left cell */
    u8  rcol;       /* +3  ring column, 0xFF when off screen (recomputed every frame, 8D38) */
    u8  type;       /* +4  bits0-3 class/kind; 0x10 item; 0x20 sword-immune; 0x40 no contact damage;
                            0x80 solid (blocks the hero).  0x08 = dying (handled by fight.bin) */
    u8  hit;        /* +5  bits0-4 hit source (1 sword, 2..8 magic n-1, 9 orb, 0 stomp);
                            0x20 hit this frame / stunned; 0x40 hit pending; 0x80 kept for AI */
    u8  phase;      /* +6  animation/state counter (AI); death anim counter */
    u8  flags;      /* +7  bits0-3 drop item id; 0x10 tall (2 records); 0x20 event object (+B/+D =
                            flag ptr/mask, never respawns); 0x40 clears +2 of object [+A] on death;
                            0x80 hero overlapping (pickup latch) */
    u8  hp;         /* +8  0 on spawn; the AI sets the initial value (eai1: 2) */
    u8  next;       /* +9  AI state / next type after death (0 = vanish, 0x10 = becomes item) */
    u8  link;       /* +A  linked object index / counters */
    u16 home_col;   /* +B  respawn column, 0xFFFF = never (or flag pointer when +7 bit5) */
    u8  home_row;   /* +D  (or flag mask when +7 bit5) */
    u8  home_type;  /* +E  type restored on respawn */
    u8  timer;      /* +F  respawn timer (spawn attempt when it wraps to 0) / pickup counter */
};

struct door {       /* 12-byte records in the C00A list ("signs" in ARCHITECTURE.md) */
    u16 col;        /* +0 */
    u8  row;        /* +2  map row of the door top (hero must be one row below) */
    u8  letter;     /* +3  bits0-2 letter tile, bit6 enter facing left, bit7 unlocked/passable */
    u8  dest_map;   /* +4 */
    u16 dest_col;   /* +5 */
    u8  dest_row;   /* +7  0xFF -> dest map id gets bit7 (town-type entry) */
    u8  dflags;     /* +8  bit0 needs lion key; bit7 play Roka demo */
    u8 *flag_ptr;   /* +9  byte to OR with mask when opened / traversed (0xFFFF none) */
    u8  flag_mask;  /* +B */
};

struct shot {       /* 13-byte enemy projectile (list at EB80, spawned by vec 29 / 8611) */
    u8  col;        /* +0  ring column; 0 = dead, 0xFF = end of list */
    u8  row;        /* +1  ring row */
    u8  cell;       /* +2  sprite cell; top 2 bits select anim mask table 83D7 = {0,1,3,7} */
    u8  age;        /* +3 */
    u8  life;       /* +4  dies when age >= life unless flags&0x40 */
    u8  flags;      /* +5  bits0-2 direction (0 R,1 UR,2 U,3 UL,4 L,5 DL,6 D,7 DR; table 85C2)
                            0x08 passes through walls; 0x40 scripted path (dir list at +9 indexed by age) */
    u8  damage;     /* +6 */
    u16 drawn;      /* +7  screen ptr | 0x8000 while drawn */
    u8 *script;     /* +9 */
    u8  last_col, last_row; /* +B, +C */
};

/* ======================================================================== */
/* Vector table (6000..6041): entry 0 = init, 1 = re-entry from town,        */
/* 2..32 = services exported to the per-cavern AI overlays (see FIGHT.md)    */
/* ======================================================================== */
/* 6000 vec00 6042 fight_main        6020 vec16 93C5 ai_probe_right_wide
 * 6002 vec01 79DC fight_enter_map   6022 vec17 940C ai_probe_up_wide
 * 6004 vec02 9723 ai_move8          6024 vec18 9452 ai_probe_down_left
 * 6006 vec03 973F ai_move8_alt      6026 vec19 949A ai_probe_down_right
 * 6008 vec04 91E5 ai_step_right     6028 vec20 6D6E ring_addr
 * 600A vec05 91F6 ai_step_right_up  602A vec21 6D82 ring_wrap_down
 * 600C vec06 920A ai_step_up        602C vec22 6D8E ring_wrap_up
 * 600E vec07 9222 ai_step_left_up   602E vec23 94E1 cell_passable_ai
 * 6010 vec08 9234 ai_step_left      6030 vec24 97A0 ai_on_hazard
 * 6012 vec09 9243 ai_step_left_down 6032 vec25 96D5 enemy_killed
 * 6014 vec10 9255 ai_step_down      6034 vec26 97B5 enemy_take_damage
 * 6016 vec11 926C ai_step_right_down6036 vec27 96A1 map_col_to_ring
 * 6018 vec12 92B4 ai_probe_right    6038 vec28 9851 damage_for_source
 * 601A vec13 930A ai_probe_left     603A vec29 8611 shot_spawn
 * 601C vec14 9362 ai_probe_up       603C vec30 83DB shots_clear
 * 601E vec15 939A ai_probe_down     603E vec31 98C5 find_visible_enemy
 *                                   6040 vec32 975B ai_on_current
 */

/* ======================================================================== */
/* Entry / init / main loop                                                  */
/* ======================================================================== */

/* 0x6042  vec 0.  Entered by GAME.BIN (jmp [0x6000]) after the map is at C000
 * and the level record has been applied (7E93/7EBB).  Also re-entered by
 * map transitions (7D61) and post-boss (72F1). */
void fight_main(void)
{
    cli; SP = 0x2000; sti; DS = CS;                       /* 6042 */
    ice_slide = ice_steps = walk_dir = 0;                 /* 6049 */
    shots[0].col = 0xFF; boss_state = 0xFF; magic[0].col = 0xFFFF;   /* 6058 */
    boss_cutscene = boss_dying = boss_defeated = 0;       /* 6064 */
    boss_knock_left = 0;

    if (boss_map) {                                       /* 6078: boss room intro */
        draw_status_frame();                              /* 6C55 */
        int60(AX=1);                                      /* music stop */
        music_restart = 0xFF;
        KRN_LOAD(mode 5, req = music_table_9E53[music_idx*11], ES:DI = arena:3000);   /* 608F */
        KRN_LOAD(mode 2, req = 0x9BF1 /* ENCNT.GRP */, arena:4000);                 /* 60AA */
        GF_301C(); hero_hidden = 0; GF_3016(); GF_3014(); hero_mark_screen();       /* 60BC */
        music_restart = 0; int60(AX=0, DS:SI = arena:3000);                          /* start score */
        for (i = 6; i; i--) {                             /* 60E6: 6 flashes of the encounter card */
            wait_ticks(0x41);                             /* 60EA: tick < 0x41 (65 ticks = 275 ms) */
            VID_TEXTBOX_FILL(BX=0x0C28, CX=0x3828, AL=0);
            wait_ticks(0x41);
            GF_301C();
        }
        MAP_LEVEL[4] = MAP_LEVEL[5];                      /* 6117: boss enemy bank becomes current */
        KRN_LOAD(mode 2, enemy_table_9D8D[MAP_LEVEL[5]*11], arena:4000);
        GF_CONVERT_2BPP(DS:SI = arena:4000, BP = 0xA000, CX = 0x100);
        goto boss_loop_setup;                             /* 614F -> 6150 */
    }

    VID_2012();                                           /* 6179 */
    draw_status_labels();                                 /* 6C33 */
    VID_2010(SI = MAP_VIDINIT);                           /* 6181 */
    VID_2016();
  boss_loop_setup:
    for (;;) {                                            /* 618F */
        VID_2006(); VID_2008(); VID_2014();
        if (!boss_room) break;                            /* 619E */
        /* boss room: hero walks in from the left edge under AI control */
        boss_intro = 0xFF; scroll_col = 0x29; hero_scr_col = 5;      /* 61A8 */
        ring_fill_from_stream();                          /* 6C98 */
        screen_force_redraw();                            /* 73B3 */
        do frame(); while (boss_room);                    /* 61BE: the boss AI clears [E6] when the intro ends */
        int60(AX=0, arena:3000); music_restart = 0;
        KRN_LOAD(mode 1, AH = 0x1E);                      /* 61DB: load system map #0x1E */
        boss_map = 0xFF; intro_done = 0xFF;
        level_apply(MAP_LEVEL[0]); level_load_resources();/* 7E93, 7EBB */
        VID_CONVERT_CELLS(arena:8030, CX = 0x66); GF_302A();  /* tileset cells 1..0x66 */
        GF_LOAD_HERO_ANIM(); VID_CONVERT_CELLS(CX = 0x18);
        entry_col = 0x18; entry_row = 0x0D; hero_scr_col = 0x0C; hero_home_row = 0x0C;   /* 621F */
        scroll_to_entry();                                /* 7DC1 */
        draw_status_frame();                              /* 6C55 */
      /* 6150 */
        boss_knock_left = ((u8 *)*(u16 *)0xA002)[8];
        VID_2010(SI = *(u16 *)(*(u16 *)0xA002 + 3));
        VID_200A(BX = *(u16 *)(*(u16 *)0xA002 + 3)); VID_200C();
    }

    ring_fill_from_stream();                              /* 623D */
    if (intro_done) { screen_force_redraw(); frame(); boss_intro = 0; }
    else { if (boss_map) GF_3012(); screen_force_redraw(); sprites_rebuild_markers(); }  /* 6254 */
    if (g_force_death) hero_die();                        /* 6266 */
    if (music_restart) { music_restart = 0; int60(AX=0, arena:3000); }
    btn1_edge = btn2_edge = 0; tick = 0; intro_done = 0;

    /* ---- 629C: the per-frame loop ---------------------------------------- */
    for (;;) {
        if (on_ladder) { ladder_loop(); continue; }       /* 629C -> 62DB */
        sword_input();                                    /* 6E3B */
        ice_slide_step();                                 /* 64BB */
        frame();                                          /* 6F9B: everything else + render + wait */
        magic_input();                                    /* 87B0 */
        unstick_from_wall();                              /* 63DA */
        knockback();                                      /* 6412 */
        if (++crouch_release == 2) crouching = 0;         /* 62B5 */
        dirs = int61().AL;                                /* 62C9 */
        if (dirs & DIR_DOWN) hero_flags &= ~WALKING;      /* 62CF */
        gravity();                                        /* 695A: may skip hero_input by popping its return */
        hero_input();                                     /* 6343 */
    }
}

/* 0x62DB  main loop variant while on a ladder (on_ladder != 0). */
void ladder_loop(void)
{
    do {
        crouching = 0; vstate = V_GROUND; conveyor = 0; casting = 0;      /* 62DB */
        GF_ERASE_SPRITES(); attacking = 0;
        frame(); knockback(); hero_input();                              /* 62F9 */
        if (on_ladder != 0xFF) break;                                    /* 6302: 0x80 = knocked off */
        s = hero_cell() + 1;                                             /* 630C: top-middle cell */
        if (LADDER(*s)) continue;
        s = ring_wrap_down(s + 0x24);                                    /* 6312: middle cell */
        if (LADDER(*s)) continue;
        break;
    } while (1);
    hero_flags &= ~WALKING; on_ladder = 0; btn1_edge = btn2_edge = 0;   /* 631D */
    ice_slide = ice_steps = 0; hero_anim = 0x7F;
}

/* ======================================================================== */
/* Ring buffer / cell helpers                                                 */
/* ======================================================================== */

/* 0x6D6E  vec 20.  AH = ring column, AL = ring row (&0x3F) -> DI = &ring[row][col]. */
u8 *ring_addr(u8 col, u8 row) { return (u8 *)0xE000 + (row & 0x3F) * 0x24 + col; }

/* 0x6D82  vec 21.  Wrap a ring pointer that ran past the last row. */
u8 *ring_wrap_down(u8 *p) { return p >= (u8 *)0xE900 ? p - 0x900 : p; }
/* 0x6D8E  vec 22.  Wrap a ring pointer that ran before the first row. */
u8 *ring_wrap_up(u8 *p)   { return p <  (u8 *)0xE000 ? p + 0x900 : p; }

/* 0x6DB1  Pointer to the hero's top-left cell in the ring. */
u8 *hero_cell(void)
{
    return ring_wrap_down(win + hero_scr_row * 0x24 + hero_scr_col + 4);   /* 6DB1..6DC9 */
}

/* 0x6DCB  Read a cell for collision.  Plain cell: returns CF=1, AL=value.
 * Sprite marker: returns CF=0, AL = object.type, BX = &object.  Callers use
 * `add al,al` on the result so that bit 7 (solid object) sets CF. */
u8 cell_or_object_type(u8 *p, struct obj **bx, bool *is_sprite)
{
    u8 v = *p;
    if (!(v & 0x80)) { *is_sprite = false; return v; }
    *bx = &MAP_OBJECTS[v & 0x7F];                       /* 6DD3: (v&0x7F)*0x10 + [C010] */
    *is_sprite = true; return (*bx)->type;              /* 6DDF: +4 */
}

/* 0x6DF3  Core test: is cell value AL in the 24-byte list at arena:8000?
 * Returns ZF=1 (passable) if found.  Not found: ZF=1 only if bit7 set (a sprite,
 * which is handled separately) — i.e. ZF=0 = solid.  Values 0x90/0x91 (&0x9F)
 * return "not passable" (dead code for the <0x40 callers). */
bool in_passable_list(u8 v)
{
    for (i = 0; i < 0x18; i++) if (PASSABLE_LIST[i] == v) return true;   /* 6DFA: repne scasb, CX=0x18 */
    if ((v & 0x9F) == 0x90 || (v & 0x9F) == 0x91) return false;          /* 6E07 */
    return (v & 0x80) != 0;                                               /* 6E11 */
}

/* 0x6DE5  Wall test (used for the hero's head row and vertical probes).
 * Every DCHR cell (>= 0x40: gates, items, door tops) and any sprite marker is
 * passable; tileset cells are passable iff listed in cell 0 of the bank. */
bool passable_wall(u8 v) { return v >= 0x40 ? true : in_passable_list(v); }      /* 6DE5 */

/* 0x6E1B  Floor/body test (feet rows, standing surface).  DCHR 0x40..0x48
 * (elevators and gates) are SOLID here, so the hero can stand on and is
 * blocked by them; items (>= 0x49) and sprites are passable. */
bool passable_body(u8 v)
{
    if (v >= 0x49) return true;                                           /* 6E1B */
    for (i = 0; i < 0x18; i++) if (PASSABLE_LIST[i] == v) return true;
    return (v & 0x80) != 0;                                               /* 6E36 */
}

/* 0x6DEC  Same as passable_body but for projectiles (0x49+ passable, else list). */
bool passable_shot(u8 v) { return v >= 0x49 ? true : in_passable_list(v); }

/* 0x94E1  vec 23.  AI variant: ZF=1 passable for listed cells and for sprite
 * markers; 0x49..0x7F (DCHR items) count as solid for enemies. */
bool cell_passable_ai(u8 v)
{
    if (v >= 0x49) return (v & 0x80) != 0;                                /* 94E1: sign test */
    for (i = 0; i < 0x18; i++) if (PASSABLE_LIST[i] == v) return true;
    return false;
}

/* 0x6BBD  CF=1 if the cell is a ladder (values 1 or 2). */
bool is_ladder(u8 *p) { return (u8)(*p - 1) < 2; }

/* 0x6BC4  Conveyor test.  ZF=1 and DL=2 if the cell is in the 8018 list (push
 * left), ZF=1 and DL=1 if in the 801C list (push right), else ZF=0. */
u8 conveyor_kind(u8 *p)
{
    for (q = CONVEYOR_L_LIST, n = 4; n && *q; q++, n--) if (*p == *q) return 2;   /* 6BD3 */
    for (q = CONVEYOR_R_LIST, n = 4; n && *q; q++, n--) if (*p == *q) return 1;   /* 6BEA */
    return 0;
}

/* 0x73C0  ZF=1 if the cell value is in the hazard list 8020 (4 entries, 0 ends). */
bool is_hazard(u8 v);

/* 0x76F6  Special-tile classifier for the 8024/8028/802C lists.
 * Returns ZF=1 with CL = 0 (updraft), 1 (current left), 2 (current right);
 * ZF=0 (CL=0xFF) otherwise.  AL=0 is never special. */
u8 special_tile(u8 v);

/* 0x6D9A  ZF=1 when ice physics apply: cavern 4 and shoes != 4 (Ruzeria). */
bool ice_physics(void) { return MAP_CAVERN == 4 && shoes != 4; }

/* 0x96A1  vec 27.  AX = map column -> BL = ring column (col - scroll_col, wrapped
 * by MAP_WIDTH); CF=1 if not within the 36-column ring. */
u8 map_col_to_ring(u16 col, bool *off)
{
    int d = col - scroll_col;                                             /* 96A3 */
    if (d < 0) {
        if (col > 0x23) { *off = true; return 0; }                        /* 96A9: 0x23 - col < 0 */
        d = MAP_WIDTH - scroll_col + col;                                 /* 96B1 */
    }
    *off = d > 0x23;                                                      /* 96BB: 0x23 - d borrows */
    return d;
}

/* ======================================================================== */
/* Tile stream decode + scrolling (see ARCHITECTURE.md "Tile stream")        */
/* ======================================================================== */

/* 0x6CED  Decode one run forward: SI -> byte(s); returns BL = value, BH = count,
 * SI advanced.  Jump table 6CFE by (b>>6): 6D1F/6D2F/6D47/6D4F. */
void run_decode_fwd(u8 **si, u8 *val, u8 *cnt);
/* 0x6D06  Decode one run backwards (SI points at the last byte of the run;
 * table 6D17).  Used when a column is uncovered on the left. */
void run_decode_rev(u8 **si, u8 *val, u8 *cnt);

/* 0x6D57  Decode a whole 64-row column into the ring at DI (stride 0x24). */
void column_decode(u8 **si, u8 *di)
{
    u8 total = 0;
    do {
        run_decode_fwd(si, &v, &n); total += n;                           /* 6D57 */
        while (n--) { *di = v; di += 0x24; }                              /* 6D5E */
    } while (total < 0x40);                                               /* 6D69 */
}

/* 0x6C98  Fill the whole ring for scroll_col: skip scroll_col columns of the
 * stream, decode 36 columns (wrapping at MAP_WIDTH), set stream_right/left and win. */
void ring_fill_from_stream(void)
{
    si = MAP_STREAM;                                                      /* 6C98 */
    for (c = scroll_col; c; c--) skip_column(&si);                        /* 6C9B: runs until 64 rows */
    stream_right = si;                                                    /* 6CB2 */
    di = ring; ax = scroll_col;
    for (n = 0x24; n; n--) {                                              /* 6CBC */
        column_decode(&si, di++);
        if (++ax == MAP_WIDTH) { si = MAP_STREAM; ax = 0; }               /* 6CC6: horizontal wrap */
    }
    if (ax == 0) si = MAP_STREAM_END;                                     /* 6CD3 */
    stream_left = si - 1;                                                 /* 6CDB */
    win = ring_addr(0, scroll_row);                                       /* 6CE0 */
}

/* 0x66F8  Scroll the world one column to the right (hero moves LEFT).
 * Entered from walk_left when all checks pass. */
void scroll_left(void)
{
    if (--scroll_col == 0xFFFF) { scroll_col = MAP_WIDTH - 1; stream_right = MAP_STREAM_END; }   /* 66F8 */
    memmove(ring + 1, ring, 0x8FF);                                       /* 6712: shift every row right by one cell */
    /* decode the uncovered column (map col scroll_col) backwards into ring column 0, bottom up */
    si = stream_right - 1; di = &ring[63][0]; total = 0;                  /* 6721 */
    do { run_decode_rev(&si, &v, &n); total += n; while (n--) { *di = v; di -= 0x24; } } while (total < 0x40);
    stream_right = si + 1;                                                /* 673F */
    if (scroll_col + 0x24 == MAP_WIDTH) stream_left = MAP_STREAM_END - 1; /* 6744 */
    else { si = stream_left; total = 0; do { run_decode_rev(&si, &v, &n); total += n; } while (total < 0x40); stream_left = si; }
    shots_shift(+1);                                                      /* 864E: every live shot col++ */
    /* re-mark objects that live in the new column */
    obj_index = 0;
    for (o = MAP_OBJECTS; o->col != 0xFFFF; o++, obj_index++)             /* 6776 */
        if ((o->col >> 8) != 0xFF && o->col == scroll_col)
            *ring_addr(0, o->row) = 0x80 | obj_index;                     /* 6790 */
}

/* 0x68A0  Scroll the world one column to the left (hero moves RIGHT). */
void scroll_right(void)
{
    scroll_col++;                                                         /* 68A0 */
    if (scroll_col + 0x23 == MAP_WIDTH) stream_left = MAP_STREAM - 1;     /* 68A4 */
    memmove(ring, ring + 1, 0x8FF);                                       /* 68B6 */
    si = stream_left + 1; column_decode(&si, &ring[0][0x23]); stream_left = si - 1;   /* 68C3: new column 35 */
    if (scroll_col == MAP_WIDTH) { scroll_col = 0; stream_right = MAP_STREAM; }        /* 68D3 */
    else { si = stream_right; skip_column_fwd(&si); stream_right = si; }               /* 68E7 */
    shots_shift(-1);                                                      /* 8639 */
    obj_index = 0; bx = scroll_col + 0x23; if (bx >= MAP_WIDTH) bx -= MAP_WIDTH;        /* 6904 */
    for (o = MAP_OBJECTS; o->col != 0xFFFF; o++, obj_index++)
        if ((o->col >> 8) != 0xFF && o->col == bx) *ring_addr(0x23, o->row) = 0x80 | obj_index;   /* 692A */
}

/* 0x6621  Scroll the world down one row (hero moves UP). */
void scroll_up(void)   { scroll_row--; win = ring_wrap_up(win - 0x24); }
/* 0x6B2E  Scroll the world up one row (hero moves DOWN). */
void scroll_down(void) { scroll_row++; win = ring_wrap_down(win + 0x24); }

/* ======================================================================== */
/* Hero input dispatch                                                        */
/* ======================================================================== */
#define DIR_UP 1
#define DIR_DOWN 2
#define DIR_LEFT 4
#define DIR_RIGHT 8

/* 0x6343  Called once per frame after gravity().  Reads INT 61h: AL = direction
 * bits (up=1 down=2 left=4 right=8), AH = buttons (bit0 sword, bit1 magic). */
void hero_input(void)
{
    walk_dir = 0;                                                         /* 6343 */
    dirs = int61().AL;
    if (dirs == DIR_UP | DIR_LEFT)  { jump_left();  return; }             /* 634A: 5 */
    if (dirs == DIR_UP | DIR_RIGHT) { jump_right(); return; }             /* 6351: 9 */
    if (dirs == DIR_UP)             { jump_up();    return; }             /* 6358: 1 */

    if (!on_ladder && vstate != V_GROUND) {                               /* 6361: airborne */
        if (!diag_jump) { stop_rising(); return; }                        /* 636F -> 65BA */
        diag_jump = 0;
        if (!(hero_flags & WALKING)) { stop_rising(); return; }
        /* one more step of the diagonal jump, then start falling */
        if (hero_flags & FACE_LEFT) walk_left(); else walk_right();       /* 638C */
        stop_rising(); return;
    }
    /* grounded (or on ladder): facing change ends an ice slide run */
    f = hero_flags & FACE_LEFT;
    if (f != prev_facing) ice_slide_start();                              /* 639A..63A8 */
    prev_facing = f;
    if (dirs == DIR_DOWN) down_pressed();                                 /* 63AF */
    if ((dirs & 0xC) == DIR_LEFT)  { walk_left();  return; }              /* 63B7 */
    if ((dirs & 0xC) == DIR_RIGHT) { walk_right(); return; }
    ice_slide_start();                                                    /* 63C7 */
    if (!on_ladder && !crouching) hero_anim = 0x80;                       /* 63CA: idle pose */
}

/* 0x65BA  Rising ends -> free fall. */
void stop_rising(void) { conveyor = 0; vstate = V_FALL; }

/* 0x663E  Walk left one cell (8 px).  Turns first if facing right. */
void walk_left(void)
{
    regen_tick = 0;                                                       /* 663E */
    if (!(hero_flags & FACE_LEFT)) { turn_around(); return; }             /* 6643 -> 6824 */
    if (crouching) return;                                                /* 664D */
    if (conveyor == 1) { stop_walking(); return; }                        /* 6655: can't walk against a right-conveyor */
    if (try_move_left()) { stop_walking(); return; }                      /* 665F: blocked (CF=1) */
    walk_dir = 2;                                                         /* 6667 */
    if (on_ladder) return;
    if (ice_physics() && !ice_slide) { slide_dir = 0; ice_steps++; }      /* 6674 */
    hero_flags |= WALKING;                                                /* 6689 */
    if (vstate == V_GROUND) { hero_anim = (hero_anim + 1) & 0x7F; door_msg_latch = 0; }   /* 668E */
}

/* 0x67C6  Walk right one cell.  Mirror of walk_left. */
void walk_right(void)
{
    regen_tick = 0;
    if (hero_flags & FACE_LEFT) { turn_around(); return; }                /* 67CB */
    if (crouching) return;
    if (conveyor == 2) { stop_walking(); return; }                        /* 67DA */
    if (try_move_right()) { stop_walking(); return; }
    walk_dir = 1;                                                         /* 67E6 */
    if (on_ladder) return;
    if (ice_physics() && !ice_slide) { slide_dir = 1; ice_steps++; }      /* 67F3 */
    hero_flags |= WALKING;
    if (vstate == V_GROUND) { hero_anim = (hero_anim + 1) & 0x7F; door_msg_latch = 0; }
}

/* 0x6824  Flip facing; idle pose unless on a ladder. */
void turn_around(void) { hero_flags ^= FACE_LEFT; if (!on_ladder) hero_anim = 0x80; }
/* 0x6837 */
void stop_walking(void) { hero_flags &= ~WALKING; if (!on_ladder && vstate == V_GROUND) hero_anim = 0x80; }

/* 0x66A5  Collision test + move for one cell to the left.  Returns CF=1 when
 * blocked.  The hero's solid body is its MIDDLE column, so the destination
 * column is the hero's own left column (hero_cell col+0).
 *   1. ring col-1, rows -1..+2 : a sprite whose object.type has bit7 blocks    (66B0)
 *   2. col+0 row 0 (head)     : passable_wall(), skipped while crouching       (66CD)
 *      cavern 7: a CURRENT_R cell here is a one-way wall (67A3)
 *   3. col+0 rows +1,+2       : passable_body()                                 (66DC)
 *   4. otherwise scroll_left() (the hero stays on the same screen column).      (66F8) */
bool try_move_left(void)
{
    tl = hero_cell();
    s = ring_wrap_up(tl - 0x24) - 1;                                      /* 66AA: row-1, col-1 */
    for (n = 4; n; n--) {                                                 /* 66B1 */
        t = cell_or_object_type(s, &o, &spr);
        if (spr && (t & 0x80)) return true;                               /* 66B7: add al,al -> CF */
        s = ring_wrap_down(s + 0x24);
    }
    s = tl;                                                               /* 66C4 */
    if (!crouching) {
        if (!passable_wall(*s)) return true;                              /* 66CF */
        if (current_blocks_left(s)) return true;                          /* 67A3 */
    }
    for (n = 2; n; n--) {                                                 /* 66DC */
        s = ring_wrap_down(s + 0x24);
        if (!passable_body(*s)) return true;                              /* 66E7 */
        if (current_blocks_left(s)) return true;                          /* 66EF */
    }
    scroll_left(); return false;                                          /* 66F8 (falls through, CF=0) */
}

/* 0x684C  Mirror: destination column is the hero's own right column (col+2),
 * sprites tested at col+2 rows -1..+2, then col+2 rows 0..2. */
bool try_move_right(void)
{
    tl = hero_cell() + 2;                                                 /* 684F */
    s = ring_wrap_up(tl - 0x24);                                          /* 6853: row-1, col+2 */
    for (n = 4; n; n--) { t = cell_or_object_type(s, &o, &spr); if (spr && (t & 0x80)) return true; s = ring_wrap_down(s + 0x24); }
    s = tl;
    if (!crouching) { if (!passable_wall(*s)) return true; if (current_blocks_right(s)) return true; }   /* 6875, 6942 */
    for (n = 2; n; n--) { s = ring_wrap_down(s + 0x24); if (!passable_body(*s)) return true; if (current_blocks_right(s)) return true; }
    scroll_right(); return false;                                         /* 68A0 */
}

/* 0x67A3 / 0x6942  In cavern 7 the CURRENT_R (8028 list, CL==2) / CURRENT_L
 * (802C list, CL==1) cells act as one-way walls.  CF=1 = blocked. */
bool current_blocks_left(u8 *s)  { return MAP_CAVERN == 7 && special_tile(*s) == 2; }   /* 67A3..67BB */
bool current_blocks_right(u8 *s) { return MAP_CAVERN == 7 && special_tile(*s) == 1; }   /* 6942..6959 */

/* 0x6537  "up": doors, elevators, ladders, then jump. */
void jump_up(void)
{
    regen_tick = 0;                                                       /* 6537 */
    door_check();                                                         /* 7A83: may not return (map change) */
    elevator_up();                                                        /* 8074 */
    ladder_mount();                                                       /* 65C5: may walk/climb instead */
    rise();                                                               /* 6545 */
}
/* 0x6634 */ void jump_left(void)  { diag_jump = 0xFF; rise(); walk_left(); }
/* 0x67BC */ void jump_right(void) { diag_jump = 0xFF; rise(); walk_right(); }

/* 0x6545  One frame of jump rise: 1 row while "up" is held, at most max_rise
 * rows (2; 4 with the Feruza shoes).  Blocked by a solid cell above the head. */
void rise(void)
{
    if (++ice_slide > 9) ice_slide = 10;                                  /* 6545: (ice) jumping reloads the slide */
    if (on_ladder) return;
    crouching = 0;                                                        /* 655D */
    if (rise_rows < max_rise) {                                           /* 6562 */
        s = ring_wrap_up(hero_cell() - 0x23);                             /* 656E: row-1, col+1 (above head centre) */
        if (passable_wall(*s)) {
            hero_anim = 0; hero_flags &= ~WALKING; vstate = V_RISE;       /* 657B */
            conveyor_kick = max_rise >> 1;                                /* 658A */
            rise_rows++;                                                  /* 6592 */
            if (hero_scr_row < 7) scroll_up(); else hero_scr_row--;       /* 6596: scroll the world while the hero is above row 7 */
            return;
        }
        if (rise_rows == 0) { if (!on_ladder) hero_anim = 0x80; return; } /* 65A5: couldn't jump at all */
    }
    stop_rising();                                                        /* 65BA: max height / blocked -> fall */
}

/* 0x65C5  Ladder handling on "up".  Top-middle cell a ladder -> mount and climb;
 * otherwise if the ladder is one column to the side, step toward it. */
void ladder_mount(void)
{
    tl = hero_cell();
    if (is_ladder(tl + 1)) {                                              /* 65C8 */
        on_ladder = 0xFF; crouching = 0;                                  /* 65EF */
        for (;;) {                                                        /* 65F9: climb 1 row, 2 if hero_anim becomes even */
            s = ring_wrap_up(hero_cell() - 0x23); hero_anim--;            /* 6602 */
            if (!is_ladder(s)) { hero_anim |= 1; return; }                /* 660B */
            scroll_up(); frame();                                         /* 6611 */
            if (hero_anim & 1) return;
        }
    }
    if (is_ladder(tl)) { if (hero_flags & FACE_LEFT) walk_left(); return; }         /* 65CE */
    if (is_ladder(tl + 2)) { if (!(hero_flags & FACE_LEFT)) walk_right(); return; } /* 65DC */
}

/* 0x6AC9  "down": elevator down, stomp, ladder descent, crouch. */
void down_pressed(void)
{
    regen_tick = 0;
    if (conveyor) return;                                                 /* 6ACE */
    elevator_down();                                                      /* 7FDC: may scroll_down and return to caller's caller */
    s = ring_wrap_down(hero_cell() + 0x6D);                               /* 6ADC: row+3, col+1 (below the feet) */
    if (is_ladder(s)) {                                                   /* 6AE2 */
        for (;;) {                                                        /* 6B04: descend 1-2 rows this frame */
            s = ring_wrap_down(hero_cell() + 0x6D); hero_anim++;
            if (!passable_wall(*s)) { hero_anim |= 1; return; }           /* 6B13 */
            scroll_down(); frame();                                       /* 6B1E */
            if (hero_anim & 1) return;
        }
    }
    if (on_ladder) { on_ladder = V_KNOCK; vstate = V_KNOCK; return; }     /* 6AE7: let go of the ladder */
    crouch_release = 0; crouching = 0xFF;                                 /* 6AF9 */
}

/* 0x63DA  Wall unstick, runs the frame after landing (vstate==0, not crouching).
 * If the hero's head row is inside solid cells, push toward the open side. */
void unstick_from_wall(void)
{
    if (crouching || vstate != V_GROUND) return;
    tl = hero_cell();
    if (passable_wall(tl[0])) return;                                     /* 63ED */
    if (passable_wall(tl[2])) return;                                     /* 63F7 */
    s = ring_wrap_down(tl + 2 + 0x24);                                    /* 63FF: row+1, col+2 */
    if (passable_wall(*s)) scroll_right(); else scroll_left();            /* 6407 */
}

/* ======================================================================== */
/* Gravity / falling / landing                                                */
/* ======================================================================== */

/* 0x6B76  Floor test.  CF=1 = standing.  Cells tested are on row+3 (below the
 * 3-row body): col+1 (centre) and col+0.  A solid sprite (type bit7) under
 * either counts as floor.  A moving hero (hero_anim != 0x80) does not fall
 * into a one-cell gap: if col+1 below is open but BOTH col+0 and col+2 below
 * are solid he keeps standing. */
bool floor_under_hero(void)
{
    c = ring_wrap_down(hero_cell() + 0x6D);                               /* 6B79: row+3, col+1 */
    t = cell_or_object_type(c, &o, &spr);     if (spr && (t & 0x80)) return true;     /* 6B81 */
    t = cell_or_object_type(c - 1, &o, &spr); if (spr && (t & 0x80)) return true;     /* 6B8A */
    if (!passable_body(c[0])) return true;                                /* 6B96 */
    if (hero_anim == 0x80) return false;                                  /* 6B9D: idle -> fall */
    if (passable_body(c[-1])) return false;                               /* 6BA7 */
    return !passable_body(c[1]);                                          /* 6BB4 */
}

/* 0x695A  Vertical physics, once per frame.  Fall speed is 1 row (8 px) per
 * frame, no acceleration.  While falling the input handler is skipped (the
 * routine pops its own return address, 698D), except for the "air control"
 * below. */
void gravity(void)
{
    if (on_updraft) return;                                               /* 695A */
    if (vstate & 0x80) return;                                            /* 6962: rising or knocked: no gravity */
    elevator_ride();                                                      /* 818E: standing on a moving elevator follows it */
    conveyor_check();                                                     /* 6A67 */
    if (floor_under_hero()) { land(); return; }                           /* 6970 -> 6B41 */

    fall_rows++;                                                          /* 6978 */
    skip_hero_input_this_frame();                                         /* 698D: pop ax */
    if (rise_rows) { rise_rows--; hero_scr_row++; }                       /* 6984: undo the jump's on-screen rise first */
    else scroll_down();                                                   /* 6990 */
    if (!(hero_flags & WALKING)) {
        s = ring_wrap_down(hero_cell() + 0x49);                           /* 699D: row+2, col+1 (bottom-centre) */
        if (is_ladder(s)) { on_ladder = 0xFF; return; }                   /* 69A8: falling onto a ladder grabs it */
    }
    hero_anim = 0x80; was = vstate; vstate = V_FALL;                      /* 69AE */
    if (conveyor || hero_dead) return;
    if (was == V_GROUND) {                                                /* 69CB: just walked off an edge: one more step */
        if (hero_flags & FACE_LEFT) walk_left(); else walk_right();
        hero_flags &= ~WALKING; return;                                   /* 69E0 */
    }
    /* air control (69E6) */
    d = int61().AL & 0xC;
    if (d == DIR_LEFT  && !(hero_flags & FACE_LEFT)) { hero_flags &= ~WALKING; turn_around(); ledge_step_right(); return; }   /* 6A0F..6A1E */
    if (d == DIR_RIGHT &&  (hero_flags & FACE_LEFT)) { hero_flags &= ~WALKING; turn_around(); ledge_step_left();  return; }   /* 6A3B..6A4A */
    if (hero_flags & WALKING) { if (hero_flags & FACE_LEFT) walk_left(); else walk_right(); return; }   /* 69F2: keep moving */
    if (d == DIR_LEFT)  ledge_step_left();                                /* 69F9 */
    if (d == DIR_RIGHT) ledge_step_right();
}

/* 0x6A1E  Mid-air step right only onto a ledge: cell below-centre open and
 * below-right solid (both with passable_wall). */
void ledge_step_right(void)
{
    s = ring_wrap_down(hero_cell() + 0x6D);
    if (!passable_wall(s[0])) return; if (passable_wall(s[1])) return; try_move_right();
}
/* 0x6A4A */
void ledge_step_left(void)
{
    s = ring_wrap_down(hero_cell() + 0x6D);
    if (!passable_wall(s[0])) return; if (passable_wall(s[-1])) return; try_move_left();
}

/* 0x6B41  Landing.  Only acts on the frame the hero was airborne (vstate==0x7F). */
void land(void)
{
    if (vstate != V_FALL) return;                                         /* 6B41 */
    skip_hero_input_this_frame();                                         /* 6B49: pop ax */
    fell = fall_rows; vstate = V_GROUND; crouch_release = 0; fall_rows = 0; hero_anim = 0x80;
    if (conveyor) return;
    if (fell >= 2) crouching = 0xFF;                                      /* 6B6A: landing crouch after a 2+ row fall */
}

/* 0x6A67  Conveyor belts (8018/801C lists) under the bottom-centre cell (row+2,col+1). */
void conveyor_check(void)
{
    conveyor = 0;
    s = ring_wrap_down(hero_cell() + 0x49);                               /* 6A6F */
    k = conveyor_kind(s); if (!k) return;
    hero_flags &= ~WALKING; conveyor = k;                                 /* 6A7B */
    if (conveyor_kick) {                                                  /* 6A84: right after a jump: 1 cell per frame */
        if (shoes == 3) return;                                           /* 6AB0: Silkarn shoes */
        conveyor_kick--;
        if (k == 1) try_move_right(); else try_move_left(); return;
    }
    if ((conveyor_phase++ & 3) != 0) return;                              /* 6A8B: otherwise 1 cell every 4 frames */
    d = int61().AL;
    if (k == 1) { if (!(d & DIR_LEFT))  try_move_right(); }               /* 6AA8: walking against it cancels the push */
    else        { if (!(d & DIR_RIGHT)) try_move_left();  }
}

/* 0x64BB  Ice slide (cavern 4 without Ruzeria shoes): after a run of N steps
 * the hero keeps sliding N/2 (max 10) cells in the same direction, one per
 * frame, unless standing on a fixture (0x40..0x48) or already walking that way. */
void ice_slide_step(void)
{
    if (!ice_physics() || vstate != V_GROUND || !ice_slide) return;       /* 64BB */
    ice_slide--;
    s = ring_wrap_down(hero_cell() + 0x6D);                               /* 64D5: row+3, col+1 */
    if (*s >= 0x40 && *s < 0x49) { ice_slide = 0; return; }              /* 64E0 */
    if (slide_dir & 1) { if (walk_dir != 1) try_move_right(); }           /* 64F1 */
    else               { if (walk_dir != 2) try_move_left();  }
}
/* 0x6508 */
void ice_slide_start(void)
{
    if (!ice_physics() || ice_slide || on_ladder) return;
    n = ice_steps >> 1; if (!n) return; if (n > 9) n = 10;                /* 651E..652C */
    ice_slide = n; ice_steps = 0;
}

/* ======================================================================== */
/* Sword                                                                      */
/* ======================================================================== */

/* 0x6E3B  Sword input, once per frame before frame().  Needs a sword. */
void sword_input(void)
{
    if (!sword) return;                                                   /* 6E3B */
    in = int61();
    if ((in.AH & 1) && vstate != V_GROUND && !conveyor && (in.AL & DIR_DOWN)) {   /* 6E45: held button + down while airborne */
        attack_type = 2; attack_var = 2;                                  /* 6E5C: down-thrust */
        if (!thrust_latch) { thrust_latch = 0xFF; sfx_request = 4; }
        goto start;
    }
    thrust_latch = 0;
    if (!btn1_edge || attacking || casting) return;                       /* 6E81..6E96 */
    if (!boss_map) {
        /* look for a hittable enemy in the 4x8 block above the head:
         * rows -4..-1, ring cols (hero col-3)..(hero col+4) */
        s = ring_wrap_up(hero_cell() - 0x93); found = 0;                  /* 6EA0: 0x93 = 4*0x24 + 3 */
        for (r = 4; r; r--) {
            for (c = 8; c; c--, s++) {
                t = cell_or_object_type(s, &o, &spr);
                if (spr && !(t & 0x60) && !(o->flags & 0x10)) found = 0xFF;   /* 6EB4..6EC3 */
            }
            s = ring_wrap_down(s + 0x1C);
        }
        if (found) { attack_type = 1; attack_var = 0; goto snd; }         /* 6ED2 -> 6EDC: upward slash */
    }
    if (int61().AL & DIR_UP) { attack_type = 1; attack_var = 0; }         /* 6ED6 */
    else { attack_type = 0; attack_var = 0; }                             /* 6EE8 */
  snd:
    sfx_request = 3;                                                      /* 6EF2 */
  start:
    btn1_edge = btn2_edge = 0; attacking = 0xFF;                          /* 6EF7 */
}

/* 0x6F07  Apply the sword to enemies, once per rendered frame while attacking.
 * The blade shape comes from the sword block cached at arena:B000 (kernel
 * mode 4, block # = sword level, 8FA2): 15 pointers at B000, each to a list
 * of cell steps (bytes, 0xFF ends) walked from an origin 4 rows above the
 * hero's top-left (3 rows when crouching).  Every sprite reached whose type
 * has no bit5 and that is not already stunned gets hit source 1. */
void sword_apply(void)
{
    if (!attacking) return;
    if (boss_map && boss_cutscene) return;                                /* 6F0F */
    s = ring_wrap_up(hero_cell() - (crouching ? 0x6C : 0x90));            /* 6F21..6F30 */
    f = (hero_flags & FACE_LEFT) << 4;                                    /* 6F33: 0x10 when facing left */
    if (attack_type == 0)      idx = (attack_var | f) + 0;                /* 6F57 */
    else if (attack_type == 1) idx = (attack_var | f) + 6;                /* 6F4B */
    else                       idx = f + 10;                              /* 6F51 */
    idx &= 0xFE;                                                          /* 6F5E */
    list = *(u8 far **)(arena + 0xB002 + idx);                            /* 6F69 */
    for (; *list != 0xFF; list++) {
        s = ring_wrap_down(s + *list);                                    /* 6F79 */
        t = cell_or_object_type(s, &o, &spr);
        if (!spr || (t & 0x20) || (o->hit & 0x20)) continue;             /* 6F81..6F8B */
        o->hit = (o->hit & 0xE0) | 0x40 | 1;                              /* 6F8D: hit pending, source 1 = sword */
    }
}

/* ======================================================================== */
/* Damage to the hero                                                         */
/* ======================================================================== */

/* 0x7685  hp -= AX (floor 0), redraw. */
void hero_damage(u16 dmg) { hp = hp > dmg ? hp - dmg : 0; VID_HP_DISPLAY(); }

/* 0x75E2  Shielded damage: with a shield, damage = dmg/2 >> ((shield+1)/2)
 * (shield 1-2: /4, 3-4: /8, 5-6: /16 ...) and the shield loses that much
 * durability; at 0 it breaks (761A: shield=0, "Shield broken."). */
void hero_damage_shielded(u16 dmg)
{
    if (!shield) { hero_damage(dmg); sfx_request = 9; return; }           /* 75E2 -> 7611 */
    dmg = (dmg >> 1) >> ((shield + 1) >> 1);                              /* 75E9..75F3 */
    if (shield_hp <= dmg) { shield_break(); shield_hp = 0; }              /* 75F5..7607 */
    else shield_hp -= dmg;
    hero_damage(dmg); sfx_request = 8;                                    /* 7608 */
}

/* 0x751F  Hero vs enemy contact, once per frame.  Scans 4 columns
 * (ring col-1 .. col+2) x 3 rows (-1, 0, +1; rows 0..+1 when crouching) for
 * sprite markers whose object.type lacks bit6.  Each contact adds the AI's
 * contact damage table entry [A010 + (type & 0xF)] to contact_damage and sets
 * hit_side[i].  Then the total is applied twice: for the left pair of columns
 * through the shield only when facing left, for the right pair only when
 * facing right (75BA/75CE). */
void hero_enemy_contact(void)
{
    if (boss_map && boss_cutscene) return;
    contact_damage = 0;
    s = hero_cell() - 1;                                                  /* 7537 */
    probe = probe_3rows;                                                  /* 7651 when crouching (2 rows) */
    if (!crouching) { probe = probe_3rows_from_above; s = ring_wrap_up(s - 0x24); }   /* 7545 */
    for (i = 0; i < 4; i++, s++) {                                        /* four calls 7551/7564/757C/7591 */
        hit_side[i] = probe(s) ? 0xFF : 0;
        if (hit_side[i]) (i < 2 ? apply_contact_left : apply_contact_right)();
    }
    hero_hit = hero_hit_flash = hit_side[0] | hit_side[1] | hit_side[2] | hit_side[3];   /* 759C */
    if (hero_hit) VID_HIT_FLASH();                                        /* 75B4 */
}
/* 0x763E / 0x7651 / 0x765E  probe rows downward; a sprite with !(type & 0x40)
 * counts: contact_damage += A010[type & 0xF] (7675), CF=1. */
/* 0x75BA */ void apply_contact_left(void)  { if (hero_dead) return; if (hero_flags & FACE_LEFT) hero_damage_shielded(contact_damage); else { hero_damage(contact_damage); sfx_request = 9; } }
/* 0x75CE */ void apply_contact_right(void) { if (hero_dead) return; if (!(hero_flags & FACE_LEFT)) hero_damage_shielded(contact_damage); else { hero_damage(contact_damage); sfx_request = 9; } }

/* 0x6412  Knockback after a hit (hero_hit set by contact or a projectile):
 * 2 cells away from the hit side (both sides: in the facing direction; boss
 * rooms with boss_knock_left: always left).  Being hit on a ladder drops the
 * hero off it.  Then one row of fall if there is no floor. */
void knockback(void)
{
    if (!hero_hit) return;
    if (boss_knock_left) goto left;                                       /* 641A */
    l = hit_side[0] | hit_side[1]; r = hit_side[2] | hit_side[3];         /* 6424 */
    if (l & r) { if (hero_flags & FACE_LEFT) goto left; goto right; }     /* 642F */
    if (l) goto right;                                                    /* 643C */
  left:
    if (on_ladder) { hero_flags = (hero_flags & ~3) | FACE_LEFT; vstate = V_FALL; btn1_edge = 0; }   /* 6440 */
    try_move_left(); try_move_left(); goto tail;                          /* 645B */
  right:
    if (on_ladder) { hero_flags &= ~3; vstate = V_FALL; btn1_edge = 0; }  /* 6463 */
    try_move_right(); try_move_right();
  tail:
    if (on_ladder) { on_ladder = V_KNOCK; vstate = V_GROUND; }            /* 6481 */
    if (on_updraft || (vstate & 0x80)) return;
    if (floor_under_hero()) return;                                       /* 64A2 */
    if (rise_rows) { rise_rows--; hero_scr_row++; } else scroll_down();   /* 64A8 */
}

/* 0x74A0  Hazard tiles: any of the hero's 3x3 cells (2x3 when crouching) in
 * the 8020 list, plus the cell below the feet centre when not on a ladder,
 * deals hazard_damage[cavern-1] per frame.  Pirika shoes (2) are immune. */
const u8 hazard_damage[9] = { 1, 1, 4, 8, 20, 20, 20, 20, 20 };          /* 7516 */
void hazard_check(void)
{
    if (shoes == 2) return;                                               /* 74A0 */
    on_hazard = 0; s = hero_cell(); rows = 3;
    if (crouching) { s = ring_wrap_down(s + 0x24); rows--; }              /* 74B3 */
    for (; rows; rows--) { for (c = 3; c; c--) if (is_hazard(*s++)) on_hazard = 0xFF; s = ring_wrap_down(s + 0x21); }
    if (!on_ladder && is_hazard(s[1])) on_hazard = 0xFF;                  /* 74DF: row+3, col+1 */
    if (!on_hazard) return;
    hero_hit_flash = 0xFF; sfx_request = 9;
    hero_damage(hazard_damage[MAP_CAVERN - 1]);                           /* 7505 */
}

/* 0x7699  Updraft / current tiles on the hero's centre column (rows +2, +1, 0). */
void special_tiles_check(void)
{
    on_updraft = 0;
    s = ring_wrap_down(hero_cell() + 0x49);                               /* 76A1 */
    for (n = 3; n; n--, s = ring_wrap_up(s - 0x24)) {
        k = special_tile(*s); if (k == 0xFF) continue;
        /* 76C2: pops two return addresses -> returns to frame()'s caller of 7699 */
        switch (k) {
        case 0: scroll_up(); on_updraft = 0xFF; vstate = V_GROUND; hero_anim = 0x80; return;   /* 76D4 */
        case 1: try_move_left();  try_move_left();  return;               /* 76F0 */
        case 2: try_move_right(); try_move_right(); return;               /* 76EA */
        }
    }
}

/* ======================================================================== */
/* The frame: simulation of everything but the hero, render, wait            */
/* ======================================================================== */

/* 0x6F9B  One rendered frame.  Also used as a sub-step by the ladder/climb
 * loops and cutscenes. */
void frame(void)
{
    max_rise = (shoes == 1) ? 4 : 2;                                      /* 6F9B..6FA6 */
    special_tiles_check();                                                /* 7699 */
    if (vstate == V_GROUND) {                                             /* 6FAC: vertical re-centering */
        rise_rows = 0;
        if (hero_home_row != hero_scr_row) {
            if (hero_home_row < hero_scr_row) { scroll_down(); hero_scr_row--; }   /* 6FCC */
            else                              { scroll_up();   hero_scr_row++; }   /* 6FC3 */
        }
    }
    if (boss_room || boss_map) {                                          /* 6FD3: horizontal re-centering to the AI's column */
        if (hero_scr_col != ((u8 *)*(u16 *)0xA002)[7]) { try_move_right(); hero_scr_col--; }
    } else if (hero_scr_col != 0x0C) { try_move_left(); hero_scr_col++; } /* 6FF9: hero normally sits on screen column 12 */
    hero_map_row = (hero_scr_row + scroll_row) & 0x3F;                    /* 7007 */
    entrance_proximity();                                                 /* 774E */
    fixtures_c_draw(); elevators_draw(); fixtures_b_draw(); signs_draw(); /* 81AE 7FB1 8163 78DD */
    magic_effect_update();                                                /* 8AAD */
    if (!boss_defeated) enemies_update();                                 /* 8D19 */
    hero_hit_flash = 0; hero_hit = 0;
    hero_enemy_contact();                                                 /* 751F */
    GF_ERASE_SPRITES();
    shots_update();                                                       /* 8422 */
    orbs_update();                                                        /* 86FC */
    GF_FLUSH();
    hazard_check();                                                       /* 74A0 */
    if (MAP_CAVERN == 7 && shoes != 5 && ((++heat_timer & 0x3F) == 0)) {  /* 704F: every 64 frames */
        hero_hit_flash = 0xFF; sfx_request = 9; hero_damage(0x0F); message(0x9BB9 /* "It's too hot !!" */);
    }
    message_boxes_tick();                                                 /* 7210 */
    if (hero_dead) hero_hit_flash = 0; else hero_hidden = 0;              /* 7081 */
    anim_flag = 0;                                                        /* 7094: renderer parameters */
    if (attacking)    { anim_flag = 0xFF; anim_kind = attack_type; anim_arg = attack_var; }
    else if (casting) { anim_flag = 0xFF; anim_arg = cast_timer; anim_kind = 1; }
    if (!hero_hidden) hero_mark_screen();                                 /* 747C */
    GF_DRAW_HERO();
    if (!hero_dead && hp_regen_pending) {                                 /* 70D9: potion effect */
        hp_regen_pending--; hp += 8; if (hp > max_hp) { hp = max_hp; hp_regen_pending = 0; }   /* 70EB */
        sfx_request = 0x13; VID_HP_DISPLAY();
    }
    GF_DRAW_BG();
    if (boss_dying) { GF_BOSS_DEATH_FX(); music_fade = 10; }
    while (tick < 2 * speed) ;                                            /* 7125: first half of the frame time */
    orbs_update(); GF_ERASE_SPRITES(); shots_draw(); orbs_draw(); magic_draw(); sword_apply(); GF_FLUSH();   /* 7133..7147 */
    do {                                                                  /* 714C: rest of the frame; kernel idle services */
        KRN_IDLE_0(); KRN_IDLE_1(); KRN_IDLE_2(); KRN_IDLE_3(); KRN_IDLE_4();
        if (KRN_QUIT_QUERY()) abort_to_town();                            /* 716E -> 78D7 */
    } while (tick < 4 * speed);
    tick = 0;                                                             /* 717F */
    if (hero_dead) return;
    if (!g_immortal && hp == 0) { hero_die(); return; }                   /* 718C */
    if (++regen_tick >= 0x10) { regen_tick = 0; if (hp < max_hp) { hp += 2; VID_HP_DISPLAY(); } }   /* 719E: idle regen */
    if (post_boss_pending) { post_boss_transition(); return; }            /* 71C2 -> 72F1 */
    if (boss_map && boss_defeated && boss_state == 0xFF) {                /* 71CC: boss rewards */
        info = (u8 *)*(u16 *)0xA002; exp_add(*(u16 *)(info + 5)); gold_add(*(u16 *)(info + 9)); post_boss_pending = 0xFF;
    }
    if (boss_cutscene) return;
    if (!(input_mask_FF18 & 1)) { menu_debounce = 0; return; }            /* 7202: menu key */
    if (menu_debounce || casting || magic_active || boss_intro) return;   /* 7275 */
    sfx_request = 0x0B; VID_2002();
    swap_C000_A000(); (*(void(*)())0xA000)(); swap_C000_A000();           /* 728C: run the item menu overlay cached at arena:C000 */
    if (menu_result == 8) { return_to_town(); return; }                   /* 729C */
    VID_2002(); GF_LOAD_HERO_ANIM(); VID_CONVERT_CELLS(CX = 0x18); menu_debounce = 0xFF;
    screen_force_redraw(); btn1_edge = btn2_edge = 0; msg_box_a = msg_box_b = 0; frame();
}

/* 0x747C  Mark the hero's 3x3 cells in the screen copy with 0xFF. */
void hero_mark_screen(void) { p = &screen[hero_scr_row][hero_scr_col]; for (r = 0; r < 3; r++, p += 0x1C) p[0] = p[1] = p[2] = 0xFF; }
/* 0x73B3 */ void screen_force_redraw(void) { memset(screen, 0xFD, 0x214); }
/* 0x72D9 */ void swap_C000_A000(void) { swap 0x800 words between arena:C000 and BASE:A000; }

/* 0x774E  Proximity to the map's start position -> snd_proximity (table 77C7
 * squares-ish {0,1,4,9,16,25,36,49,64,81,100,121,144,169,196,225}, then
 * 77D7 maps the sum to a volume 0x0F..). */
void entrance_proximity(void);

/* ======================================================================== */
/* Enemies (C010 object table) and the AI overlay                             */
/* ======================================================================== */

/* 0x8D19  Per-frame enemy update.  In boss rooms (boss_map or boss_room) the
 * boss AI's entry [A000] is called once and does everything itself. */
void enemies_update(void)
{
    if (boss_map || boss_room) { (*(void(*)())0xA000)(); return; }        /* 8D1D */
    obj_index = 0;
    for (o = MAP_OBJECTS; o->col != 0xFFFF; o++, obj_index++) {           /* 8D30 */
        o->rcol = 0xFF;
        if ((o->col >> 8) != 0xFF) {
            r = map_col_to_ring(o->col, &off);
            if (!off) {
                o->rcol = r;
                enemy_step(o);                                            /* 8DAE */
                if ((o->col >> 8) != 0xFF) {                              /* still alive: place the marker */
                    p = ring_addr(o->rcol, o->row);
                    under_sprite[obj_index] = *p; *p = 0x80 | obj_index;  /* 8D62 */
                    if (!(o->type & 0x11) && (o->flags & 0x10)) {         /* tall enemy: second marker 2 rows down */
                        p = ring_wrap_down(p + 0x48); under_sprite[obj_index + 1] = *p; *p = 0x80 | (obj_index + 1);
                    }
                }
            }
        }
        if (!(o->flags & 0x20)) { if (++o->timer == 0) enemy_spawn(o); }  /* 8D90: respawn timer */
    }
}

/* 0x8DAE  One object: restore the cell under its marker, expose the hit flag
 * to the AI, then either call the AI overlay (live enemies) or run the
 * built-in dying/item state machine. */
void enemy_step(struct obj *o)
{
    p = ring_addr(o->rcol, o->row);                                       /* 8DAE */
    h = o->hit & ~0x20;
    if (h & 0x40) { if (!(o->type & 0x20)) h |= 0x20; h &= ~0x40; }       /* 8DB9: pending hit -> "hit" unless immune */
    o->hit = h;
    *p = under_sprite[obj_index];                                         /* 8DCA */
    if (!(o->type & 0x11) && (o->flags & 0x10)) *ring_wrap_down(p + 0x48) = under_sprite[obj_index + 1];
    if (!(o->type & 0x18)) { AI_ENTRY(SI = o, DI = p, obj_index); return; }   /* 8DF7: jmp [cs:A000] */
    st = (o->type & 0x1F) - 0x10;
    if (st < 0) { enemy_dying(o); return; }                               /* 8E0B -> 90E6 */
    item_state[st](o);                                                    /* 8E10: table 8E14 */
}

/* 0x90E6  Dying: phase advances every other frame; at 3 the object becomes
 * its drop (type = 0x70 | id, flags 0x80, timer 4) or vanishes (914C). */
void enemy_dying(struct obj *o)
{
    if (!((o->phase += 0x80) carried)) return;                            /* 90E6 */
    if (++o->phase != 3) return;
    o->timer = 0;
    if (o->flags & 0x40) { o->flags &= ~0x40; MAP_OBJECTS[o->link].row = 0; }   /* 90FB */
    if ((o->flags & 0x10) && !(o->type & 1)) { enemy_remove(o); return; } /* 9116: lower half of a tall enemy */
    o->phase = 0; o->type = 0x72;                                         /* 9122 */
    id = o->flags & 0xF;
    if (id == 0) return;                                                  /* stays as a 0x72 "corpse" item */
    if (id == 1) { enemy_remove(o); return; }
    o->type = 0x70 | id; o->flags |= 0x80; o->timer = 4; o->hit &= 0x80; o->flags &= 0xF0;   /* 9136 */
}

/* 0x914C  Remove: col = 0xFF00; event objects OR their flag byte. */
void enemy_remove(struct obj *o)
{
    o->col = 0xFF00;
    if ((o->flags & 0x20) && o->home_col != 0xFFFF) { *(u8 *)o->home_col |= o->home_row; o->home_col = 0xFFFF; }
}

/* 0x94FF  Respawn attempt (timer wrapped).  Needs: inactive, home_col set,
 * home column inside the ring but not at ring col 0/35, and NOT in view:
 * either its row is more than 24 rows below the window top or its ring col is
 * outside 3..0x1F.  The 3x3 (tall: 5x3) cells around it must hold no sprite. */
void enemy_spawn(struct obj *o)
{
    if ((o->col >> 8) != 0xFF) return;
    if ((o->flags & 0x10) && (o[1].col >> 8) != 0xFF) return;             /* 9506 */
    if (o->home_col == 0xFFFF) return;
    r = map_col_to_ring(o->home_col, &off); if (off || r == 0 || r == 0x23) return;   /* 951C..952A */
    dy = (o->home_row - (scroll_row - 2)) & 0x3F;                         /* 952D */
    if (dy < 0x18 && r >= 3 && r < 0x20) return;                          /* 953B: would appear on screen */
    if (!(o->flags & 0x10)) {
        o->rcol = r; p = ring_addr(r, o->home_row);
        if (any_sprite_in_block(p - 0x25, 3 rows, 3 cols)) return;        /* 955C..957C */
        *p = 0x80 | obj_index;
        o->col = o->home_col; o->row = o->home_row; o->type = o->home_type;
        o->phase = 0x10; o->hit = 0; o->next = 0; o->link = 0; o->hp = 0; under_sprite[obj_index] = 0;   /* 9599 */
        return;
    }
    if (o->home_type & 1) return;                                         /* 95B6 */
    /* tall enemy: two records, second marker 2 rows lower, 5x3 block must be free */
    ...                                                                   /* 95BD..9658 */
}

/* 0x96C1  Kill with EXP: exp += [A008 + (type & 0xF)] for non-items, then 96D5. */
/* 0x96D5  vec 25.  Enemy killed: phase 0, type |= 0x68 (dying + immune),
 * hit &= 0x80; tall second half too; sound 7 if within 19 rows of the window. */
void enemy_killed(struct obj *o)
{
    o->phase = 0; o->type |= 0x68; o->hit &= 0x80;                        /* 96D5 */
    if ((o->flags & 0x10) && !(o->type & 1)) { o->phase = 0x80; o[1].phase = 0; o[1].type |= 0x68; o[1].hit &= 0x80; }
    if (((o->row - (scroll_row - 1)) & 0x3F) < 0x13) sfx_request = 7;    /* 96FD */
}

/* 0x97B5  vec 26.  Enemy takes the pending hit: damage from the hit source. */
void enemy_take_damage(struct obj *o)
{
    d = damage_for_source(o->hit & 0x1F);                                 /* 97B5..97BA */
    if (o->hp > d) { o->hp -= d; sfx_request = 6; return; }               /* 97BD */
    /* killed: roll a drop unless one is already assigned */
    f = (!(o->type & 1) && (o->flags & 0x10)) ? &o[1].flags : &o->flags;  /* 97CD: tall enemies keep it in the 2nd record */
    if (!(*f & 0xF)) {
        tbl = ((u16 far *)*(u16 *)0xA006)[o->type & 7];                   /* 97E2: per-class 4-entry drop list */
        r = KRN_RANDOM() & 3; if (attack_type == 2) r = 0;                /* 97F2..9803: down-thrust always gives entry 0 */
        *f = (*f & 0xF0) | tbl[r];
    }
    kill_with_exp(o);                                                     /* 96C1 */
}

/* 0x9851  vec 28.  AL = hit source -> AH = damage.
 *   0 (stomp)   : level/2 + 1                                              (9851)
 *   1 (sword)   : sword_base[sword-1] + level/2, x (attack_bonus+1), cap 255,
 *                 x2 for a down-thrust (attack_type 2), cap 255            (9882..98B7)
 *   9 (orb)     : min(255, (level+1)*4)                                    (9862..9876)
 *   2..8 (magic): magic_base[src-2]                                        (9877) */
const u8 sword_base[6] = { 1, 2, 4, 8, 0x20, 0x7F };                      /* 98B8 */
const u8 magic_base[7] = { 2, 4, 8, 0x10, 0x20, 0x40, 0xFF };             /* 98BE */
u8 damage_for_source(u8 src)
{
    u8 ah = (hero_level >> 1) + 1;
    if (src == 0) return ah;
    if (src != 1) {
        ah = (hero_level + 1) * 4; if (overflow) ah = 0xFF;
        return src == 9 ? ah : magic_base[src - 2];
    }
    d = sword_base[sword - 1] + (hero_level >> 1); if (carry) return 0xFF;
    d *= attack_bonus + 1; if (d > 0xFF) d = 0xFF;
    if (attack_type == 2) { d *= 2; if (d > 0xFF) d = 0xFF; }
    return d;
}

/* 0x9715 */ void exp_add(u16 n)  { exp  = sat_add(exp, n); }
/* 0x917C */ void gold_add(u16 n) { gold = sat_add(gold, n); VID_GOLD_DISPLAY(); }

/* 0x9190  Pickup overlap test: CF=0 when the hero's top row is within rows
 * home-2..home+1 of the object and the hero's ring column within rcol-2..rcol+1
 * (a 2x2 sprite overlapping the 3x3 hero).  flags bit7 latches "already
 * overlapping": repeat pickups only every 8 frames. */
bool hero_overlaps_item(struct obj *o);

/* Item states (type 0x10..0x1F), table 8E14.  All use 9190 for the touch test. */
void item_state_0x10(o) /* 8E32 */ { /* enemy corpse fading: after 4 half-speed phases -> next type (0 vanish, 0x10 item) */ }
void item_state_0x11(o) /* 8E8D */ { /* touch-triggered: fires when the hero is exactly 3 rows below and within 2 cols */ }
void item_state_0x12(o) /* 8EE9 */ { /* 3-frame flash then remove */ }
void item_state_0x13(o) /* 8EF6 */ { /* treasure box: phase&0xF selects 50/100/-/500/1000 gold (8F33) or the drop table */ }
void item_state_0x14_15(o) /* 8FAB */ { /* coin: class 4 -> 1 gold, 5 -> 10, else 100; sound 0x10 */ }
void item_state_0x16(o) /* 8FE8 */ { keys++;      /* "You get a Key." */ }
void item_state_0x17(o) /* 8FF8 */ { lion_keys++; /* "Get the lion's head Key." */ }
void item_state_0x18(o) /* 9008 */ { hp_regen_pending += 10;             /* "You have recovered." = +80 HP */ }
void item_state_0x19(o) /* 901C */ { hp_regen_pending += max_hp / 8 + 1; /* "You have recovered full." */ }
void item_state_0x1A(o) /* 909D */ { /* shoes by cavern: 4 -> Ruzeria(4), 5 -> Pirika(2), 6 -> Silkarn(3); table 90CA */ }
void item_state_0x1B(o) /* 9090 */ { inventory_add(1); /* Feruza shoes */ }
void item_state_0x1D(o) /* 903C */ { /* boss chest: shows the message from MAP_TEXTS[phase], sound 0x11 */ }
void item_state_0x1E(o) /* 907F */ { hero_crest = 0xFF; }

/* ======================================================================== */
/* AI overlay services (vectors 2..19, 23, 24, 32).  All take SI = object.     */
/* ======================================================================== */

/* 0x927F  col+1 (wrap), rcol+1.   0x9293  col-1, rcol-1.
 * 0x92A4  row+1 & 63.             0x92AC  row-1 & 63. */

/* 0x91E5  vec 4.  Step right if rcol < 0x22 and the 3 cells right of a 2x2
 * sprite (92B4: (row, rcol+2), (row+1, rcol+2), then the column above) are
 * passable.  CF=1 = blocked.  The probes (92B4/930A/9362/939A/93C5/940C/
 * 9452/949A, vectors 12..19) test cell_passable_ai() on the cells adjacent to
 * the sprite in the given direction; in cavern 5 CURRENT tiles also block
 * (92EB/9341). */
bool ai_step_right(o)      { if (o->rcol >= 0x22) return true; if (ai_probe_right(o)) return true; o->col++; o->rcol++; return false; }
bool ai_step_right_up(o)   { /* 91F6 */ ... o->col++; o->rcol++; o->row--; }
bool ai_step_up(o)         { /* 920A: rcol must be 1..0x22 */ o->row--; }
bool ai_step_left_up(o)    { /* 9222: rcol >= 2 */ o->col--; o->rcol--; o->row--; }
bool ai_step_left(o)       { /* 9234 */ o->col--; o->rcol--; }
bool ai_step_left_down(o)  { /* 9243 */ o->col--; o->rcol--; o->row++; }
bool ai_step_down(o)       { /* 9255 */ o->row++; }
bool ai_step_right_down(o) { /* 926C */ o->col++; o->rcol++; o->row++; }

/* 0x9723  vec 2 / 0x973F vec 3: AL & 7 -> one of the 8 steps above (tables 972F/974B). */
/* 0x97A0  vec 24: ZF=1 if the cell under the sprite (row+2, rcol) is a hazard tile. */
/* 0x975B  vec 32: current tiles under the sprite -> jumps to 9788 table. */
/* 0x98C5  vec 31: find the first object with home_col == 0xFFFF that is on
 * screen (or row 0x7F), returns DL = index, CF=0. */

/* 0x8611  vec 29.  Append the 13-byte shot template at BX to the EB80 list
 * (max 31 live shots). */
void shot_spawn(struct shot *tmpl)
{
    if (projectile_count >= 0x1F) return;                                 /* 8611 */
    for (s = shots; s->col != 0xFF; s++) ;
    *s = *tmpl; (s + 1)->col = 0xFF; projectile_count++;
}

/* 0x8422  Move every live shot (also compacts the list).  age++, then 846F:
 * scripted or fixed direction step (85A5), wall test unless flags&8 (6DEC),
 * then hero hit test. */
void shots_update(void)
{
    projectile_count = 0; dst = shots;
    for (s = shots; ; s++) {
        if (!s->col && !(s->drawn & 0x8000)) continue;                    /* 842F: dead and erased */
        if (s->col == 0xFF) { dst->col = 0xFF; return; }
        s->age++; shot_move_and_hit(s);                                   /* 8444..8449 */
        *dst++ = *s;                                                      /* 844F: compact */
        if (!(s->flags & 0x40) && s->age >= s->life) s->col = 0;          /* 8455 */
        projectile_count++;
    }
}

/* 0x846F  One shot: step, wall kill, then hit the hero if it is on one of
 * the hero's rows (top/mid/bottom; mid/bottom when crouching) and on the
 * hero's ring column +0/+1 (facing right) or +1/+2 (facing left). */
void shot_move_and_hit(struct shot *s)
{
    shot_step(s);                                                         /* 85A5 */
    if (!(s->flags & 8)) { if (!s->col) return; if (!passable_shot(*ring_addr(s->col, s->row))) { s->col = 0; return; } }   /* 8472..848C */
    r = scroll_row + hero_scr_row; n = 2;
    if (!crouching && ((r & 0x3F) == s->row)) goto rows_ok;              /* 8490 */
    for (; n; n--) { r = (r + 1) & 0x3F; if (r == s->row) goto rows_ok; }
    return;
  rows_ok:
    c = hero_scr_col + 4 + ((hero_flags & FACE_LEFT) ? 1 : 0);            /* 84B4 */
    if (s->col != c && s->col != c + 1) return;                           /* 84C2 */
    s->col = 0;                                                           /* 84CD: consumed */
    d = s->flags & 7;
    if (shield && !attacking && !on_ladder && d != 2 && d != 6) {         /* 84D0: shield only vs horizontal-ish shots */
        from_left = (d == 0 || d == 1 || d == 7);
        if (from_left == ((hero_flags & FACE_LEFT) != 0)) {               /* facing the shot */
            if (shield >= 4) { sfx_request = 0x0A; return; }             /* 854F: full shield */
            mid = (hero_scr_row + scroll_row + 1 + (crouching ? 1 : 0));  /* 8556 */
            /* 8573: row must match mid (dir 0/4), mid-1 (dir 1..3), mid+1 (dir 5..7) */
            if (shield_row_match(d, mid, s->row)) { sfx_request = 0x0A; return; }
        }
    }
    hero_damage(s->damage); sfx_request = 9; hero_hit = hero_hit_flash = 0xFF;   /* 850E */
    if (d == 2 || d == 6)           { hit_side01 = hit_side23 = 0xFFFF; } /* 8523: vertical: both sides */
    else if (d == 0 || d == 1 || d == 7) { hit_side01 = 0xFFFF; hit_side23 = 0; }   /* from the left -> push right */
    else                            { hit_side01 = 0; hit_side23 = 0xFFFF; }
}

/* 0x8366  Draw shots inside the window (rcol 4..0x1F, row within 18 of scroll_row). */
/* 0x83DB  vec 30: erase and clear the whole shot list. */
/* 0x8639 / 0x864E  shift every live shot one column left/right (on scroll). */

/* ======================================================================== */
/* Magic                                                                      */
/* ======================================================================== */

/* 0x87B0  Magic button (btn2_edge) starts a cast (casting=0xFF, cast_timer);
 * 2 frames later, if magic_count[sel-1] > 0, it is consumed, sound 0x18,
 * magic_active=0xFF and the spell's spawn routine (table 883F by magic_sel)
 * fills the EB15 records (884D: single bolt in facing direction; 88A8: 4
 * falling sprites; 88F8: 3-sprite spread).  Cast ends at cast_timer 6. */
void magic_input(void);
/* 0x8AAD  Per-frame spell effect by magic_sel (table 8AC6): bolts move 2 cells
 * per frame in their facing (8BD0), live 5/10/12 frames, and hit every enemy
 * in the 3x3 block around them (8BF7/8C4F: hit source = magic_sel+1).
 * 8918: screen-wide spell hits every sprite in the window. */
void magic_effect_update(void);
/* 0x896E / 0x8A37  draw / erase the 2x2 magic sprites (cell tables 8C81/8C8D). */

/* 0x86FC  Orbiting spheres (EB60): phase = (phase + speed) & 15, position =
 * hero + orbit_table[phase] (8790), hits an enemy under the orb (8741:
 * source 9, hits-- ). */
void orbs_update(void);

/* ======================================================================== */
/* Doors, elevators, fixtures, level records, transitions                     */
/* ======================================================================== */

/* 0x7A83  On "up": a DOOR_CELL (0x4A) one row above the hero's top-left
 * enters the door found in the C00A list at (map col, hero_map_row-1);
 * 0x4A above the left/right cell steps sideways to align. */
void door_check(void)
{
    s = ring_wrap_up(hero_cell() - 0x25);                                 /* 7A86: row-1, col-1 */
    if (s[0] == DOOR_CELL) { if (hero_flags & FACE_LEFT) { pop_return(); walk_left(); } return; }
    if (s[2] == DOOR_CELL) { if (!(hero_flags & FACE_LEFT)) { pop_return(); walk_right(); } return; }
    if (s[1] != DOOR_CELL) return;
    col = scroll_col + hero_scr_col + 4; if (col > MAP_WIDTH - 1) col = ~((MAP_WIDTH - 1) - col);   /* 7AB6 */
    row = (hero_scr_row - 1 + scroll_row) & 0x3F;
    for (d = MAP_DOORS; d->col != 0xFFFF; d++) if (d->col == col && d->row == row) break;
    if (d->col == 0xFFFF) return;
    pop_return();                                                         /* 7AF6: skip the rest of jump_up */
    if (!(d->letter & 0x80)) {                                            /* locked */
        if (door_unlock(d)) { hero_anim = 0x80; ice_steps = 0; }          /* 7E15: uses a key / lion key, sound 0x15, sets bit7 and the flag */
        else if (!door_msg_latch) { door_msg_latch = 0xFF; sfx_request = 0x16; message("Can't open this door."); }
        return;
    }
    if (d->flag_ptr != (u8 *)0xFFFF) *d->flag_ptr |= d->flag_mask;        /* 7B25 */
    map_transition(d);                                                    /* 7B32.. */
}

/* 0x7B32  Leave through a door: clear sprites, fade, then load the new map
 * (cur_map = dest_map | 0x80 if dest_row == 0xFF), apply patches, place the
 * hero at entry_col/entry_row (7DC1: scroll_col = entry_col-16, scroll_row =
 * entry_row+1-row_bias), reload tileset/AI/enemy bank as needed (7EBB), and
 * walk the hero in from the door side (7C6E: 26 animation frames), then jump
 * back to fight_main (7D61) or return to the town engine (7D82: vector 6000
 * of the town overlay via 7D85). */
void map_transition(struct door *d);

/* 0x7DC1 */ void scroll_to_entry(void) { scroll_col = entry_col - 0x10; if ((int)scroll_col < 0) scroll_col += MAP_WIDTH; scroll_row = (entry_row + 1 - MAP_ROW_BIAS) & 0x3F; }
/* 0x7DE1  Town return: scroll_col for entry_col (clamps at the map ends), BL = hero col. */

/* 0x7E93  Apply level record byte 0: music_idx = (flags>>1)&0xF (restart music
 * if it changed, music_fade = 10), copy 4 bytes to 9EF6.. */
/* 0x7EBB  Load level resources: gfx (9C2D table, arena:4000 unless flags bit0),
 * tileset (9C43 table -> arena:8000) if it changed, AI (9CBC -> BASE:A000 raw),
 * enemies (9D8D -> arena:4000 + GF_CONVERT_2BPP CX=0x100), music (9E53). */
/* 0x6BFC  Conditional pokes from the C00C patch list: {u8* p, u8 mask, {u16 addr, u16 val}* , 0xFFFF}* ;
 * when (*p & mask) != 0 every addr = val. */
/* 0x7FB1 / 0x8163 / 0x81AE  write fixture cells (0x40..0x42 / 0x43..0x45 / 0x46..0x48 +variant)
 * into the ring each frame for records inside the window (82F8/831F: ring col
 * = 0x21 - (col - scroll_col)); 8352 writes under_sprite[] instead when the
 * cell holds a sprite marker. */
/* 0x82B4  Is the hero standing on fixture record [SI]?  AL = the fixture's ring
 * column (from 82F8).  Tests the hero's ring column + 4, + 5, + 6 against it:
 *   82E2: mov dl,[0x83] / add dl,0x4 / mov cx,3
 *   82EC: cmp dl,al / clc / jnz 82F2 / ret        (CF = 0: not this one)
 *   82F2: inc dl / loop 82EC / stc                (CF = 1: carried)
 * With [83] pinned at 0x0C by 6FF9 those are platform ring columns 0x11/0x10/0x0F —
 * exactly the three placements in which one of the platform's cells is ring column
 * [83]+5, the cell 6D6E+0x6D tests for ground.  In map terms the hero is carried iff
 *   hero_map_col + 1  is within [fixture_col, fixture_col + 2]
 * — note the +1: reading his own column instead walks him off the leading end of a
 * platform moving right. */
static u8 hero_on_fixture(struct fixture *f)      /* 82B4 */
{
    u8 dl = hero_scr_col + 4;                     /* 82E2 */
    for (int i = 0; i < 3; i++, dl++)             /* 82E9, 82F2 */
        if (dl == fixture_ring_col(f)) return 1;  /* 82EC -> 82F6 stc */
    return 0;                                     /* 82EF clc */
}

/* 0x7FDC / 0x8074  Elevators (fixture A under the feet, 814C matches 0x40..0x42):
 * "down" moves the record one row down if the 3 cells below are empty and
 * scrolls the hero with it; "up" moves it up if the cells above are empty.
 * 0x818E  each frame: if standing on fixture A, follow it (scroll_down when it moved). */
/* 0x78DD  Signs: draws the 4x5 letter tiles (string 79B4) for door records in view. */

/* 0x98FC  Death: 3-frame animation cycle every 8 frames until hero_anim==2,
 * blink 30 frames, then: exp += 127 - 2*level, gold /= 2, lifetime gold
 * counter zeroed, hp = max_hp, return to town (99E0: cur_map = town_map,
 * entry = MAP_START_COL, hero col 2, jump to the town overlay entry 6002). */
void hero_die(void);
/* 0x78D7  Quit key: town overlay entry 601C. */
/* 0x72F1  Post-boss: reload AI/enemies from level record +6/+7, apply the
 * {ptr,val} pokes from +8, put the exit door at scroll_col+hero_col (+9 when
 * `test byte [si-5]` — the ring cell 5 columns left of the hero — is non-zero;
 * NOT the poke list's last byte).  The list's last poke sets the room's
 * **boss-defeated story flag** in the player page — every boss room has one
 * ([00] [08] [10] [18] [20] [28] [30] [32] [47]) — which is what the room's own
 * C00C patch list (6BFC) keys off to restage it for the reward visit (docs/FIGHT.md).
 * Restart fight_main. */
/* 0x79DC  vec 1: entry from the town (re-init locals, load hero bank FMAN.GRP
 * 9BE6 -> arena:6000, GF_CONVERT_2BPP SI=6333 BP=D000 CX=0xE6, tileset, walk-in). */
