# Global state page (BASE:FF00-FF7F) and fight.bin locals

R/W columns name the code that reads/writes. "kernel" = STICK.BIN, "vid" =
GM*.BIN, "gf" = gfmcga (ZELRES2[6]), "fight" = fight.bin, "AI" = eai*/boss
overlays. Addresses in parentheses are the fight.bin instructions that
established the meaning. *uncertain* = inferred from a single use.

## FF00 page

| Off | Size | Name | Meaning | Written by | Read by |
|---|---|---|---|---|---|
| FF00 | 4 | loader_svc | far ptr to ZELIARD.EXE services | loader | kernel |
| FF04 | 4 | old_int8 | BIOS INT 8 vector | loader | kernel `02B7` |
| FF08 | 1 | snd_proximity | entrance-distance volume 0..0x0F from tables `77C7/77D7`; 0 when >15 cells away | fight `774E`, `7E5B`, `99E0` | sound driver (*uncertain*) |
| FF0A | 1 | joystick_enable | gates joystick polling in INT 61h (`0610`) | loader | kernel |
| FF0C | 4 | sfx_driver | far ptr called every tick | loader | kernel `025A` |
| FF10 | 4 | music_driver | far ptr called every tick | loader | kernel `025F` |
| FF14 | 1 | video_mode | 0 EGA … 4 MCGA | loader | GAME.BIN |
| FF15 | 1 | joystick/mt32 variant | selects blob in mode-5 loads | loader | kernel |
| FF16 | 1 | kbd_buttons | current button state (bit0 sword, bit1 magic) | kernel INT 9 | INT 61h → AH |
| FF17 | 1 | kbd_dirs | current direction state (1 up 2 down 4 left 8 right) | kernel INT 9 | INT 61h → AL |
| FF18 | 2 | key_mask | bitmask of function/menu keys (bit0 menu `7202`, bit14 quit `092D`, bit15 speed prompt `07D2`) | kernel | fight, kernel |
| FF1A | 1 | tick | +1 per 236.7 Hz tick; fight zeroes it at frame end (`717F`) | kernel `027A`, fight | fight `7125`,`7179`,`60EF` |
| FF1B | 2 | frame_counter | +1 per tick, never reset | kernel `0284` | kernel `[11A]` (random source for the AI) |
| FF1D | 1 | btn1_edge | sword button pressed since last clear | kernel `0138`,`01AA` | fight `6E81`, cleared `6EF7` |
| FF1E | 1 | btn2_edge | magic button edge | kernel `0165`,`01D0` | fight `87BF` |
| FF1F | 2 | timer_hook | near vector called every tick when FF20 != 0 | ? | kernel `0296` |
| FF20 | 1 | timer_hook_on | | ? | kernel |
| FF24 | 1 | music_fade | set to 10 at level start / boss death, 8 on hero death (`7BC9`,`7120`,`998B`) | fight | music driver (*uncertain*) |
| FF2C | 2 | arena_seg | segment of the 64 KB data arena | loader | everyone |
| FF2E | 1 | boss_cutscene | boss intro/hero frozen: disables contact, sword, magic hits | AI (boss) | fight `6F16`,`7526`,`71FA`,`8BFE` |
| FF2F | 1 | boss_dying | play boss death fx `[3010]` | AI (boss) | fight `7114` |
| FF30 | 1 | boss_defeated | skip enemy pass; with `EDA0==FF` award EXP/gold | AI (boss) | fight `7025`,`71D3` |
| FF31 | 2 | win | ring pointer of screen row 0, col 0 | fight `6CE8`,`6621`,`6B2E` | fight `6DB1`, gf `3042`,`3DDF` |
| FF33 | 1 | speed | frame = 4×speed ticks; default 5 | loader `0173`, kernel `07F9` | fight `7125`,`714C`,`7F82` |
| FF34 | 1 | boss_map | level flags bit 7 (boss room map) | fight `7D0D`,`61E4` | fight (many) |
| FF35 | 1 | hero_map_row | `(hero_scr_row + scroll_row) & 63` | fight `7010` | fight `774E`,`8E9C`,`91A7`, gf `39BF`, AI |
| FF36 | 1 | hero_hit_flash | hero took damage this frame | fight `7505`,`851B`,`75AD` | gf `3A95` |
| FF37 | 1 | hero_hidden | hero not drawn (death blink, door walk) | fight, gf `40F8` | fight `70CA` |
| FF38 | 1 | crouching | 0xFF while crouched | fight `6AFE`,`6B70` | fight |
| FF39 | 1 | on_ladder | 0xFF on ladder, 0x80 just knocked/dropped off | fight `65EF`,`69A8`,`6488` | fight |
| FF3A | 1 | hero_entering | hero invisible before the walk-in | fight `7E5B`,`7C02` | gf `3AB5`,`3B83` |
| FF3B | 1 | joystick_present | | loader | kernel `017C`,`060C` |
| FF3C | 1 | casting | magic cast in progress | fight `87E6`,`8805` | fight, gf |
| FF3D | 1 | vstate | 0 grounded, 0xFF rising, 0x7F airborne/falling, 0x80 knocked | fight `6585`,`65BF`,`6B4E`,`6AF3` | fight |
| FF3E | 1 | magic_active | spell sprites live | fight `882C`,`8BBC` | fight |
| FF3F | 1 | anim_arg | renderer arg: attack_var or cast_timer | fight `70AB`,`70BF` | gf `3ADA` |
| FF40 | 1 | anim_flag | renderer: special hero pose active | fight `7094` | gf `3ACF` |
| FF41 | 1 | anim_kind | renderer: attack_type or 1 = casting | fight `70A8`,`70C5` | gf `3B00` |
| FF42 | 1 | conveyor | 0 none, 1 pushes right, 2 pushes left | fight `6A80`,`6A67` | fight |
| FF43 | 1 | attacking | sword swing active | fight `6F01`; cleared gf `3F1A` | fight `6F07`,`6E89`, gf `3E04` |
| FF44 | 1 | sword_frame | renderer's swing-drawn flag | gf `3FD8`,`3F3C` | gf |
| FF45 | 1 | attack_type | 0 slash, 1 upward, 2 down-thrust | fight `6E5C`,`6EDC`,`6EE8` | fight `6F42`,`97FC`,`98A8`, gf `3E49`, AI |
| FF46 | 1 | attack_var | 0/2 at start; renderer increments per frame (`3E45`) → selects the blade shape (`6F57`) | fight, gf | fight, gf |
| FF47 | 1 | thrust_latch | down-thrust sound played | fight `6E70`,`6E7C` | fight |
| FF48 | 1 | joy_dirs | INT 61h scratch: joystick directions | kernel `0600` | kernel |
| FF49 | 1 | joy_btns | INT 61h scratch: joystick buttons | kernel `06A4` | kernel |
| FF4A | 1 | obj_index | index of the object being processed (passed to the AI) | fight `8D2B`,`8DA5` | fight, AI |
| FF4B | 1 | menu_result | 8 = warp to town after the item menu | menu overlay | fight `729C` |
| FF50 | 2 | tick_total | +1 per tick | kernel `027F` | ? |
| FF6C | 8 | player_name | save-file name (`NAME.USR`, kenjpro A862); name entry town.bin | town/kenjpro | (was mis-named music_drv_name) |
| FF75 | 1 | sfx_request | sound-effect id: 3 swing, 4 thrust, 6 enemy hit, 7 enemy dies, 8 shield block (dmg), 9 hero hurt, 0xA shield block (shot), 0xB menu, 0x10 coin, 0x11 item, 0x12 corpse, 0x13 potion tick, 0x14 pickup, 0x15 door unlock, 0x16 locked door, 0x17 cast start, 0x18 cast, 0x19 spell end, **0x1A footstep, 0x1B flash, 0x1C flight sparkle** (rokademo `A06B`/`A158`/`A22D`, docs/CUTSCENES.md §5) | fight, kernel `07FC`, rokademo | sound driver |
| FF79 | 4 | old_int9 | | loader | kernel |

