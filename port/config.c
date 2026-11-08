/* config.c — the player's settings file (issue #42).  See config.h. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

#ifdef _WIN32
#include <direct.h>
#define CFG_MKDIR(d) _mkdir(d)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define CFG_MKDIR(d) mkdir((d), 0777)
#endif

void config_init(Config *c)
{
    memset(c, 0, sizeof *c);
    c->scale = c->fullscreen = c->zoom = c->smooth = c->music = c->sfx = -1;
    c->speaker = c->volume = -1;
    c->touch_size = c->touch_opacity = -1;
    c->sync = -1;
}

/* --------------------------------------------------------------- parsing */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

/* 1/0, on/off, true/false, yes/no (either case).  Returns 1 when recognised. */
static int parse_bool(const char *v, int *out)
{
    if (!strcmp(v, "1") || !strcmp(v, "on")  || !strcmp(v, "true")  || !strcmp(v, "yes")) { *out = 1; return 1; }
    if (!strcmp(v, "0") || !strcmp(v, "off") || !strcmp(v, "false") || !strcmp(v, "no"))  { *out = 0; return 1; }
    return 0;
}

static int parse_int(const char *v, int *out)
{
    char *e;
    long n = strtol(v, &e, 10);
    if (e == v || *e) return 0;
    *out = (int)n;
    return 1;
}

static void copy_str(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src);
}

/* key.<action> / pad.<action>: keep the action and the raw value text.  A
 * repeated action replaces its value in place (so the last line wins, as a
 * hand-edited ini would expect); new actions append while there is room. */
static int bind_set(ConfigBind *list, int *n, const char *action, const char *val)
{
    if (!*action || !*val) return 0;
    for (int i = 0; i < *n; i++)
        if (!strcmp(list[i].action, action)) { copy_str(list[i].value, sizeof list[i].value, val); return 1; }
    if (*n >= CONFIG_MAX_BIND) return 0;
    copy_str(list[*n].action, sizeof list[*n].action, action);
    copy_str(list[*n].value, sizeof list[*n].value, val);
    (*n)++;
    return 1;
}

int config_set(Config *c, const char *key, const char *val)
{
    if (!strncmp(key, "key.", 4))       return bind_set(c->key, &c->n_key, key + 4, val);
    if (!strncmp(key, "pad.", 4))       return bind_set(c->pad, &c->n_pad, key + 4, val);
    if (!strcmp(key, "dir"))            { copy_str(c->dir, sizeof c->dir, val); return 1; }
    if (!strcmp(key, "scale"))          { int v; if (!parse_int(val, &v)) return 0; c->scale = v; return 1; }
    if (!strcmp(key, "fullscreen"))     { int v; if (!parse_bool(val, &v)) return 0; c->fullscreen = v; return 1; }
    if (!strcmp(key, "zoom"))           { int v; if (!parse_bool(val, &v)) return 0; c->zoom = v; return 1; }
    if (!strcmp(key, "smooth"))         { int v; if (!parse_bool(val, &v)) return 0; c->smooth = v; return 1; }
    if (!strcmp(key, "video"))          { copy_str(c->video, sizeof c->video, val); return 1; }
    if (!strcmp(key, "filter"))         { copy_str(c->filter, sizeof c->filter, val); return 1; }
    if (!strcmp(key, "aspect"))         { copy_str(c->aspect, sizeof c->aspect, val); return 1; }
    if (!strcmp(key, "music"))          { int v; if (!parse_bool(val, &v)) return 0; c->music = v; return 1; }
    if (!strcmp(key, "sfx"))            { int v; if (!parse_bool(val, &v)) return 0; c->sfx = v; return 1; }
    if (!strcmp(key, "audio"))          { copy_str(c->audio, sizeof c->audio, val); return 1; }
    if (!strcmp(key, "speaker"))        { int v; if (!parse_bool(val, &v)) return 0; c->speaker = v; return 1; }
    if (!strcmp(key, "volume"))         { int v; if (!parse_int(val, &v)) return 0; c->volume = v; return 1; }
    if (!strcmp(key, "touch.size"))     { int v; if (!parse_int(val, &v)) return 0; c->touch_size = v; return 1; }
    if (!strcmp(key, "touch.opacity"))  { int v; if (!parse_int(val, &v)) return 0; c->touch_opacity = v; return 1; }
    if (!strcmp(key, "sync"))           { int v; if (!parse_bool(val, &v)) return 0; c->sync = v; return 1; }
    if (!strcmp(key, "sync_base"))      { copy_str(c->sync_base, sizeof c->sync_base, val); return 1; }
    if (!strcmp(key, "sync_key"))       { copy_str(c->sync_key, sizeof c->sync_key, val); return 1; }
    if (!strcmp(key, "sync_token"))     { copy_str(c->sync_token, sizeof c->sync_token, val); return 1; }
    return 0;
}

