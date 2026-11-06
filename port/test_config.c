/* test_config.c — the settings file (#42): the ini parser, the writer, the
 * atomic save and the no-SDL default path.  Needs nothing but the port, so it
 * is part of `make test`'s ten-binaries-and-one contract. */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config.h"

static int checks, failures;
static void ck(int ok, const char *fmt, ...)
{
    va_list ap; checks++;
    if (ok) return;
    failures++;
    printf("  FAIL ");
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
}

/* ------------------------------------------------------------- the sentinels */
static void t_defaults(void)
{
    printf("defaults        ");
    Config c; config_init(&c);
    ck(c.scale == -1 && c.fullscreen == -1 && c.music == -1 && c.sfx == -1 &&
       c.speaker == -1 && c.volume == -1, "init sets every int sentinel");
    ck(c.dir[0] == 0 && c.video[0] == 0 && c.filter[0] == 0 && c.aspect[0] == 0,
       "init leaves the strings empty");
}

/* ---------------------------------------------------------------- the parse */
static void t_parse(void)
{
    printf("parse           ");
    Config c; config_init(&c);
    int n = config_parse(&c,
        "dir=/games/zeliard\nscale=4\nfullscreen=1\nvideo=ega\nfilter=integer\n"
        "aspect=1:1\nmusic=0\nsfx=1\nspeaker=1\naudio=tandy\nvolume=7\n");
    ck(n == 11, "all eleven keys recognised (got %d)", n);
    ck(!strcmp(c.audio, "tandy"), "audio back end");
    ck(!strcmp(c.dir, "/games/zeliard"), "dir");
    ck(c.scale == 4, "scale");
    ck(c.fullscreen == 1, "fullscreen");
    ck(!strcmp(c.video, "ega"), "video");
    ck(!strcmp(c.filter, "integer"), "filter");
    ck(!strcmp(c.aspect, "1:1"), "aspect");
    ck(c.music == 0 && c.sfx == 1 && c.speaker == 1, "music/sfx/speaker");
    ck(c.volume == 7, "volume");
}

/* ------------------------------------------------------- missing keys, junk */
static void t_bad(void)
{
    printf("bad lines       ");
    Config c; config_init(&c);
    int n = config_parse(&c,
        "garbage line\n=5\nkeyonly\n\n# comment\n; also a comment\n"
        "scale = 9\nunknown=1\nfullscreen = maybe\n");
    ck(n == 1, "only the one good line is recognised (got %d)", n);
    ck(c.scale == 9, "whitespace around key and value is trimmed");
    ck(c.fullscreen == -1, "a bad boolean leaves the field unset");
    ck(c.dir[0] == 0 && c.video[0] == 0, "junk never touches the other fields");

    Config d; config_init(&d);
    config_parse(&d, "dir=/a b/c  \r\nscale=2\r\n");
    ck(!strcmp(d.dir, "/a b/c"), "a path with spaces survives, trailing CR/space trimmed");
    ck(d.scale == 2, "CRLF line endings");
}

static void t_bools(void)
{
    printf("bool spellings  ");
    Config e; config_init(&e);
    config_parse(&e, "music=on\nsfx=off\nspeaker=true\nfullscreen=no\n");
    ck(e.music == 1 && e.sfx == 0 && e.speaker == 1 && e.fullscreen == 0,
       "on/off/true/no are all understood");
}

/* -------------------------------------------------------------- round trip */
static void t_roundtrip(void)
{
    printf("round trip      ");
    Config a; config_init(&a);
    snprintf(a.dir, sizeof a.dir, "/opt/Zeliard Files");
    a.scale = 5; a.fullscreen = 1;
    snprintf(a.video, sizeof a.video, "hgc");
    snprintf(a.filter, sizeof a.filter, "smooth");
    snprintf(a.aspect, sizeof a.aspect, "4:3");
    a.music = 0; a.sfx = 1; a.speaker = 1; a.volume = 3;
    snprintf(a.audio, sizeof a.audio, "tandy");

    char text[4096];
    config_render(&a, text, sizeof text);
    ck(strstr(text, "dir=/opt/Zeliard Files") != NULL, "the writer emits the dir");

    Config b; config_init(&b);
    int n = config_parse(&b, text);
    ck(n == 11, "every rendered key parses back (got %d)", n);
    ck(!strcmp(a.dir, b.dir) && a.scale == b.scale && a.fullscreen == b.fullscreen &&
       !strcmp(a.video, b.video) && !strcmp(a.filter, b.filter) && !strcmp(a.aspect, b.aspect) &&
       a.music == b.music && a.sfx == b.sfx && a.speaker == b.speaker && a.volume == b.volume &&
       !strcmp(a.audio, b.audio),
       "the round trip is lossless");
}

