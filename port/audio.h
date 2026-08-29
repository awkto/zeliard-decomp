/* audio.h — sound output: the OPL2 music driver, the SND*.DRV effect driver
 * and the SDL2 / WAV back ends.  docs/MUSIC.md is the spec for the score side,
 * disasm/SND{ADLIB,STD}.asm for the effects.
 *
 * Nothing here runs unless main.c calls audio_init(): the test binaries and
 * `--headless` without `--dump-audio` never touch it, so they stay silent and
 * deterministic. */
#ifndef ZEL_AUDIO_H
#define ZEL_AUDIO_H
#include <stdint.h>
#include <stddef.h>

enum { AUDIO_ADLIB = 0, AUDIO_SPEAKER = 1 };

/* Open the sound hardware (SDL2) and load SND*.DRV from `dir`.  `backend` is
 * AUDIO_ADLIB or AUDIO_SPEAKER; `want_sdl` = 0 leaves the device closed (the
 * --dump-audio path).  Returns 0 when audio is running. */
int  audio_init(const char *dir, int backend, int want_sdl);
void audio_shutdown(void);
int  audio_active(void);
const char *audio_backend_name(void);

/* Render to a 16-bit mono WAV instead of a sound card (deterministic). */
int  audio_dump_open(const char *path);
void audio_dump_close(void);
/* Advance the drivers by `ms` of audio; only does anything while dumping. */
void audio_advance_ms(double ms);

/* fight.bin 7E93: the level record's music index (bits 1-4 of byte +0), also
 * used for the town maps.  Restarts the score only when the index changes. */
void audio_music(int music_idx);
/* force one score and ignore later level changes (--music N, for listening
 * tests and --dump-audio verification) */
void audio_music_force(int music_idx);
void audio_music_stop(void);          /* INT 60h AX=1 */
/* the cutscenes play scores that are not in the 9E53 table (zopn/zend/mfan):
 * load ZELRES{archive+1}[index] and start it, exactly as INT 60h AX=0 does. */
void audio_music_play_res(int archive, int index);
int  audio_music_stopped(void);       /* [FF26]: the score has ended */
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

#endif
