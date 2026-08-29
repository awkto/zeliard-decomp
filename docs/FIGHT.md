# fight.bin — cavern physics, collision and combat spec

Source: `src/fight.c` (hand-cleaned from ZELRES2[0] = fight.bin, loaded at
BASE:6000). Every number below is traceable to an instruction address in
fight.bin unless marked *uncertain*. Companion tables: `docs/STATE_PAGE.md`.

**Correction to ARCHITECTURE.md:** the 24-byte list in cell 0 of every MPPx
tileset bank (arena:8000, read by `6DF3`) is the list of **passable** cells,
not solid ones. `6DF3` returns ZF=1 when the value is found and every caller
treats ZF=1 as "free" (jump `6579 jnz blocked`, walk `66D2 stc / jz continue`,
floor `6B9A stc / jz no-floor`). Data agrees: mpp1's list is
`{0,1,2,8,9,A,B,C,F,10..19}` — it contains 0 (empty) and not 6 (the rock
wall that fills mp10). Cell 0 also holds five more lists (see §3).

## 1. Time: ticks and frames

* Timer: ZELIARD.EXE reprograms PIT channel 0 to divisor 0x13B1 (`ZELIARD:02BF`)
  → **236.7 Hz ticks**. STICK's INT 8 (`0250`) calls the sound and music
  drivers every tick, polls input every 5th tick, increments `FF1A` (byte),
  `FF1B`/`FF50` (words) every tick and chains to the BIOS every 13th tick.
* A game frame (`6F9B`) renders, then busy-waits `FF1A >= 2*speed` (`7125`)
  for the mid-frame sprite pass and `FF1A >= 4*speed` (`7179`) before zeroing
  `FF1A` (`717F`). `speed` = `FF33`, **default 5** (`ZELIARD:0173`), user
  adjustable to 10−digit (kernel `07DB..07F9`).  
  → default frame period = 20 ticks = **84.5 ms (≈11.8 fps)**; range 4..40
  ticks. All speeds below are per frame.
* During the wait the kernel idle services `[110]..[118]` run (F-key menus,
  speed change) and `[11E]` polls the quit key.
* Input is read through `INT 61h` (STICK `05FD`): AL = direction bits
  (1 up, 2 down, 4 left, 8 right; keyboard `FF17` OR joystick), AH = buttons
  (bit0 sword, bit1 magic). Edge-triggered button presses are `FF1D`/`FF1E`
  (set by the kernel, cleared by fight.bin when consumed).

## 2. Coordinates and buffers

| Thing | Where | Notes |
|---|---|---|
| Map | `C000` header + column-major RLE stream | see ARCHITECTURE.md; width `[C002]` cells, 64 rows, wraps horizontally |
| Ring | `E000`, 36 cols × 64 rows, stride 0x24 | ring column 0 = map column `scroll_col` (`[0x80]`); rows wrap mod 64 (`6D82/6D8E` wrap the whole 0x900 buffer, so pointer arithmetic is linear inside it) |
| Window | `win` = `[FF31]` = `&ring[scroll_row][0]` (`6CE0`) | screen shows ring columns 4..31 × rows scroll_row..+18 (28×19 cells = 224×152 px) |
| Screen copy | `E900`, 28×19 | 0xFD force redraw, 0xFF hero, 0xFC/0xFE message box (`7210`) |
| Hero | `hero_scr_col` `[0x83]`, `hero_scr_row` `[0x84]` | top-left of a 3×3-cell (24×24 px) sprite, drawn at pixel (col*8, row*8) (gfmcga `42F7`). Ring cell of the top-left = `win + row*0x24 + col + 4` (`6DB1`) |
| Hero map position | column `scroll_col + hero_scr_col + 4`, row `(scroll_row + hero_scr_row) & 63` = `FF35` (`7007`) | |

