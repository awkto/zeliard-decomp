/* test_cutscene.c — the three cutscene overlays and the gd art format
 * (docs/CUTSCENES.md; port/gd.c, port/cutscene.c, port/tear.c).
 *
 * usage: test_cutscene [GAMEDIR]                                            */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sar.h"
#include "gd.h"
#include "cutscene.h"
#include "tear.h"
#include "shell.h"
#include "render.h"
#include "boss.h"
#include "png.h"

static int checks, failures;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) { failures++; printf("  FAIL %s\n", what); }
}
#define CHECK(c, w) check((c) != 0, w)

static const char *G_DIR = "../zeliard";
/* 0 when the (non-redistributable) game files are absent — a clean checkout or
 * CI.  The synthetic format checks still run; everything that needs a .SAR
 * says SKIP instead of failing. */
static int have_data = 1;
static const char *SHOT = NULL;   /* argv[2]: dump the Tear scene mid-flight */

/* the front end the acts drive: count frames and stop after `cap` of them */
static void null_present(Cutscene *c) { (void)c; }

/* ------------------------------------------------------------ the format */
static void test_format(void)
{
    printf("gd format       ");
    size_t len = 0;
    static uint8_t buf[0x30000];
    size_t n = 0;
    if (have_data) {
        uint8_t *ame = sar_load(G_DIR, 0, 13, 1, &len);            /* ame.grp */
        CHECK(ame != NULL, "ame.grp loads");
        n = ame ? gd_unpack_mask(ame, len, buf, sizeof buf, 1) : 0;
        /* 72 plane bytes x 104 rows x 3 planes = 22464, rounded up to nmask*8 */
        CHECK(n == 22528, "mask+delta unpacks ame.grp to nmask*8 = 22528 bytes");
        CHECK(n >= (size_t)72 * 104 * 3, "ame.grp is at least 72x104x3");
        free(ame);

        uint8_t *ttl3 = sar_load(G_DIR, 0, 31, 1, &len);           /* ttl3.grp */
        n = ttl3 ? gd_unpack_rle(ttl3, len, buf, sizeof buf) : 0;
        CHECK(n >= (size_t)65 * 112 * 2, "the 6/14-bit RLE unpacks ttl3.grp (65x112x2)");
        free(ttl3);

        uint8_t *ttl2 = sar_load(G_DIR, 0, 30, 1, &len);           /* ttl2.grp */
        n = ttl2 ? gd_unpack_rle(ttl2, len, buf, sizeof buf) : 0;
        CHECK(n == 0xC80, "ttl2.grp is a 40x40 two-plane tile bank (0xC80 bytes)");
        free(ttl2);
    }

    /* the lag-2 XOR delta: 00 00 ... must stay 00, and one 2-bit field must
     * propagate to every later field of the same parity */
    uint8_t t[4] = { 0x02, 0x00, 0x00, 0x00 };   /* nmask would be 4 -> synthesise */
    uint8_t src[2 + 1 + 1] = { 1, 0, 0x80, 0x40 };
    n = gd_unpack_mask(src, sizeof src, buf, sizeof buf, 1);
    CHECK(n == 8, "one mask byte emits 8 output bytes");
    CHECK(buf[0] == 0x55 && buf[1] == 0x55, "the 2-bit delta accumulator runs across bytes");
    (void)t;
}

/* ----------------------------------------------------------- the palette */
static void test_palette(void)
{
    printf("gd palette      ");
    static uint8_t fb[GD_W * GD_H];
    static uint8_t scratch[GD_SCRATCH];
    Gd g;
    if (gd_init(&g, G_DIR, fb, scratch, NULL)) { if (have_data) CHECK(0, "gdmcga.bin loads"); printf("SKIP\n"); return; }
    CHECK(1, "gdmcga.bin (ZELRES1[5]) loads");
    /* 4289 record 0, entry 1 = (00,0F,0F); record 4 entry 6 = (1F,1F,00) */
    CHECK(g.base[0][1][0] == 0x00 && g.base[0][1][1] == 0x0F && g.base[0][1][2] == 0x0F,
          "palette record 0 colour 1 = (00,0F,0F)");
    CHECK(g.base[4][6][0] == 0x1F && g.base[4][6][1] == 0x1F && g.base[4][6][2] == 0x00,
          "palette record 4 colour 6 = (1F,1F,00)");
    gd_set_palette(&g, 4);
    /* DAC[l*16+r] = C[l] + C[r], scaled (v<<2)|(v>>4) */
    int l = 6, r = 7;
    unsigned want = (unsigned)(g.base[4][l][0] + g.base[4][r][0]);
    CHECK(g.dac[l * 16 + r][0] == gd_dac8((uint8_t)want), "DAC[l*16+r] = C[l] + C[r]");
    CHECK(gd_dac8(0x3E) == 251 && gd_dac8(0x1E) == 121 && gd_dac8(0x0F) == 60 && gd_dac8(0x0E) == 56,
          "dac8() matches the DOSBox 6-to-8-bit expansion");
    CHECK(g.dac[0][0] == 0 && g.dac[0][1] == 0 && g.dac[0][2] == 0, "DAC entry 0 is black");
    gd_free(&g);
}

