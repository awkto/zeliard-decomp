/* input.c — key/controller remapping (issue #42, step 2).  See input.h. */
#include "input.h"
#ifdef HAVE_SDL
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------- the names */
static const char *const ACT_NAMES[ACT_COUNT] = {
    [ACT_LEFT] = "left", [ACT_RIGHT] = "right", [ACT_UP] = "up", [ACT_DOWN] = "down",
    [ACT_SWORD] = "sword", [ACT_MAGIC] = "magic", [ACT_MENU] = "menu", [ACT_PAUSE] = "pause",
    [ACT_FILTER] = "filter", [ACT_FULLSCREEN] = "fullscreen", [ACT_SCREENSHOT] = "screenshot",
    [ACT_MUSIC] = "music", [ACT_SFX] = "sfx",
    [ACT_QUICKSAVE] = "quicksave", [ACT_QUICKLOAD] = "quickload", [ACT_RESTORE] = "restore", [ACT_SPEED] = "speed", [ACT_TURBO] = "turbo", [ACT_MAP] = "map", [ACT_COMBO] = "combo", [ACT_AUTOFIRE] = "autofire", [ACT_ZOOM] = "zoom",
    [ACT_SAVE1] = "save1", [ACT_SAVE1 + 1] = "save2", [ACT_SAVE1 + 2] = "save3",
    [ACT_SAVE1 + 3] = "save4", [ACT_SAVE1 + 4] = "save5", [ACT_SAVE1 + 5] = "save6",
    [ACT_SAVE1 + 6] = "save7", [ACT_SAVE1 + 7] = "save8", [ACT_SAVE1 + 8] = "save9",
    [ACT_LOAD1] = "load1", [ACT_LOAD1 + 1] = "load2", [ACT_LOAD1 + 2] = "load3",
    [ACT_LOAD1 + 3] = "load4", [ACT_LOAD1 + 4] = "load5", [ACT_LOAD1 + 5] = "load6",
    [ACT_LOAD1 + 6] = "load7", [ACT_LOAD1 + 7] = "load8", [ACT_LOAD1 + 8] = "load9",
};

const char *input_action_name(int act) { return (act >= 0 && act < ACT_COUNT) ? ACT_NAMES[act] : "?"; }

static int action_by_name(const char *name)
{
    for (int a = 0; a < ACT_COUNT; a++) if (!strcmp(ACT_NAMES[a], name)) return a;
    return -1;
}

/* -------------------------------------------------------------- defaults */
static void kb_set(KeyBind *b, SDL_Keymod mod, ...)
{
    va_list ap; b->n = 0;
    va_start(ap, mod);
    for (;;) {
        int sc = va_arg(ap, int);
        if (sc < 0) break;
        if (b->n < INPUT_KEYS_MAX) { b->sc[b->n] = (SDL_Scancode)sc; b->mod[b->n] = mod; b->n++; }
    }
    va_end(ap);
}
static void pb_set(PadBind *b, ...)
{
    va_list ap; b->n = 0;
    va_start(ap, b);
    for (;;) {
        int btn = va_arg(ap, int);
        if (btn < 0) break;
        if (b->n < INPUT_KEYS_MAX) b->btn[b->n++] = (SDL_GameControllerButton)btn;
    }
    va_end(ap);
}

/* The keys the port used before there was a config: `town` picks the town
 * variant of the shared gameplay actions (Space talks in town, jumps in the
 * cavern; Alt cancels in town, casts in the cavern). */
