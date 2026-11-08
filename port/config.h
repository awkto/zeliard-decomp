/* config.h — the player's settings file (issue #42): a small key=value ini the
 * front end remembers between runs.
 *
 * The port's defaults are all command-line flags today, so nothing survives a
 * restart: the window scale, the video mode, the display filter, the game
 * folder.  This module is the shell side of that — pure C, no SDL, no engine —
 * so the test binaries can link and unit-test it.  `main.c` resolves the path
 * (SDL_GetPrefPath("awkto","zeliard")/zeliard.ini when SDL is present, else
 * config_default_path()) and owns the policy of *when* to write; the file
 * format itself lives here.
 *
 * Every field carries an "unset" sentinel (-1 for the ints, "" for the
 * strings) so a missing key can be told apart from an explicit value: the
 * command line overrides the config, and the config overrides the built-in
 * defaults, in that order.  Nothing here touches the engine. */
#ifndef ZEL_CONFIG_H
#define ZEL_CONFIG_H
#include <stddef.h>

#define CONFIG_PATH_MAX 1024
#define CONFIG_STR_MAX   32
#define CONFIG_VAL_MAX   64         /* a binding's value: "C, Left Alt, Right Alt, Backspace" */
#define CONFIG_MAX_BIND  48         /* key.<action> / pad.<action> pairs kept */

/* One `key.<action> = <value>` (or `pad.<action> = <value>`) line: the action
 * name and the value text, kept verbatim so the file round-trips.  The port
 * resolves the value into scancodes/controller buttons at run time (input.c);
 * config.c only carries the text, so it stays SDL-free. */
typedef struct ConfigBind {
    char action[CONFIG_STR_MAX];
    char value[CONFIG_VAL_MAX];
} ConfigBind;

typedef struct Config {
    char dir[CONFIG_PATH_MAX];          /* the game folder; "" = unset */
    int  scale;                         /* window scale; -1 = unset */
    int  fullscreen;                    /* 0/1; -1 = unset */
    char video[CONFIG_STR_MAX];         /* --video spelling; "" = unset */
    char filter[CONFIG_STR_MAX];        /* --filter spelling; "" = unset */
    int  zoom;                          /* Z state: 0/1; -1 = unset */
    int  smooth;                        /* T state (smooth scrolling, #127): 0/1; -1 = unset */
    char aspect[CONFIG_STR_MAX];        /* 4:3 / 1:1; "" = unset */
    int  music;                         /* F1 state; -1 = unset, 0 = off, 1 = on */
    int  sfx;                           /* F2 state; -1 = unset */
    int  speaker;                       /* 1 = PC speaker, 0 = AdLib; -1 = unset (older files; `audio` wins) */
    char audio[CONFIG_STR_MAX];         /* sound back end: adlib / speaker / tandy; "" = unset */
    int  volume;                        /* carried if present; -1 = unset */
    int  touch_size;                    /* touch.size, percent of the default; -1 = unset (#54) */
    int  touch_opacity;                 /* touch.opacity, percent; -1 = unset (#54) */
    /* #42 step 2: key remapping.  `key.<action>` and `pad.<action>` are kept in
     * the order they were read; a later line for the same action replaces the
     * value in place.  n_key/n_pad are the counts. */
    /* #55: save sync.  `sync` turns the (step 2) lifecycle hooks on; the --sync
     * tool runs work regardless.  The key falls back to the sync-key file. */
    int  sync;                          /* 0/1; -1 = unset */
    char sync_base[256];                /* server URL; "" = unset */
    char sync_key[64];                  /* ABCD-EFGH; "" = unset */
    char sync_token[128];               /* optional bearer token; "" = unset */
    ConfigBind key[CONFIG_MAX_BIND]; int n_key;
    ConfigBind pad[CONFIG_MAX_BIND]; int n_pad;
    char path[CONFIG_PATH_MAX];         /* the ini's own location (load/save) */
} Config;

/* Zero `c` and set every "unset" sentinel. */
void config_init(Config *c);

/* Parse `text` as the ini body; returns the number of keys recognised.  Blank
 * lines, `#`/`;` comments, lines without `=` and unknown keys are ignored, and
 * an unparseable value leaves its field unset (config_set returns 0). */
int  config_parse(Config *c, const char *text);

/* Apply one key/value pair.  Returns 1 when the key was known and the value
 * accepted, 0 otherwise (unknown key or a bad value). */
int  config_set(Config *c, const char *key, const char *val);

/* Serialise the set fields of `c` to `out` (a NUL-terminated ini body). */
void config_render(const Config *c, char *out, size_t n);

/* Load `path` into `c` (which is init'd here).  A missing file is not an
 * error: it returns 0 with everything unset.  -1 only on a real read error. */
int  config_load(Config *c, const char *path);

/* Write `c` to c->path atomically: a sibling `.tmp` file, then rename.  Creates
 * the parent directory if needed.  Returns 0 on success. */
int  config_save(const Config *c);

/* The no-SDL fallback path: $XDG_CONFIG_HOME/zeliard/zeliard.ini, else
 * $HOME/.config/zeliard/zeliard.ini, else `zeliard.ini` next to argv0. */
void config_default_path(char *out, size_t n, const char *argv0);

#endif