The hero never moves on screen while walking: every step scrolls the world
one column (`66F8` / `68A0`), and the vertical motion scrolls one row
(`6621` / `6B2E`) except while the jump's rise is being undone. The hero's
screen column is pulled back toward **12** (`6FF9`; boss rooms: the AI's
`[[A002]+7]`) and its screen row toward `hero_home_row` (`9F00`, = the map's
row bias `[C016]`, 10 in normal caverns) one cell per frame while grounded
(`6FAC`). A port can simply keep a camera that centres the hero on column 12 /
row 10 and applies the same one-cell-per-frame catch-up.

Scrolling is cell-granular: **8 px per step, no sub-cell positions anywhere**.

## 3. Cell classification (tileset bank cell 0, arena:8000)

| Offset | Entries | Meaning | Reader |
|---|---|---|---|
| 8000 | 24 | passable cell values (walkable/air) | `6DF3` |
| 8018 | 4 (0 ends) | conveyor: pushes hero **left** | `6BC4` (DL=2) |
| 801C | 4 | conveyor: pushes hero **right** | `6BC4` (DL=1) |
| 8020 | 4 | hazard (damage every frame) | `73C0` |
| 8024 | 4 | updraft (lifts 1 row per loop) | `76F6` CL=0 |
| 8028 | 4 | current: pushes 2 cells/frame left; one-way wall in cavern 7 | `76F6` CL=1 |
| 802C | 4 | current: pushes 2 cells/frame right | `76F6` CL=2 |

Cell-value classes used by the collision tests:

| Test | Address | Passable when |
|---|---|---|
| `passable_wall` (head row, vertical probes, wall-unstick, door/ledge checks) | `6DE5` | value ≥ 0x40 (any DCHR fixture/item, any sprite marker) **or** in the 8000 list |
| `passable_body` (feet rows when walking, floor under feet) | `6E1B` | value ≥ 0x49 **or** in the list. So DCHR 0x40..0x48 (elevator cells 40-42, gate fixtures 43-48) are solid to the body and can be stood on |
| `passable_shot` (projectiles) | `6DEC` | same as body |
| AI `cell_passable_ai` (vec 23) | `94E1` | cells < 0x49 passable iff in the list; 0x49..0x7F pass; ≥ 0x80 (sprite markers) block — verified in docs/ENEMIES.md |
| ladder | `6BBD` | value 1 or 2 |
| door column | `7A8C` | value 0x4A (DCHR cell 10) |
| sprite marker | bit 7 | `6DCB` returns the object's `type` byte instead; `type & 0x80` = solid to the hero |

## 4. Hero geometry

The sprite is 3×3 cells but the **solid body is the middle column only**
(8 px wide × 24 px tall). Consequences (all in `66A5`/`684C`/`6B76`):

* Walking left tests the hero's own *left* column (the body moves into it);
  walking right tests the *right* column. Test order for moving left:
  1. sprites at ring col−1, rows −1..+2: an object with `type&0x80` blocks;
  2. (not crouching) head cell (row 0, col 0): `passable_wall`; in cavern 7 a
     CURRENT_R cell here or below blocks (`67A3`);
  3. rows +1, +2 of col 0: `passable_body`;
  4. else scroll. Mirror for right with col +2.
* Crouching (`FF38`) skips the head-row test → the hero can walk through
  2-cell-high gaps while crouched.
* Floor (`6B76`): the cell on row +3 (below the feet) under col +1 (body) —
  solid sprite (`type&0x80`) under col+1 or col+0 also counts. If the body
  cell below is open and the hero is **moving** (`hero_anim != 0x80`), he
  still stands when both col+0 and col+2 below are solid (walks over a 1-cell
  hole); an idle hero drops into it.
* Above-head probe for jumping: (row −1, col +1) `passable_wall` (`656E`).
* Ladder probes: mount = (row 0, col +1) (`65C8`); grab while falling =
  (row +2, col +1) (`699D`); descend = (row +3, col +1) (`6ADC`); stay on
  ladder = (row 0, col +1) or (row +1, col +1) (`630C`/`6312`).
* Conveyor probe: (row +2, col +1) (`6A6F`). Updraft/current probes: col +1
  rows +2, +1, 0 (`76A1`). Ice-slide stop probe: (row +3, col +1) in
  0x40..0x48 (`64E0`).