static void default_key(KeyBind *b, int act, int town)
{
    switch (act) {
    case ACT_LEFT:  kb_set(b, 0, SDL_SCANCODE_LEFT, -1); return;
    case ACT_RIGHT: kb_set(b, 0, SDL_SCANCODE_RIGHT, -1); return;
    case ACT_DOWN:  kb_set(b, 0, SDL_SCANCODE_DOWN, -1); return;
    /* The original's fire keys (kernel INT 9, docs/SERVICES.md FF16) are Space =
     * bit 0 (sword / talk) and Alt = bit 1 (magic / cancel) in towns AND caverns;
     * Up jumps.  Space used to jump in the cavern here, which DOS players found
     * backwards. */
    case ACT_UP:     kb_set(b, 0, SDL_SCANCODE_UP, -1); return;
    case ACT_SWORD:
        if (town) kb_set(b, 0, SDL_SCANCODE_SPACE, SDL_SCANCODE_X, -1);
        else      kb_set(b, 0, SDL_SCANCODE_SPACE, SDL_SCANCODE_X, SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL, -1);
        return;
    case ACT_MAGIC:
        if (town) kb_set(b, 0, SDL_SCANCODE_C, SDL_SCANCODE_LALT, SDL_SCANCODE_RALT, -1);
        else      kb_set(b, 0, SDL_SCANCODE_C, SDL_SCANCODE_LALT, SDL_SCANCODE_RALT, -1);
        return;
    case ACT_MENU:       kb_set(b, 0, SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER, -1); return;
    case ACT_PAUSE:      kb_set(b, 0, SDL_SCANCODE_ESCAPE, -1); return;
    case ACT_FILTER:     kb_set(b, 0, SDL_SCANCODE_F3, SDL_SCANCODE_V, -1); return;
    case ACT_FULLSCREEN: kb_set(b, 0, SDL_SCANCODE_F11, SDL_SCANCODE_F, -1); return;
    case ACT_ZOOM:       kb_set(b, 0, SDL_SCANCODE_Z, -1); return;
    case ACT_SCREENSHOT: kb_set(b, 0, SDL_SCANCODE_F12, -1); return;
    case ACT_MUSIC:      kb_set(b, 0, SDL_SCANCODE_F1, -1); return;
    case ACT_SFX:        kb_set(b, 0, SDL_SCANCODE_F2, -1); return;
    case ACT_RESTORE:    kb_set(b, 0, SDL_SCANCODE_F7, SDL_SCANCODE_BACKSLASH, -1); return;
    case ACT_SPEED:      kb_set(b, 0, SDL_SCANCODE_F9, SDL_SCANCODE_BACKSPACE, -1); return;
    case ACT_TURBO:      kb_set(b, 0, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT, -1); return;
    case ACT_MAP:        kb_set(b, 0, SDL_SCANCODE_M, -1); return;
    case ACT_COMBO:      kb_set(b, 0, SDL_SCANCODE_A, -1); return;
    case ACT_AUTOFIRE:   kb_set(b, 0, SDL_SCANCODE_S, -1); return;
    case ACT_QUICKSAVE:  kb_set(b, KMOD_CTRL, SDL_SCANCODE_PERIOD, -1); return;
    case ACT_QUICKLOAD:  kb_set(b, KMOD_CTRL, SDL_SCANCODE_BACKSLASH, -1); return;
    default:
        /* Ctrl+1..9 / Alt+1..9, and the same on the keypad: a browser keeps
         * Ctrl/Alt+digit for tab switching, but not the keypad digits, so the
         * web build always has a way to reach every slot (port/README.md). */
        if (act >= ACT_SAVE1 && act <= ACT_SAVE1 + 8) {
            kb_set(b, KMOD_CTRL, SDL_SCANCODE_1 + (act - ACT_SAVE1), -1);
            b->sc[b->n] = SDL_SCANCODE_KP_1 + (act - ACT_SAVE1); b->mod[b->n++] = KMOD_CTRL;
        }
        else if (act >= ACT_LOAD1 && act <= ACT_LOAD1 + 8) {
            kb_set(b, KMOD_ALT, SDL_SCANCODE_1 + (act - ACT_LOAD1), -1);
            b->sc[b->n] = SDL_SCANCODE_KP_1 + (act - ACT_LOAD1); b->mod[b->n++] = KMOD_ALT;
        }
        else b->n = 0;
        return;
    }
}

static void default_pad(PadBind *b, int act)
{
    switch (act) {
    case ACT_LEFT:  pb_set(b, SDL_CONTROLLER_BUTTON_DPAD_LEFT, -1); return;
    case ACT_RIGHT: pb_set(b, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, -1); return;
    case ACT_UP:    pb_set(b, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_A, -1); return;
    case ACT_DOWN:  pb_set(b, SDL_CONTROLLER_BUTTON_DPAD_DOWN, -1); return;
    case ACT_SWORD: pb_set(b, SDL_CONTROLLER_BUTTON_X, -1); return;
    case ACT_MAGIC: pb_set(b, SDL_CONTROLLER_BUTTON_Y, -1); return;
    case ACT_MENU:  pb_set(b, SDL_CONTROLLER_BUTTON_START, -1); return;
    case ACT_PAUSE: pb_set(b, SDL_CONTROLLER_BUTTON_BACK, -1); return;
    case ACT_TURBO: pb_set(b, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, -1); return;
    default: b->n = 0; return;
    }
}

/* --------------------------------------------------------------- parsing */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return s;
}

static int ci_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

/* "Ctrl+Shift+X" -> the modifier mask, leaving *key at "X".  `spec` is written
 * to.  Only the exact words Ctrl/Alt/Shift/Gui/Super/Meta count as modifiers, so
 * "Left Ctrl" stays a key name. */
static SDL_Keymod split_mods(char *spec, char **key)
{
    SDL_Keymod mod = 0;
    char *p = spec;
    for (;;) {
        char *plus = strchr(p, '+');
        if (!plus) break;
        *plus = 0;
        if      (ci_eq(p, "ctrl"))  mod |= KMOD_CTRL;
        else if (ci_eq(p, "alt"))   mod |= KMOD_ALT;
        else if (ci_eq(p, "shift")) mod |= KMOD_SHIFT;
        else if (ci_eq(p, "gui") || ci_eq(p, "super") || ci_eq(p, "meta")) mod |= KMOD_GUI;
        else { *plus = '+'; break; }        /* not a modifier word: the '+' is part of the key */
        p = plus + 1;
    }
    *key = p;
    return mod;
}

