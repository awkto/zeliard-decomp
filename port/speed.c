/* speed.c -- F9 "Speed change" (speed.h) */
#include "speed.h"
#include "text.h"
#include "audio.h"

static const uint8_t S_SPEED[] = "Speed change\rSelect 0-9:\xFF";    /* STICK 0845 */

void speed_open(SpeedDlg *s, int ff33)
{
    s->state = SP_DIGIT;
    s->speed = ff33 >= 1 && ff33 <= 10 ? ff33 : SPEED_DEFAULT;
    s->digit = 10 - s->speed;
    audio_sfx_request(2);                       /* dialog_open: [FF75] = 2 */
}

int speed_key(SpeedDlg *s, int key, int digit)
{
    if (s->state == SP_DIGIT) {
        if (key == SPK_CANCEL) { s->state = SP_CLOSED; return 0; }
        if (key != SPK_DIGIT || digit < 0 || digit > 9) return 0;   /* read_digit loops */
        s->digit = digit;
        s->speed = 10 - digit;
        audio_sfx_request(1);                   /* 07DB: [FF75] = 1 */
        s->state = SP_ANY;
        return s->speed;
    }
    if (s->state == SP_ANY && key != SPK_NONE) s->state = SP_CLOSED;   /* dialog_close */
    return 0;
}

void speed_draw(const SpeedDlg *s, const Shell *sh, uint8_t *fb)
{
    if (!s->state) return;
    const TextFont *f = &sh->tfont;
    vid_window(fb, 0xFF, 0x1A, 0x46, 0x1E, 0x28);       /* dialog_open's box, as restore.c */
    vid_puts(fb, f, S_SPEED, 0x74, 0x52);
    if (s->state == SP_ANY) vid_putchar(fb, f, (uint8_t)('0' + s->digit), 1, 0xCC, 0x5A);
}