* Hazard probe: all 9 cells (6 when crouching) + (row +3, col +1) unless on a
  ladder (`74AD..74EC`).

## 5. Movement

### Horizontal
* **1 cell (8 px) per frame** while left/right is held (`663E`/`67C6` →
  `66A5`/`684C`). Pressing the opposite direction first turns the hero
  (`6824`, one frame, no movement). No acceleration.
* Facing: `[0xC2]` bit0 = facing **left**; bit1 = walking.
* Ice (cavern 4 unless Ruzeria shoes `[0x9E]==4`, `6D9A`): steps in one
  direction are counted (`9F21`); when the direction changes or input stops,
  the hero slides `min(steps/2, 10)` more cells, one per frame (`6508`,
  `64BB`), unless standing on a DCHR 0x40..0x48 cell. Jumping refills the
  slide counter (`6545`, capped 10).
* Conveyors (`6A67`): while the (row+2,col+1) cell is in list 8018/801C the
  hero is pushed 1 cell every 4th frame (`6A8B`) unless walking against it;
  for `max_rise/2` frames (1, or 2 with Feruza shoes) right after a jump
  start the push is 1 cell **every** frame (`9F0C`, skipped with Silkarn
  shoes `[0x9E]==3`). Walking flag is cleared; walking *with* the belt is
  refused (`6655`/`67DA`).
* Currents (lists 8028/802C, `7699`): the hero is pushed 2 cells per
  main-loop iteration and **the frame is aborted before rendering** (`76C2`
  pops two return addresses), so the push repeats without delay until the
  hero's centre column is clear of the tiles. Updraft (8024): same, 1 row up
  per iteration, gravity and knockback disabled (`9F15`).

### Vertical
* **Rise:** while "up" is held, 1 row per frame, up to `max_rise` rows =
  **2** (4 with Feruza shoes `[0x9E]==1`, `6F9B`). Blocked by a solid cell
  above the head. `vstate = 0xFF` during the rise; gravity is off (`6962`).
  The world scrolls down while `hero_scr_row < 7`, otherwise the hero moves
  up on screen (`6596`).
* Releasing "up" (or reaching the limit / a ceiling) ends the rise at once →
  `vstate = 0x7F` (falling). Variable jump height 1..2 rows.
* Diagonal jump (up+left / up+right, `6634`/`67BC`): rise + walk in the same
  frame, and one extra step the frame the rise ends (`636F`).
* **Fall:** 1 row per frame, no acceleration (`6978`). The first rows fallen
  undo the on-screen rise (`hero_scr_row++`), then the world scrolls. While
  falling the input handler is skipped (`698D` pops its return) except for
  *air control*: keep walking in the facing direction if the jump started
  while walking; pressing the opposite direction turns around and steps onto
  a ledge if the cell diagonally below is solid (`69E6..6A64`); with the
  walking flag clear, left/right only moves the hero if that ledge exists.
  Walking off an edge carries one extra step (`69CB`).
* Falling onto a ladder cell (row+2, col+1) grabs it unless walking (`69A8`).
* **Landing** (`6B41`): `vstate = 0`, input skipped that frame; a fall of
  ≥2 rows lands crouched (`6B6A`, released after 2 frames by `62B5`).
* **Crouch:** "down" on the ground (`6AF9`); cleared 2 frames after the key
  is released. Lowers the sword origin by one row (`6F2B`) and removes the
  head row from hazard/contact/wall tests.