/* Parse one action's value (comma-separated "Mod+Key" specs) into `b`.  Warns
 * and skips a spec whose key name SDL does not know.  Returns the number of
 * keys accepted. */
static int parse_keys(KeyBind *b, const char *action, const char *value)
{
    b->n = 0;
    char buf[CONFIG_VAL_MAX * 2];
    snprintf(buf, sizeof buf, "%s", value);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        char *spec = trim(tok);
        if (!*spec) continue;
        char *key;
        SDL_Keymod mod = split_mods(spec, &key);
        key = trim(key);
        SDL_Scancode sc = SDL_GetScancodeFromName(key);
        if (sc == SDL_SCANCODE_UNKNOWN) {
            fprintf(stderr, "[input] key.%s: unknown key name \"%s\" (ignored)\n", action, key);
            continue;
        }
        if (b->n < INPUT_KEYS_MAX) { b->sc[b->n] = sc; b->mod[b->n] = mod; b->n++; }
    }
    return b->n;
}

static int parse_pad(PadBind *b, const char *action, const char *value)
{
    b->n = 0;
    char buf[CONFIG_VAL_MAX * 2];
    snprintf(buf, sizeof buf, "%s", value);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        char *name = trim(tok);
        if (!*name) continue;
        SDL_GameControllerButton btn = SDL_GameControllerGetButtonFromString(name);
        if (btn == SDL_CONTROLLER_BUTTON_INVALID) {
            fprintf(stderr, "[input] pad.%s: unknown button name \"%s\" (ignored)\n", action, name);
            continue;
        }
        if (b->n < INPUT_KEYS_MAX) b->btn[b->n++] = btn;
    }
    return b->n;
}

/* ------------------------------------------------------------------- API */
int input_init(Input *in, const Config *cfg)
{
    for (int a = 0; a < ACT_COUNT; a++) {
        default_key(&in->cav[a], a, 0);
        default_key(&in->town[a], a, 1);
        default_pad(&in->pad[a], a);
    }
    int n = 0;
    for (int i = 0; i < cfg->n_key; i++) {
        int a = action_by_name(cfg->key[i].action);
        if (a < 0) { fprintf(stderr, "[input] key.%s: unknown action (ignored)\n", cfg->key[i].action); continue; }
        KeyBind b;
        if (parse_keys(&b, cfg->key[i].action, cfg->key[i].value) == 0) continue;   /* keep the default */
        in->cav[a] = b; in->town[a] = b; n++;
    }
    for (int i = 0; i < cfg->n_pad; i++) {
        int a = action_by_name(cfg->pad[i].action);
        if (a < 0) { fprintf(stderr, "[input] pad.%s: unknown action (ignored)\n", cfg->pad[i].action); continue; }
        PadBind b;
        if (parse_pad(&b, cfg->pad[i].action, cfg->pad[i].value) == 0) continue;
        in->pad[a] = b; n++;
    }
    return n;
}

static int is_modifier_key(SDL_Scancode sc)
{
    return sc >= SDL_SCANCODE_LCTRL && sc <= SDL_SCANCODE_RGUI;
}

int input_key_down(const Input *in, int act, int town, const uint8_t *keystate, int combo)
{
    const KeyBind *b = town ? &in->town[act] : &in->cav[act];
    for (int i = 0; i < b->n; i++) {
        if (combo && is_modifier_key(b->sc[i])) continue;
        if (keystate[b->sc[i]]) return 1;
    }
    return 0;
}

int input_pad_event_match(const Input *in, int act, int button)
{
    const PadBind *b = &in->pad[act];
    for (int i = 0; i < b->n; i++) if ((int)b->btn[i] == button) return 1;
    return 0;
}

int input_pad_down(const Input *in, int act, SDL_GameController *pad)
{
    if (!pad) return 0;
    const PadBind *b = &in->pad[act];
    for (int i = 0; i < b->n; i++) if (SDL_GameControllerGetButton(pad, b->btn[i])) return 1;
    return 0;
}

/* KMOD_CTRL is Left|Right: a required group is satisfied by either side. */
static int mods_held(int have, int need)
{
    static const int groups[4] = { KMOD_CTRL, KMOD_ALT, KMOD_SHIFT, KMOD_GUI };
    for (int g = 0; g < 4; g++)
        if ((need & groups[g]) && !(have & groups[g])) return 0;
    return 1;
}

int input_event_match(const Input *in, int act, const SDL_KeyboardEvent *k)
{
    const KeyBind *b = &in->cav[act];               /* hotkeys are engine-independent */
    for (int i = 0; i < b->n; i++) {
        /* plain \ (Restore) and Backspace (speed) must not fire under Ctrl/Alt:
         * Ctrl+\ is quick-load */
        if (!b->mod[i] && (b->sc[i] == SDL_SCANCODE_BACKSLASH || b->sc[i] == SDL_SCANCODE_BACKSPACE)
            && (k->keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) continue;
        if (k->keysym.scancode == b->sc[i] && mods_held(k->keysym.mod, b->mod[i]))
            return 1;
    }
    return 0;
}

#endif /* HAVE_SDL */