## fight.bin locals (BASE:9EED-9F2D)

| Off | Size | Name | Meaning |
|---|---|---|---|
| 9EED/9EEE | 1+1 | msg_timer_a/b | message box close timers (32 frames, `7250`/`721D`) |
| 9EEF/9EF0 | 1+1 | msg_box_a/b | message box open flags |
| 9EF1 | 1 | msg_box_rows | rows of the multi-line box (`7418`) |
| 9EF2/9EF4 | 2+1 | msg_x/msg_y | text cursor for `740E` |
| 9EF5 | 1 | menu_debounce | |
| 9EF6..9EFB | 6 | level_copy | copy of the level record (flags, gfx, tileset, ai, enemies, end) (`7E93`) |
| 9EFE/9EFF | 1+1 | loaded_tiles/loaded_enemies | resource indices currently in arena:8000 / 4000 (`7EBB`) |
| 9F00 | 1 | hero_home_row | screen row the world scrolls to keep the hero on (= `[C016]`) |
| 9F01 | 1 | boss_knock_left | from `[[A002]+8]`: knockback always left |
| 9F02 | 1 | music_restart | |
| 9F03 | 2 | stream_right | tile-stream pointer of the column after ring col 35 |
| 9F05 | 2 | stream_left | pointer of the column before ring col 0 (reverse decode) |
| 9F07 | 1 | fixture_anim | fixture-C animation counter |
| 9F08 | 1 | fall_rows | rows fallen (≥2 → landing crouch) |
| 9F09 | 1 | rise_rows | rows risen in the current jump (undone before scrolling on the way down) |
| 9F0A | 1 | crouch_release | crouch ends when it reaches 2 |
| 9F0B | 1 | diag_jump | diagonal jump in progress |
| 9F0C | 1 | conveyor_kick | frames of full-speed conveyor push after a jump |
| 9F0D | 1 | max_rise | 2, or 4 with Feruza shoes |
| 9F0E..9F11 | 4 | hit_side[] | contact flags for ring cols −1,0,+1,+2 → knockback direction |
| 9F12 | 2 | contact_damage | accumulated contact damage this frame |
| 9F14 | 1 | hero_hit | knockback trigger |
| 9F15 | 1 | on_updraft | disables gravity/knockback |
| 9F16 | 1 | conveyor_phase | |
| 9F17 | 1 | on_hazard | |
| 9F18 | 1 | regen_tick | idle frames; +2 HP at 16 |
| 9F19 | 1 | door_msg_latch | |
| 9F1A/9F1C | 2+1 | entry_col/entry_row | destination of a map transition |
| 9F1D | 1 | entry_flags | bit 7: run the Roka demo overlay first |
| 9F1E | 1 | post_boss_pending | |
| 9F1F | 1 | projectile_count | live EB80 shots (max 31) |
| 9F20 | 1 | ice_slide | remaining slide cells |
| 9F21 | 1 | ice_steps | steps walked in one direction |
| 9F22 | 1 | walk_dir | 1 right, 2 left this frame |
| 9F23 | 1 | slide_dir | bit0 = right |
| 9F24 | 1 | prev_facing | |
| 9F25 | 1 | heat_timer | cavern 7 |
| 9F26 | 1 | boss_intro | |
| 9F27 | 1 | intro_done | |
| 9F28/9F29 | 1+1 | death_anim | |
| 9F2A | 1 | magic_hit_any | |
| 9F2B | 1 | cast_timer | +2 per frame; spell fires at 4, cast ends at 6 |
| 9F2C/9F2D | 1+1 | dist_col/dist_row | distance to the map start position |