* **Ladders:** "up" with a ladder at (row 0,col+1) mounts; each frame "up" is
  held the hero climbs 1 row per *rendered* frame — each climbed row calls
  `frame()` (`65F9..661F`); per `hero_input` call it is 1 row, then 2,2,2…
  until `hero_anim` is odd (verified by the port's ladder tests, port/test_physics.c).
  "down" with a ladder below the feet descends the same way (`6B04`). On a
  ladder the main loop runs `62DB` (no gravity, no sword), and the hero drops
  off when neither (row 0,col+1) nor (row+1,col+1) is a ladder, when "down"
  is pressed with no ladder below (`6AE7`), or when hit (`6488`).
* **Wall unstick** (`63DA`, the frame after landing): if both head-row
  cells are solid, push the hero right if (row+1,col+2) is open, else left.
* **Elevators** (fixture list A, DCHR cells 0x40-0x42): "down" while standing
  on one moves the platform and the hero one row down if the 3 cells under it
  are empty (`7FDC/8024`); "up" moves it up (`8074`); standing on it follows
  its motion (`818E`).

## 6. Combat

### Sword input (`6E3B`, needs `[0x92] != 0`)
* Button-1 edge, not already attacking/casting → `attacking = 0xFF`
  (`6F01`), `attack_type` = **1 (upward slash)** if "up" is held or, outside
  boss rooms, if any hittable enemy sprite is in the 4×8 block rows −4..−1, cols −3..+4 relative to the hero's top-left
  (`6EA0..6ED4`); otherwise **0 (slash)**. Sound 3.  The hittable test is
  `spr && !(type & 0x60) && !(flags & 0x10)` (`6EB4..6EC3`) — i.e. not
  sword-immune, not harmless, and not the second record of a tall enemy; note
  it does **not** exclude items (`type & 0x10`).  `port/combat.c` approximates
  it as `!(type & 0x70)`, which also skips items — a deviation worth checking.
* Button-1 **held** + "down" while airborne and not on a conveyor →
  `attack_type = 2` (down-thrust), sound 4 once (`6E5C`).
* The renderer runs the swing: it increments `FF46` each frame and clears
  `attacking` when done (gfmcga `3E45`, `3F1A`); `FF44` is its frame flag.

### Blade shape and hit application (`6F07`, every rendered frame while attacking)
Origin = 4 rows above the hero's top-left (3 when crouching). The shape is a
list of cell steps from the current sword block (kernel mode-4 block
`#sword` relocated to arena:B000; word index `((attack_var | facing<<4) +
{0,6,10}) & 0xFE`), i.e. per facing: pointers 0-2 = slash frames (0-1, 2-3,
4), 3-5 = upward slash frames, 5 = thrust; left-facing uses pointers 8-13.
Decoded from `sword.grp` (ZELRES2[26]) section 0, cells relative to the hero's
top-left (row, col), facing right:

```
slash f0-1: (-2,-2)(-2,-1)(-1,-2)(-1,-1)(-1,0)(0,-2)(0,-1)(0,0)(0,1)(1,-2)(1,-1)(1,0)(1,1)   wind-up behind
slash f2-3: rows 0..1 × cols -1..+4                                                          reach: 2 cells past the sprite (16 px)
slash f4  : rows 0..1 × cols +1..+4
up    f0-1: (-2,-1..1) (-1,-2..1) (0,-2..2)
up    f2-3: (-2,1..3) (-1,1..4) (0,1..4) (1,1..4) (2,1..4) (3,2..3)
thrust    : (0,1..2) (1,0..2) (2,0..2) (3,0..2)                                              1 row below the feet
```
Sections 1 and 2 (assumed sword levels 2 and 3, *uncertain* which kernel
block holds levels 4-6) extend the slash to col +5 and the thrust to row +4/+5;
left-facing shapes are mirrored about the body column (col +1). Every sprite
marker on a shape cell whose object `type` lacks bit 5 and whose `hit` byte
lacks bit 5 gets `hit = (hit & 0xE0) | 0x40 | 1` (`6F8D`): hit pending,
source 1. The enemy update converts pending→"hit this frame" (bit 5) unless
`type & 0x20` (`8DB9`), and the AI calls vector 26 to take the damage.

### Damage to enemies (`9851`, vector 28, AL = hit source → AH)
| Source | Formula |
|---|---|
| 1 sword | `sword_base[sword-1] + level/2` (`98B8` = 1,2,4,8,32,127), × `(attack_bonus[0xE4]+1)`, cap 255; ×2 and cap for a down-thrust |
| 0 stomp (crouching on an enemy, `8009`) | `level/2 + 1` |
| 9 orb | `min(255, (level+1)*4)` |
| 2..8 magic | `98BE[src-2]` = 2,4,8,16,32,64,255 |

`level` = `[0x8D]` (*name uncertain*, it is the strength term of every formula).
`enemy.hp -= dmg` (`97BD`); if it reaches 0 the enemy is killed: a drop is
rolled from the AI's per-class table `[[A006]+class*2]` (4 entries, index
`rand&3`, always 0 for a down-thrust kill, `97F2..9803`), `exp +=
[A008+class]` (`96C1`), the object gets `type |= 0x68` (dying + immune) and
sound 7 plays if within 19 rows of the window (`96D5`). Dying (`90E6`)
advances every 2nd frame; at phase 3 the object becomes its drop
(`type = 0x70|id`, `flags |= 0x80`, `timer = 4`) or vanishes (id 1 / lower half
of a tall enemy). Sword hits stun: an enemy with `hit & 0x20` cannot be hit
again until its AI clears the bit.