/* --------------------------------------------------------- the atomic write */
static void t_save(void)
{
    printf("atomic save     ");
    char tmpl[] = "/tmp/zel_cfg_XXXXXX";
    char *base = mkdtemp(tmpl);
    ck(base != NULL, "mkdtemp");
    if (!base) return;

    Config c; config_init(&c);
    snprintf(c.path, sizeof c.path, "%s/sub/zeliard.ini", base);
    c.scale = 2; c.fullscreen = 0;
    snprintf(c.filter, sizeof c.filter, "smooth");
    ck(config_save(&c) == 0, "config_save creates the parent directory and writes");

    FILE *f = fopen(c.path, "rb");
    ck(f != NULL, "the ini exists");
    if (f) fclose(f);

    char tmp[CONFIG_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", c.path);
    ck(access(tmp, F_OK) != 0, "no .tmp file is left behind");

    Config d; config_load(&d, c.path);
    ck(d.scale == 2 && d.fullscreen == 0 && !strcmp(d.filter, "smooth"),
       "the written file loads back");

    Config m; int rc = config_load(&m, "/tmp/zel_cfg_definitely_missing.ini");
    ck(rc == 0 && m.scale == -1 && m.dir[0] == 0, "a missing file is not an error");

    remove(c.path);
    char sub[CONFIG_PATH_MAX + 8];
    snprintf(sub, sizeof sub, "%s/sub", base);
    rmdir(sub);
    rmdir(base);
}

/* ----------------------------------------------------------- the path choice */
static void t_paths(void)
{
    printf("default path    ");
    char out[CONFIG_PATH_MAX];
    setenv("XDG_CONFIG_HOME", "/tmp/xdgtest", 1);
    config_default_path(out, sizeof out, "/usr/bin/zeliard");
    ck(!strcmp(out, "/tmp/xdgtest/zeliard/zeliard.ini"), "XDG_CONFIG_HOME wins");
    unsetenv("XDG_CONFIG_HOME");

    setenv("HOME", "/home/tester", 1);
    config_default_path(out, sizeof out, "/usr/bin/zeliard");
    ck(!strcmp(out, "/home/tester/.config/zeliard/zeliard.ini"), "HOME/.config is the fallback");
    unsetenv("HOME");

    config_default_path(out, sizeof out, "/opt/zel/zeliard");
    ck(!strcmp(out, "/opt/zel/zeliard.ini"), "next to the binary when there is no home");
}

/* ----------------------------------------------- key.* / pad.* (step 2) */
static void t_binds(void)
{
    printf("bindings        ");
    Config c; config_init(&c);
    ck(c.n_key == 0 && c.n_pad == 0, "init: no bindings");
    int n = config_parse(&c,
        "key.sword = Left Ctrl, X\npad.magic=A\nkey.left=Left,A\n"
        "key.sword = Z\nkey.nosuchaction = Q\nkey.up =\nkey.quicksave=Ctrl+Period\n");
    ck(n == 6, "six binding lines kept, the empty value refused (got %d)", n);
    ck(c.n_key == 4 && c.n_pad == 1, "counts (key %d, pad %d)", c.n_key, c.n_pad);
    ck(!strcmp(c.key[0].action, "sword") && !strcmp(c.key[0].value, "Z"),
       "a repeated action replaces its value in place; the last line wins");
    ck(!strcmp(c.key[1].action, "left") && !strcmp(c.key[1].value, "Left,A"), "order and value text kept verbatim");
    ck(!strcmp(c.key[3].value, "Ctrl+Period"), "modifier spelling kept");
    ck(!strcmp(c.pad[0].action, "magic") && !strcmp(c.pad[0].value, "A"), "pad binding");
    ck(config_set(&c, "key.", "X") == 0, "an empty action name is refused");

    char text[4096];
    config_render(&c, text, sizeof text);
    ck(strstr(text, "key.sword=Z\n") && strstr(text, "pad.magic=A\n") && strstr(text, "key.quicksave=Ctrl+Period\n"),
       "render writes the binding lines");
    Config d; config_init(&d); config_parse(&d, text);
    int same = d.n_key == c.n_key && d.n_pad == c.n_pad;
    for (int i = 0; same && i < c.n_key; i++)
        same = !strcmp(d.key[i].action, c.key[i].action) && !strcmp(d.key[i].value, c.key[i].value);
    for (int i = 0; same && i < c.n_pad; i++)
        same = !strcmp(d.pad[i].action, c.pad[i].action) && !strcmp(d.pad[i].value, c.pad[i].value);
    ck(same, "bindings survive render then parse");

    /* saved to disk and loaded back, alongside the ordinary keys */
    char tmpl[] = "/tmp/zel_cfgb_XXXXXX";
    char *base = mkdtemp(tmpl);
    ck(base != NULL, "mkdtemp");
    if (!base) return;
    snprintf(c.path, sizeof c.path, "%s/zeliard.ini", base);
    c.scale = 3;
    ck(config_save(&c) == 0, "save with bindings");
    Config e; config_load(&e, c.path);
    ck(e.scale == 3 && e.n_key == 4 && e.n_pad == 1 && !strcmp(e.key[0].value, "Z"), "bindings load back from the file");
    remove(c.path); rmdir(base);

    /* a full table does not overflow */
    Config f; config_init(&f);
    int ok = 1;
    for (int i = 0; i < CONFIG_MAX_BIND; i++) { char k[32]; snprintf(k, sizeof k, "key.a%d", i); ok &= config_set(&f, k, "X"); }
    ck(ok && f.n_key == CONFIG_MAX_BIND, "the table fills");
    ck(config_set(&f, "key.overflow", "X") == 0 && f.n_key == CONFIG_MAX_BIND, "a full table refuses more");
}

int main(void)
{
    t_defaults();  printf("ok\n");
    t_parse();     printf("ok\n");
    t_bad();       printf("ok\n");
    t_bools();     printf("ok\n");
    t_roundtrip(); printf("ok\n");
    t_save();      printf("ok\n");
    t_paths();     printf("ok\n");
    t_binds();     printf("ok\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
