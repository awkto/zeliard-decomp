/* cutscene.h — the three cutscene overlays (docs/CUTSCENES.md, src/opdemo.c,
 * src/enddemo.c, src/rokademo.c).
 *
 *   opdemo.bin   ZELRES1[0]   the attract sequence: the scrolling prologue over
 *                             the pendant, the demon's warning, the title
 *                             screen, the STAFF credits and the 15-picture
 *                             storm demo with its byte script
 *   enddemo.bin  ZELRES2[50]  the ending: nine tableaux, then the typewriter
 *                             credits roll synchronised to zend.msd
 *   rokademo.bin ZELRES3[0]   "a Tear of Esmesanti" — see cutscene_tear() in
 *                             tear.c; it is not a gd demo and plays on the
 *                             fight screen
 *
 * opdemo and enddemo draw through **gdmcga only** (port/gd.c) and use nothing
 * of the game engine, so `Cutscene` owns its own 320x200 framebuffer, its own
 * 256-entry DAC and its own frame loop, exactly the way `Shop` and `Status` do.
 * The front end supplies `present`, which is called once per rendered frame;
 * one frame is `CS_TICKS_PER_FRAME` of the 236.7 Hz [FF1A] clock the original
 * waits on.
 *
 * Every script, text block, metric table and geometry constant is read out of
 * the original overlay image at run time — no text is copied into the port. */
#ifndef ZEL_CUTSCENE_H
#define ZEL_CUTSCENE_H
#include <stdint.h>
#include "gd.h"
#include "text.h"

/* the original waits on [FF1A] at 236.7 Hz; the port renders a frame every
 * four ticks (16.9 ms), which divides every wait the demos use (0x10 a
 * character, 0x14 a dissolve pass, 0x1C a scroll step, 0xF0 a beat) exactly. */
#define CS_TICKS_PER_FRAME 4
#define CS_FRAME_MS (1000.0 * CS_TICKS_PER_FRAME / 236.7)

/* one lip-sync code group (opdemo 6C28/6C77, enddemo 64E3/6530/6568/658C/65D5).
 * `n < split` picks the first frame bank, `n >= split` the second; a bank whose
 * source is the overlay image itself has `image` set. */
typedef struct GdSpeaker {
    uint8_t group;          /* 0x80, 0x90, 0xA0, 0xB0, 0xC0 */
    uint8_t split;          /* 0x10 = no second bank */
    uint8_t image;          /* 1 = the frames live in the overlay, not the arena */
    struct { unsigned base; unsigned stride; uint8_t x4, y, w, h; } bank[2];
} GdSpeaker;

typedef struct Cutscene Cutscene;
typedef void (*CutscenePresentFn)(Cutscene *c);

struct Cutscene {
    Gd        gd;
    const char *dir;
    const TextFont *font;
    uint8_t   fb[GD_W * GD_H];          /* A000:0000 */
    uint8_t  *scratch;                  /* (CS+0x2000), 64 KB */
    uint8_t  *arena;                    /* the arena segment, 64 KB */
    uint8_t  *img;   size_t imglen;     /* the running overlay, addressed @6000 */

    /* the clock and the two abort flags ([FF1A], [FF1D], [FF29]) */
    unsigned  ticks;
    unsigned  frames;
    int       abort;                    /* Space or Return was seen */
    int       quit;                     /* the front end wants out */
    uint8_t   key;                      /* bit 0: Space/Return is down */
    uint8_t   key_prev;

    /* opdemo 6D56..6D5D / enddemo 6630..6637 — the narration engine.  Both
     * overlays carry the same engine with a different speaker table and a
     * different pair of metric tables, so the port parameterises it. */
    unsigned  narr_p;
    int       narr_x, narr_line, narr_ink, narr_shadow, narr_click;
    unsigned  bearing_addr, advance_addr;
    const struct GdSpeaker *speakers; int nspeakers;
    /* opdemo 653D/653F — the fixed-pitch typed-text player */
    int       text_x, text_y;

    /* enddemo 6965..696C — the credits script */
    unsigned  script_p;
    int       cur_col, cur_row, pause_ticks, char_delay, scene_i;
    unsigned  sync;                     /* [FF21], bumped by the score */
    unsigned  ff50;                     /* the free-running tick counter */

    /* the front end */
    void     *user;
    CutscenePresentFn present;
    unsigned  max_frames;               /* headless guard: 0 = none */
    int       act;                      /* which act is running (for the tests) */
    int       beat;                     /* how many narration beats have played */
};

/* load gdmcga + the font; `fb` and the two 64 KB buffers are allocated here */
int  cutscene_init(Cutscene *c, const char *dir, const TextFont *font);
void cutscene_free(Cutscene *c);

/* opdemo 6002/640C/6540 — the three acts, in order.  Returns 0 when it ran to
 * the end or was aborted, non-zero if the overlay could not be loaded. */
int  cutscene_intro(Cutscene *c);
/* enddemo 6002/6638 — the ending.  The original never returns; the port does. */
int  cutscene_ending(Cutscene *c);

/* what the cutscene has on screen, and its own 256-entry palette */
const uint8_t *cutscene_framebuffer(const Cutscene *c);
void cutscene_to_rgb(const Cutscene *c, uint8_t *rgb);

/* --- exposed for test_cutscene.c --------------------------------------- */
/* load resource `res` (the 1-based number in the overlay's request block) of
 * archive `arc`, run unpacker `rle`(0 = mask+delta, 1 = RLE, 2 = mask only,
 * 3 = raw) over it and leave the result at `arena + off`.  Returns its size. */
size_t cutscene_load(Cutscene *c, int arc, int res, int mode, unsigned off);
/* the three acts on their own, so a test can run just one */
int  cutscene_act1(Cutscene *c);
int  cutscene_act2(Cutscene *c);
int  cutscene_act3(Cutscene *c);
/* opdemo 6A80: run the narration script to the next 0xFD.  Returns 1 at 0xFF. */
int  cutscene_narrate(Cutscene *c);
/* the opdemo image addresses the port reads its data out of */
enum {
    OP_PROLOGUE_TEXT = 0x6FF0, OP_EPILOGUE_TEXT = 0x7338, OP_STAFF_TEXT = 0x742F,
    OP_NARRATION     = 0x79C6, OP_STORM_DROPS   = 0x9060, OP_DEMON_SPEECH = 0x9096,
    OP_DEMON_EYES    = 0x911E, OP_TTL2_MAP      = 0x912B, OP_GLYPH_BEARING = 0x947D,
    OP_GLYPH_WIDTH   = 0x94DD, OP_COPYRIGHT     = 0x64EA,
    ED_SCRIPT        = 0x787E, ED_SCENE_TABLE   = 0x6820,
};

#endif