### Damage to the hero
* **Contact** (`751F`, every frame): scan 4 columns (ring col −1..+2) × rows
  −1..+1 (rows 0..+1 when crouching) for sprite markers whose `type` lacks bit
  6; each adds the AI table `[A010 + (type & 0xF)]` (eai1: 5,5,15,8,…) to a
  running total and flags its column. The total is applied once per flagged
  column, through the shield only on the facing side (left columns when facing
  left, `75BA`; right columns when facing right, `75CE`).
* **Shield** (`75E2`): `dmg = (dmg/2) >> ((shield+1)/2)` (shield 1-2: ÷4,
  3-4: ÷8, 5-6: ÷16); `shield_hp -= dmg`, at 0 the shield breaks (`761A`,
  `[0x93]=0`, "Shield broken."). Sound 8 when shielded, 9 otherwise.
* **Projectiles** (`846F`): a shot on one of the hero's rows (all 3; 2 when
  crouching) and on ring col `hero+4+{0,1}` facing right / `+{1,2}` facing
  left is consumed. A shield with the hero facing the shot (dir 0/1/7 = moving
  right vs facing left, dir 3/4/5 vs facing right) blocks it fully at shield
  ≥4, or when the shot's row equals the hero's middle row (dir 0/4), middle−1
  (dir 1-3) or middle+1 (dir 5-7) (`8556..85A3`). Otherwise `hp -= shot.damage`.
* **Knockback** (`6412`, the frame after any hit): 2 cells (`try_move` ×2,
  so walls stop it) away from the hit side; hit on both sides → in the facing
  direction; boss rooms with `9F01` set → always left. A hero on a ladder is
  knocked off it. Then, if there is no floor, one row of fall.
* **Invulnerability: none for the hero.** Contact damage is re-applied every
  frame the sprites overlap (the knockback is what separates them). The only
  latch is the hit flash `FF36` for the renderer.
* **Hazard tiles** (`74A0`): per frame `hazard_damage[cavern-1]` =
  {1,1,4,8,20,20,20,20,20} (`7516`); Pirika shoes (2) immune. Cavern 7 heat:
  15 HP every 64 frames unless shoes 5 (`704F`).
* **Regeneration:** +2 HP every 16 frames of no action (`719E`, `regen_tick`
  reset by jump/walk/down); potions add to `hp_regen_pending`, +8 HP per
  frame (`70E0`). Death at HP 0 (`718C`, unless `[0x7F]`): 3-frame animation,
  `exp += 127 − 2*level`, gold halved, HP restored, back to town (`98FC`).

### Enemy projectiles (`EB80` list, 13 bytes, ≤31 live, spawned by vector 29)
Move 1 cell per frame in one of 8 directions (table `85C2`; 0 = right,
counter-clockwise) or along a byte script (`flags&0x40`, `85F2`); die on a
non-`passable_shot` cell unless `flags&8`, or when `age >= life`. Animation:
cell + (age & {0,1,3,7}[cell>>6]). Drawn only inside the window
(`8366`: ring col 4..0x1F, row within 18 of `scroll_row`).

