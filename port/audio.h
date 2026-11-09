/* audio.h — sound output: the OPL2 music driver, the SND*.DRV effect driver
 * and the SDL2 / WAV back ends.  docs/MUSIC.md is the spec for the score side,
 * disasm/SND{ADLIB,STD}.asm for the effects.
 *
 * Back ends: AdLib (OPL2), PC speaker, Tandy / PCjr (SN76496, MSCJR.DRV +
 * SNDJR.DRV; sn76496.c, sndjr.c).
 *
 * Nothing here runs unless main.c calls audio_init(): the test binaries and
 * `--headless` without `--dump-audio` never touch it, so they stay silent and
 * deterministic. */
#ifndef ZEL_AUDIO_H
#define ZEL_AUDIO_H
#include <stdint.h>
#include <stddef.h>

enum { AUDIO_ADLIB = 0, AUDIO_SPEAKER = 1, AUDIO_TANDY = 2 };

/* Open the sound hardware (SDL2) and load SND*.DRV from `dir`.  `backend` is
 * AUDIO_ADLIB, AUDIO_SPEAKER or AUDIO_TANDY; `want_sdl` = 0 leaves the device closed (the
 * --dump-audio path).  Returns 0 when audio is running. */
int  audio_init(const char *dir, int backend, int want_sdl);
void audio_shutdown(void);
int  audio_active(void);
const char *audio_backend_name(void);
/* the spellings used by --audio and the `audio=` ini key: adlib speaker tandy (also pcjr, pc-speaker, opl2).
 * -1 = unknown. */
int  audio_backend_by_name(const char *s);
const char *audio_backend_key(int backend);

/* Render to a 16-bit mono WAV instead of a sound card (deterministic). */
int  audio_dump_open(const char *path);
void audio_dump_close(void);
/* Advance the drivers by `ms` of audio; only does anything while dumping. */
void audio_advance_ms(double ms);
/* host fast-forward (#130): run the INT 8 clock `mult` times faster (1 = normal) -- tempo up, pitch unchanged.
 * Thread-safe (the audio callback reads it); takes effect from the next callback chunk or audio_advance_ms. */
void audio_set_turbo(int mult);
int  audio_turbo(void);

/* fight.bin 7E93: the level record's music index (bits 1-4 of byte +0), also
 * used for the town maps.  Restarts the score only when the index changes. */
void audio_music(int music_idx);
/* The cavern entry in fight.bin's three steps (7E93 / 7F5D / 6270), docs/MUSIC.md "Cavern entry":
 * level() parks a changed index and starts fading the old score, commit() (after the walk-in)
 * stops it and loads the new one, frame_end() (the end of the first frame) starts it.
 * settle() is commit() plus, for a boss map, the immediate start of 6085..60E3. */
void audio_music_level(int music_idx);
void audio_music_town_queue(int music_idx); /* as _town_idx, but audio_music_frame_end() starts it */
void audio_music_tear_exit(int to_mpa0);    /* 7C39: the Tear exit door parks [C8], stops, queues the score; into MPA0 nothing starts it (oracle) */
void audio_music_town_idx(int music_idx);   /* the death return: [C8] only, the Sage restarts the score */
void audio_music_commit(void);
void audio_music_frame_end(void);
void audio_music_settle(int boss_map);
/* the game's view of the driver, device-independent, for the harness trace and the fade clock */
void audio_music_model_ticks(int n);
uint8_t audio_model_c8(void);       /* [C8] */
uint8_t audio_model_fade(void);     /* [FF24] */
uint8_t audio_model_level(void);    /* [FF25] */
uint8_t audio_model_stopped(void);  /* [FF26] */
uint8_t audio_model_restart(void);  /* [9F02] */
/* force one score and ignore later level changes (--music N, for listening
 * tests and --dump-audio verification) */
void audio_music_force(int music_idx);
/* GAME.BIN A080: the boot path runs opdemo before any level record is read, so
 * hold the level score (audio_music) while the port builds its scaffold Game
 * and the intro runs.  audio_music_play_res(), the cutscenes' own INT 60h
 * AX=0, is never held.  audio_music_force() clears the hold. */
void audio_music_hold(int on);
void audio_music_restart(int music_idx);   /* stop + start: town.bin 6EF8 after a shop */
void audio_music_stop(void);          /* INT 60h AX=1 */
/* the cutscenes play scores that are not in the 9E53 table (zopn/zend/mfan):
 * load ZELRES{archive+1}[index] and start it, exactly as INT 60h AX=0 does. */
void audio_music_play_res(int archive, int index);
int  audio_music_stopped(void);       /* [FF26]: the score has ended */
/* a silent score player stepped by game frames (10 driver ticks each), for [FF21] as a game
 * input (MAO2's start gate): independent of the audio device, deterministic in frames */
int  audio_clock_start(const char *dir, int music_idx, int pre_ticks);
void audio_clock_stop(void);
int  audio_clock_on(void);
void audio_clock_frame(void);
int  audio_clock_sync0(void);
int  audio_music_sync0(void);         /* [FF21]: bumped by score opcode F1 */
void audio_music_sync0_clear(void);
void audio_music_pause(int on);       /* INT 60h AX=3 */
void audio_music_fade(int rate);      /* the game poking FF24 */
/* STICK's F1 / F2 hotkeys (docs/SERVICES.md 01E3): INT 60h AX=2 and FF27 */
int  audio_music_enable(int on);
int  audio_sfx_enable(int on);
/* FF75: hand a sound-effect id to the sound driver */
void audio_sfx_request(int id);

/* ---- introspection, used by test_audio.c ---- */
typedef struct MusicRes { const char *name; int archive, index; } MusicRes;
const MusicRes *audio_music_table(int *n);      /* fight.bin 9E53, 14 entries */
int  audio_sfx_count(void);                     /* effects in the loaded SND*.DRV */
int  audio_sfx_entry(int id, int *prio, unsigned *t0, unsigned *t1, unsigned *durtab);
int  audio_sfx_load(const char *dir, int backend);   /* load SND*.DRV only */
double audio_int8_hz(void);
/* Tandy back end: call `fn` for every byte the music and sound drivers write to
 * the SN76496 (port C0h), with the INT 8 tick it was written on; and run n INT 8
 * ticks without rendering any audio.  Both are for test_audio.c. */
void audio_chip_tap(void (*fn)(void *u, int tick, uint8_t byte), void *u);
void audio_run_ticks(int n);
int  audio_int8_count(void);

#endif