/* ------------------------------------------------------------ the intro */
static void test_intro(void)
{
    printf("opdemo          ");
    static Cutscene c;
    if (cutscene_init(&c, G_DIR, NULL)) { if (have_data) CHECK(0, "cutscene_init"); printf("SKIP\n"); return; }
    size_t len = 0;
    c.img = sar_load(G_DIR, 0, 0, 1, &len);
    c.imglen = len;
    CHECK(c.img != NULL && len == 13865, "opdemo.bin (ZELRES1[0]) is 13865 bytes");
    if (c.img) {
        CHECK(c.img[0] == 0x02 && c.img[1] == 0x60, "its vector word is 6002");
        /* the narration script: 21 x 0xFD then one 0xFF, 22 beats */
        int fd = 0, ff = 0;
        for (unsigned a = OP_NARRATION; a <= 0x905F; a++) {
            uint8_t b = c.img[a - 0x6000];
            if (b == 0xFD) fd++;
            if (b == 0xFF) ff++;
        }
        CHECK(fd == 21 && ff == 1, "the narration script at 79C6 has 22 beats");
        CHECK(!memcmp(c.img + (OP_PROLOGUE_TEXT - 0x6000) + 11, "Two thousand years,", 19),
              "the prologue text block is at 6FF0");
        CHECK(!memcmp(c.img + (OP_STAFF_TEXT - 0x6000) + 10, "Fantasy Action Game", 19),
              "the STAFF text block is at 742F");
        CHECK(c.img[OP_DEMON_EYES - 0x6000] == 1 && c.img[OP_DEMON_EYES - 0x6000 + 12] == 0,
              "the demon eye script at 911E is 12 frames + a terminator");
        /* the two proportional metric tables, 96 entries each */
        const uint8_t *adv = c.img + (OP_GLYPH_WIDTH - 0x6000);
        CHECK(adv[0] == 5 && adv['W' - 0x20] == 8 && adv['\'' - 0x20] == 3,
              "the glyph advance table at 94DD");
        const uint8_t *bear = c.img + (OP_GLYPH_BEARING - 0x6000);
        CHECK(bear[0] == 0 && bear['I' - 0x20] == 2, "the glyph bearing table at 947D");
    }
    c.user = NULL; c.present = null_present;
    cutscene_act1(&c);
    CHECK(c.frames == 6465, "act 1 (prologue, demon, title) runs 6465 frames");
    c.frames = 0;
    cutscene_act2(&c);
    CHECK(c.frames > 3000, "act 2 (the STAFF credits) runs the whole block");
    c.frames = 0;
    cutscene_act3(&c);
    CHECK(c.beat == 22, "act 3 plays all 22 narration beats");
    CHECK(c.frames > 30000, "act 3 runs for minutes");
    cutscene_free(&c);
}

/* ---------------------------------------------------------- the aborts */
static void test_abort(void)
{
    printf("abort keys      ");
    static Cutscene c;
    if (cutscene_init(&c, G_DIR, NULL)) { printf("SKIP\n"); return; }
    size_t len = 0;
    c.img = sar_load(G_DIR, 0, 0, 1, &len); c.imglen = len;
    c.present = null_present;
    c.key = 1;                      /* Space held from the first frame */
    cutscene_act1(&c);
    CHECK(c.frames < 300, "Space aborts act 1 into its teardown");
    CHECK(c.abort == 0, "the teardown clears the abort flags for the next act");
    cutscene_free(&c);
}

/* ----------------------------------------------------------- the ending */
static void test_ending(void)
{
    printf("enddemo         ");
    static Cutscene c;
    if (cutscene_init(&c, G_DIR, NULL)) { printf("SKIP\n"); return; }
    size_t len = 0;
    uint8_t *img = sar_load(G_DIR, 1, 50, 1, &len);
    CHECK(img != NULL && len == 8683, "enddemo.bin (ZELRES2[50]) is 8683 bytes");
    if (img) {
        CHECK(img[0] == 0x02 && img[1] == 0x60, "its vector word is 6002");
        /* the seven-entry scene table at 6820 */
        static const uint16_t SCENE[7] = { 0x682E, 0x685A, 0x6891, 0x68B5, 0x68C2, 0x68CF, 0x6932 };
        int ok = 1;
        for (int i = 0; i < 7; i++) {
            unsigned v = img[0x820 + i * 2] | (img[0x820 + i * 2 + 1] << 8);
            if (v != SCENE[i]) ok = 0;
        }
        CHECK(ok, "the scene table at 6820 has the seven documented entries");
        CHECK(!memcmp(img + (0x6AA8 - 0x6000) + 3, "At long last, Jashiin was destroyed", 35),
              "act 1 has its own narration script at 6AA8 (src/enddemo.c calls it beat())");
        CHECK(!memcmp(img + (0x787E - 0x6000) + 12, "STAFF", 5),
              "the credits script at 787E starts with STAFF");
        free(img);
    }
    c.present = null_present;
    c.max_frames = 4000;            /* the ending is long; just prove it runs */
    cutscene_ending(&c);
    CHECK(c.frames >= 4000, "the ending runs");
    CHECK(c.act == 4 || c.act == 5, "it reaches act 1 or the credits");
    cutscene_free(&c);
}

