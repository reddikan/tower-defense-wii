/*
 * TOWER DEFENSE - procedurally synthesized sound effects.
 *
 * No audio assets are needed: every effect is generated at start-up from a few
 * oscillators (square / triangle / saw / sine) and filtered noise, in the style
 * of old 8-bit consoles. Samples are 16-bit signed mono at SFX_RATE Hz in the
 * host's native byte order (big-endian on the Wii, which is what ASND wants).
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define SFX_RATE 22050

enum {
    SFX_MOVE, SFX_SELECT, SFX_BUILD, SFX_UPGRADE, SFX_SELL, SFX_ERROR,
    SFX_ARROW, SFX_CANNON, SFX_FROST, SFX_EXPLODE, SFX_KILL, SFX_LEAK,
    SFX_WAVE, SFX_BOSS, SFX_CLEAR, SFX_START, SFX_PAUSE, SFX_OVER, SFX_WIN,
    SFX_COUNT
};

void           audio_init(void);               /* synthesize every effect (call once) */
const int16_t *audio_data(int id);             /* 32-byte aligned sample buffer */
int            audio_len(int id);              /* length in samples (multiple of 16) */
int            audio_volume(int id);           /* suggested playback volume 0..255 */

#endif