### Magic (summary; `87B0`, `8AAD`, `896E`)
Button 2 starts a cast (`casting`, 6 frames); 2 frames in, one charge of
`magic_count[sel-1]` is consumed and the spell's `EB15` records are filled
(`883F` table by spell 1..7: single bolt ×4, 4-sprite rain, 3-way spread).
Bolts move 2 cells/frame in their facing (`8BD0`), live 5/10/12 frames and hit
every sprite in the 3×3 around them with source `sel+1` (`8C4F`); spell 7 hits
every sprite in the window (`8918`). Orbs (`EB60`, 4 × 7 bytes): orbit the hero
using the 16-entry offset table `8790`, hit source 9, `hits` per orb.

## 7. Enemy records and the AI overlay

Object table at `[C010]`, 16-byte records, 0xFFFF-terminated (`struct obj` in
`src/fight.c`):

| Off | Field | Notes |
|---|---|---|
| +0 | u16 map col | high byte 0xFF = inactive (`0xFF00` on removal) |
| +2 | u8 row | ring row of the sprite's top-left (sprites are 2×2 cells, gfmcga `3363`) |
| +3 | u8 ring col | recomputed every frame (`8D38`), 0xFF off-ring |
| +4 | u8 type | bits 0-3 class (drop/EXP/contact tables) or item id; 0x08 dying; 0x10 item; 0x20 sword-immune; 0x40 no contact damage; 0x80 solid |
| +5 | u8 hit | bits 0-4 source; 0x20 hit this frame/stunned; 0x40 pending; 0x80 AI-private |
| +6 | u8 phase | AI animation/state; death counter |
| +7 | u8 flags | bits 0-3 drop id; 0x10 tall (uses the next record too); 0x20 event object; 0x40 clears row of object [+A] on death; 0x80 hero-overlap latch |
| +8 | u8 hp | 0 at spawn; eai1 sets 2 on first update (`A27C`) |
| +9 | u8 next | AI state; after death 0 = vanish, 0x10 = become item |
| +A | u8 link | |
| +B | u16 home col | 0xFFFF never respawns (event objects: flag pointer) |
| +D | u8 home row | (event objects: flag mask) |
| +E | u8 home type | |
| +F | u8 timer | respawn timer, spawn attempt on wrap (`8D96`); pickup counter |

Per-frame enemy pass (`8D19`): for each active object inside the ring, restore
the cell under its marker, expose the hit bit, call the AI (or the built-in
dying/item state machine when `type & 0x18`), then write the marker
`0x80|index` at the new (row, rcol) saving the covered cell in `ED20[index]`.
Respawn (`94FF`) only off-screen: ring col not 0/35, and either row not within
24 rows below `scroll_row−2` or ring col outside 3..31, with no sprite in the
3×3 around the spawn cell. Map scrolling re-marks objects in the uncovered
column (`6776`/`6915`).

**AI overlay (eai1..8.bin, boss *.bin) at BASE:A000, raw:**

| Address | Content |
|---|---|
| `[A000]` | entry. Called per live enemy with **SI = record, DI = ring cell of its old position, `FF4A` = index**, DS=CS=BASE (`8DF7`). In boss maps called once per frame instead of the whole pass (`8D1D`), and once after loading (`7C27`) |
| `[A002]` | boss info block: +3 u16 boss HP (initial bar value, updated by the overlay via video `[200C]`), +5 u16 EXP, +7 u8 camera/hero screen column, +8 u8 knockback-left flag, +9 u16 name-record ptr `{x, u16 y, len, chars}`, +B u16 gold — see docs/ENEMIES.md (boss overlays rebuild the whole C010 list per frame from a part buffer) |
| `[A006]` | pointer to 8 pointers (per class) to 4-byte drop-id lists |
| `A008` | u8[8] EXP per class |
| `A010` | u8[16] contact damage per `type & 0xF` |