/* -------------------------------------------------------------- the Tear */
/* argv[2]: dump the frame the crystal is halfway to its slot on, so the scene
 * can be eyeballed the way `make verify` eyeballs the rest of the port */
static Tear *SHOT_T;
static void tear_present(Game *g)
{
    if (!SHOT || !g->tear) return;
    SHOT_T = g->tear;
    if (SHOT_T->frames != 60) return;
    static uint8_t rgb[FB_W * FB_H * 3];
    render_to_rgb(tear_framebuffer(SHOT_T), rgb);
    if (!png_write_rgb(SHOT, rgb, FB_W, FB_H)) fprintf(stderr, "wrote %s\n", SHOT);
    SHOT = NULL;
}

static void test_tear(void)
{
    printf("rokademo        ");
    static Shell s;
    if (shell_init(&s, G_DIR, 1)) { if (have_data) CHECK(0, "shell_init(MP1D)"); printf("SKIP\n"); return; }
    s.quiet = 1;
    s.present = tear_present;
    Game *g = &s.g;
    g->present = tear_present;

    CHECK(s.tear_art.loaded, "the Tear art loads");
    CHECK(s.tear_art.ncells == 54, "dman.grp converts to 54 cells");
    /* GAME.BIN A3D3: the nine slot columns, outside-in */
    static const uint8_t X4[9] = { 0x0F, 0x3D, 0x15, 0x37, 0x1B, 0x31, 0x21, 0x2B, 0x26 };
    CHECK(!memcmp(s.tear_art.slot_x4, X4, 9), "GAME.BIN A3D3 has the nine slot columns");
    CHECK(s.tear_art.frame[9][0] == 0x2E && s.tear_art.frame[9][8] == 0x35,
          "rokademo A435 has TEN frame maps, not nine (src/rokademo.c is short one)");

    /* MP1D's exit door only exists after the post-boss transition installs it */
    Map *m = (Map *)g->map;
    CHECK(m->ndoors == 0, "MP1D ships with no doors at all");
    g->hp = g->max_hp;
    post_boss_transition(g);                             /* 72F1 */
    CHECK(g->map->ndoors >= 1, "post_boss_transition installs the exit door");
    if (g->map->ndoors) {
        CHECK((g->map->doors[0].dflags & 0x80) != 0,
              "the exit door's byte +8 has bit 7 (fight.bin 7C18)");
        g->page[0xA0] = 0;
        int rc = tear_cutscene(&s);
        CHECK(rc == 0, "the Tear cutscene runs");
        CHECK(g->page[0xA0] == 1, "[A0] counts one Tear of Esmesanti");
        CHECK(g->tear == NULL, "it hands the screen back");
        /* and the HUD row now draws one icon at slot 1 (x4 0x0F -> x 60) */
        static uint8_t fb[FB_W * FB_H];
        memset(fb, 0, sizeof fb);
        tear_draw_slots(fb, g, &s.tear_art);
        int lit = 0;
        for (int y = 0; y < 13; y++)
            for (int x = 60; x < 76; x++) if (fb[y * FB_W + x]) lit++;
        CHECK(lit > 40, "GAME.BIN A3A5 draws the Tear icon at x 60");
        memset(fb, 0, sizeof fb);
        g->page[0xA0] = 0;
        tear_draw_slots(fb, g, &s.tear_art);
        int any = 0;
        for (int i = 0; i < FB_W * 13; i++) if (fb[i]) any = 1;
        CHECK(!any, "with [A0] == 0 the row draws nothing (A3AA)");
        g->page[0xA0] = 1;
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) G_DIR = argv[1];
    if (argc > 2) SHOT = argv[2];
    {   /* the original game files are not redistributable, so a clean checkout
         * (and CI) has none: run the synthetic format checks and skip the rest */
        size_t probe_len = 0;
        uint8_t *probe = sar_load(G_DIR, 0, 0, 1, &probe_len);
        have_data = probe != NULL;
        free(probe);
        if (!have_data)
            fprintf(stderr, "  (ZELRES1.SAR not available in %s: only the synthetic gd checks run)\n", G_DIR);
    }
    test_format();   printf("ok\n");
    test_palette();  printf("ok\n");
    test_intro();    printf("ok\n");
    test_abort();    printf("ok\n");
    test_ending();   printf("ok\n");
    test_tear();     printf("ok\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