## Story flags (BASE:0000-0048)

The bytes below the player record are a **story-flag array**, written by `7B25` (passing
through an unlocked door), `7E39`, `914C` (removing an event object) and `72F1` (the last
poke of a boss room's list), and read by every `[C00C]` patch list and by door records'
`flag_ptr`.  The boss-defeated flags live at `[00] [08] [10] [18] [20] [28] [30] [32] [47]`
(one per boss room); `[03] |= 0x20` is "cavern 1 cleared".  A `[C00C]` list is how a room
restages itself between visits — see docs/FIGHT.md on the boss reward.

## Player record (BASE:0049-00E8, from STDPLY.BIN)

| Off | Size | Name | Default | Notes |
|---|---|---|---|---|
| 49 | 1 | force_death | 0 | != 0 → die at loop start (`6266`); *uncertain* |
| 7F | 1 | immortal | 0 | != 0 → HP 0 does not kill (`718C`); *uncertain* |
| 80 | 2 | scroll_col | 0x1E | map column in ring column 0 |
| 82 | 1 | scroll_row | 0 | ring row on screen row 0 |
| 83 | 1 | hero_scr_col | 0x0A (0x0C in caverns) | |
| 84 | 1 | hero_scr_row | 0x0A | |
| 85/86 | 1+2 | gold | 0 | gold (24-bit, `916B`, halved on death) — TOWN.md; fight.c calls this `gold_total` |
| 8B | 2 | almas | 0 | almas (bank exchanges almas→gold, TOWN.md); fight.c calls this `gold` |
| 8D | 1 | level | 0 | strength term of the damage formulas; *name uncertain* |
| 8E | 2 | exp | 0 | |
| 90 | 2 | hp | 0x50 | |
| 92 | 1 | sword | 1 | 1..6; Enchantment sword = 6 (`8F8F`) |
| 93 | 1 | shield | 0 | |
| 94 | 2 | shield_hp | 0 | durability |
| 98 | 1 | keys | 0 | |
| 99 | 1 | lion_keys | 0 | |
| 9B | 1 | glory_crest | 0 | |
| 9C | 1 | hero_crest | 0 | |
| 9D | 1 | magic_sel | 0 | 1..7 |
| 9E | 1 | worn_key_item | 0 | the **worn** key item, selected in select.bin's WEAR row (`[A1..A5]` is the bag): 1 Feruza shoes (jump 4) 2 Pirika (hazard immune) 3 Silkarn (no conveyor kick) 4 Ruzeria (no ice slide) 5 Asbestos cape (heat immune) |
| A0 | 1 | tears | 0 | **Tears of Esmesanti collected, 0..9.**  `rokademo.bin` increments it (`A03B`, clamped at 9 with `cmp ..,9 / jc`, so the ninth is the last) and GAME.BIN's HUD painter at **`A3A5`** draws the row: while `[FF00] & 1`, for `i` in `0..[A0]-1` it calls `[0x203E]` with `bx = table[A3D3][i]` and `AL = (i == 8)`, i.e. the ninth slot uses the driver's second icon.  docs/CUTSCENES.md §5, docs/VIDEO_DRIVERS.md §1.2 |
| A1 | ~10 | inventory | 0 | item ids, 0-terminated (`90B9`) |
| AB | 7 | magic_count | 0C 06 08 04 03 04 03 | |
| B2 | 2 | max_hp | 0x50 | |
| B4 | 7 | magic_max | 0C 06 08 04 03 04 03 | |
| C2 | 1 | hero_flags | 0 | bit0 facing left, bit1 walking |
| C3 | 1 | door_side | 0 | |
| C4 | 1 | cur_map | 0x80 | system resource # (AH of mode-1 load) |
| C5 | 1 | town_map | 0x81 | |
| C6 | 2 | hp_regen_pending | 0 | +8 HP per frame while > 0 |
| C8 | 1 | music_idx | 0 | |
| E4 | 1 | attack_bonus | 0 | sword damage ×(E4+1); *uncertain* |
| E6 | 1 | boss_room | 0 | level flags bit 6 |
| E7 | 1 | hero_anim | 0 | 0x80 idle; walk frames & 0x7F; 0 jump pose |
| E8 | 1 | hero_dead | 0 | |

## Work areas above E000

| Addr | Size | Name |
|---|---|---|
| E000 | 0x900 | ring (36×64 cells) |
| E900 | 0x214 | screen copy (28×19) |
| EB15 | 4×16 | magic sprite records |
| EB60 | 4×7 | orbs |
| EB80 | 31×13 | enemy projectiles (`struct shot`) |
| ED20 | 128 | cell saved under sprite marker i |
| EDA0 | 1 | boss_state (0xFF = defeated; written by boss AI) |

## select.bin additions (Sprint 13)

Documented in full in `docs/TOWN.md` §12; the fields it owns or reveals:

| Addr | Size | Name | Meaning |
|---|---|---|---|
| `9D` | 1 | `magic_sel` | selected spell 1-7 (0 = none); **select.bin is the only writer** |
| `9E` | 1 | `worn_key_item` | the worn key item (see above); `[A1..A5]` is the bag |
| `A6..AA` | 5 | `potion_slots` | each holds *drug id + 1*; zeroed when drunk (`A422..A437`) |
| `AB..B1` / `B4..BA` | 7+7 | `magic_charge` / `magic_max` | per-spell charges and maxima |
| `94` / `96` | 1+1 | `shield_hp` / `shield_hp_max` | max confirmed; Holy Water adds `{80,90,100,110,115,120}[shield−1]` capped |
| `E4` | 1 | `sabre_oil` | +1 per Sabre Oil, **no cap**; cleared on entering town |
| `FF4B` | 1 | `menu_result` | potion slot value; **8 = Kioku Feather → warp to town** (fight.bin 99E0, no death penalty) |
| `FF18` | 2 | `key_mask` | `== 0x0286` on the potion row opens the hidden LEVEL/EXP panel |
| `EB60..EB7B` | 28 | `orbs[4]` | 7-byte records `{phase, dir, hits, 0,0,0,0}`; Magia Stone arms 4 × 80 hits |

**Original bug** (replicated in `port/`): at `A199` the magic row's Down tests `[ADF8]`
(the in-town flag) but the following `mov cl,2` discards the flags, so the potion row is
reachable from the magic row in town even though it was meant to be town-only.