The AI moves the record with fight.bin's vectors (SI = record, CF=1 blocked):
4-11 step R, RU, U, LU, L, LD, D, RD (`91E5..926C`, each first probes the
cells beside the 2×2 sprite with vector 23 and refuses ring col ≤1 / ≥0x22);
vectors 2/3 dispatch a step by `AL & 7`; 12-19 are the bare probes; 20-22 ring
address/wrap; 24 hazard test under the sprite; 25 `enemy_killed`; 26
`enemy_take_damage` (the AI must call it when it sees `hit & 0x20`, eai1
`A280`); 27 map col → ring col; 28 damage lookup; 29 spawn a projectile
template at BX; 30 clear projectiles; 31 find a visible free object; 32 current
tiles under the sprite. Kernel `[11A]` supplies randomness (`FF1B`).
eai1 dispatches on `type & 0xF` through a table at `A262` (`A254`); enemies
standing on a hazard tile are killed outright (`A26A`).

## 8. Items, doors, transitions

* Pickups are objects with `type & 0x10`; state = `(type & 0x1F) − 0x10`
  → table `8E14`: 0 corpse fade, 1 touch trigger, 2 flash, 3 treasure box
  (50/100/−/500/1000 gold or drop), 4-5 coins (1/10/100 gold), 6 key, 7 lion
  key, 8 potion (+80 HP over 10 frames), 9 full potion, A/B shoes, D boss
  chest with message, E Hero's Crest. Touch test `9190`: item top-left within
  rows −1..+2 / cols −1..+2 of the hero's top-left (a 2×2 sprite overlapping the
  3×3 hero).
* Doors: DCHR cell 0x4A one row above the hero's top-left + "up" (`7A83`);
  record in the C00A list (`struct door`), keys via `7E15`; transition
  `7B32..7D61` reloads the map with `KRN_LOAD` mode 1, positions the hero at
  `dest_col−16 / dest_row+1−row_bias` (`7DC1`) and walks him in 26 frames
  (`7C6E`). The message text table for locked doors / items is at `9A1E..`.
* The in-game menu overlay lives at arena:C000 and is swapped into A000 to
  run (`72D9`), result `FF4B == 8` warps to town.

## 9. Port milestones

**(a) walk around cavern 1 with correct collision** needs: §2 camera model,
§3 lists from mpp1 cell 0 (passable = list; body-solid 0x40-0x48), §4 body =
middle column, §5 horizontal 1 cell/frame with the 3-step test, rise 2 rows /
fall 1 row per frame with the floor rule (1-cell-gap walkover when moving),
landing crouch, ladders 1-2 rows/frame, elevators, and the 84.5 ms frame.

**(b) kill one enemy with correct damage** needs: the object record, marker
placement, `eai1` initial HP 2, contact damage 5 (class 0/1) per frame with the
2-cell knockback, sword shape table (section 0, frames by `FF46`), hit → stun →
vector 26 → `sword_base[0] + level/2 = 1 + level/2` per hit, death phase 3 →
drop, EXP `A008[class]`.

## 10. Not decoded / uncertain

* **Swing length** — how `FF46`/`attack_var` advances and when `attacking`
  clears (gfmcga `3E45`/`3F1A`) is not decoded; it is the one combat number not
  traceable to an instruction.  `port/combat.c` approximates 6 rendered frames
  (3 shapes × 2).

* Which kernel mode-4 blocks hold sword levels 4-6 (sword.grp has 3 sections).
* `[0x8D]` "level", `[0xE4]` attack bonus, `[0x7F]`, `[0x49]`: roles inferred
  from the formulas only; the town/shop overlays that write them are not yet
  decompiled.
* The renderer side (gfmcga) of the hero animation frames (`E7`, `FF3F-FF41`,
  `FF44-FF46`) and enemy sprite frame selection.
* Magic spells 1-7 individually (only the shared machinery is described),
  the boss AI overlays, the message renderer `740E`, fixture-C variants
  (`8244` table), signs (`78DD`).