int config_parse(Config *c, const char *text)
{
    int n = 0;
    char line[CONFIG_PATH_MAX + CONFIG_STR_MAX + 8];
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t raw = eol ? (size_t)(eol - p) : strlen(p);
        size_t len = raw < sizeof line ? raw : sizeof line - 1;
        memcpy(line, p, len);
        line[len] = 0;
        p = eol ? eol + 1 : p + raw;

        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;                      /* a bad line: no key=value */
        *eq = 0;
        char *key = trim(s);
        char *val = trim(eq + 1);
        if (*key && config_set(c, key, val)) n++;
    }
    return n;
}

void config_render(const Config *c, char *out, size_t n)
{
    size_t o = 0;
    /* snprintf into a growing window; a short buffer just truncates cleanly */
    #define EMIT(...) do { \
        if (o < n) { int _w = snprintf(out + o, n - o, __VA_ARGS__); \
                     if (_w > 0) o += (size_t)_w; } } while (0)
    EMIT("# zeliard.ini - the Zeliard port's settings (issue #42)\n");
    EMIT("# Written by the port on exit; command-line flags override it.\n");
    if (c->dir[0])      EMIT("dir=%s\n", c->dir);
    if (c->scale >= 0)  EMIT("scale=%d\n", c->scale);
    if (c->fullscreen >= 0) EMIT("fullscreen=%d\n", c->fullscreen);
    if (c->zoom >= 0)   EMIT("zoom=%d\n", c->zoom);
    if (c->smooth >= 0) EMIT("smooth=%d\n", c->smooth);
    if (c->video[0])    EMIT("video=%s\n", c->video);
    if (c->filter[0])   EMIT("filter=%s\n", c->filter);
    if (c->aspect[0])   EMIT("aspect=%s\n", c->aspect);
    if (c->music >= 0)  EMIT("music=%d\n", c->music);
    if (c->sfx >= 0)    EMIT("sfx=%d\n", c->sfx);
    if (c->speaker >= 0) EMIT("speaker=%d\n", c->speaker);
    if (c->audio[0])    EMIT("audio=%s\n", c->audio);
    if (c->volume >= 0) EMIT("volume=%d\n", c->volume);
    if (c->touch_size >= 0)    EMIT("touch.size=%d\n", c->touch_size);
    if (c->touch_opacity >= 0) EMIT("touch.opacity=%d\n", c->touch_opacity);
    if (c->sync >= 0)   EMIT("sync=%d\n", c->sync);
    if (c->sync_base[0])  EMIT("sync_base=%s\n", c->sync_base);
    if (c->sync_key[0])   EMIT("sync_key=%s\n", c->sync_key);
    if (c->sync_token[0]) EMIT("sync_token=%s\n", c->sync_token);
    for (int i = 0; i < c->n_key; i++) EMIT("key.%s=%s\n", c->key[i].action, c->key[i].value);
    for (int i = 0; i < c->n_pad; i++) EMIT("pad.%s=%s\n", c->pad[i].action, c->pad[i].value);
    #undef EMIT
    if (n) out[o < n ? o : n - 1] = 0;
}

/* ------------------------------------------------------------------- I/O */
int config_load(Config *c, const char *path)
{
    config_init(c);
    copy_str(c->path, sizeof c->path, path);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;                           /* absent is fine: defaults */
    char text[8192];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    config_parse(c, text);
    return 0;
}

/* mkdir -p for the ini's parent directory (SDL_GetPrefPath already made its
 * own; the fallback path needs this). */
static void make_parents(const char *path)
{
    char tmp[CONFIG_PATH_MAX];
    copy_str(tmp, sizeof tmp, path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char save = *p;
            *p = 0;
            CFG_MKDIR(tmp);
            *p = save;
        }
    }
}

int config_save(const Config *c)
{
    char text[8192];
    config_render(c, text, sizeof text);
    make_parents(c->path);

    char tmp[CONFIG_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", c->path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    size_t n = strlen(text);
    if (fwrite(text, 1, n, f) != n) { fclose(f); remove(tmp); return -1; }
    if (fclose(f) != 0) { remove(tmp); return -1; }
#ifdef _WIN32
    remove(c->path);                            /* Windows rename won't replace */
#endif
    if (rename(tmp, c->path) != 0) { remove(tmp); return -1; }
    return 0;
}

void config_default_path(char *out, size_t n, const char *argv0)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) { snprintf(out, n, "%s/zeliard/zeliard.ini", xdg); return; }
    const char *home = getenv("HOME");
    if (home && *home) { snprintf(out, n, "%s/.config/zeliard/zeliard.ini", home); return; }
    if (argv0 && *argv0) {
        const char *slash = strrchr(argv0, '/');
#ifdef _WIN32
        const char *bs = strrchr(argv0, '\\');
        if (bs && (!slash || bs > slash)) slash = bs;
#endif
        if (slash && slash != argv0) {
            snprintf(out, n, "%.*s/zeliard.ini", (int)(slash - argv0), argv0);
            return;
        }
    }
    snprintf(out, n, "zeliard.ini");
}
