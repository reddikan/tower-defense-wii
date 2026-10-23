/*
 * TOWER DEFENSE - sound effect synthesis. See audio.h.
 */
#include "audio.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAXLEN 24576                       /* ~1.1 s per effect */

static int16_t SFX[SFX_COUNT][MAXLEN] __attribute__((aligned(32)));
static int     sfx_len[SFX_COUNT];
static float   mixbuf[MAXLEN];
static int     mixend;
static uint32_t rng = 0x2545F491u;
static int     ready;

/* relative loudness (every effect is peak-normalised first, then scaled by this) */
static const uint8_t VOL[SFX_COUNT] = {
    /* MOVE */ 60,  /* SELECT */ 80, /* BUILD */ 200, /* UPGRADE */ 170, /* SELL */ 150, /* ERROR */ 150,
    /* ARROW */ 85, /* CANNON */ 230, /* FROST */ 110, /* EXPLODE */ 230, /* KILL */ 100, /* LEAK */ 220,
    /* WAVE */ 190, /* BOSS */ 240, /* CLEAR */ 200, /* START */ 190, /* PAUSE */ 120, /* OVER */ 220,
    /* WIN */ 220, /* THUNDER */ 235
};

enum { W_SQ, W_TRI, W_SAW, W_SIN, W_NOISE };

static float frand(void)                   /* -1 .. 1 */
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(rng >> 8) / 8388608.0f - 1.0f;
}

/*
 * One note / sound element, mixed into the effect being built.
 * start, dur  : seconds
 * f0 -> f1    : pitch sweep in Hz (for W_NOISE this is the low-pass cutoff sweep)
 * duty        : pulse width for W_SQ
 * decay       : envelope exponent, 1 = linear fade out, 2 = snappier, 0 = sustained
 */
static void note(float start, float dur, float f0, float f1, float vol, int wave, float duty, float decay)
{
    int s0 = (int)(start * SFX_RATE), n = (int)(dur * SFX_RATE);
    float ph = 0.0f, lp = 0.0f;
    for (int i = 0; i < n && s0 + i < MAXLEN; i++) {
        float t = (float)i / (float)n;
        float f = f0 + (f1 - f0) * t;
        float env = (decay <= 0.0f) ? 1.0f : powf(1.0f - t, decay);
        float att = (float)i / (0.003f * SFX_RATE);
        if (att > 1.0f) att = 1.0f;
        float rel = (1.0f - t) * (float)n / (0.004f * SFX_RATE);   /* tiny release, avoids clicks */
        if (rel > 1.0f) rel = 1.0f;
        float v;
        switch (wave) {
        case W_SQ:  v = (ph < duty) ? 1.0f : -1.0f; break;
        case W_TRI: v = 4.0f * fabsf(ph - 0.5f) - 1.0f; break;
        case W_SAW: v = 2.0f * ph - 1.0f; break;
        case W_SIN: v = sinf(ph * 2.0f * (float)M_PI); break;
        default: {
            float k = f * 2.0f * (float)M_PI / (float)SFX_RATE;
            if (k > 1.0f) k = 1.0f;
            lp += (frand() - lp) * k;
            v = lp / (0.577f * sqrtf(k / (2.0f - k))) * 0.6f;   /* keep loudness roughly constant */
            if (v > 1.5f) v = 1.5f;
            if (v < -1.5f) v = -1.5f;
        }
        }
        ph += f / (float)SFX_RATE;
        if (ph >= 1.0f) ph -= 1.0f;
        mixbuf[s0 + i] += v * env * att * rel * vol;
        if (s0 + i + 1 > mixend) mixend = s0 + i + 1;
    }
}

static void begin(void)
{
    memset(mixbuf, 0, sizeof mixbuf);
    mixend = 0;
}

static void finish(int id)
{
    float peak = 0.0f;
    for (int i = 0; i < mixend; i++) if (fabsf(mixbuf[i]) > peak) peak = fabsf(mixbuf[i]);
    float scale = peak > 0.0f ? 0.9f / peak : 0.0f;
    int len = (mixend + 15) & ~15;
    if (len > MAXLEN) len = MAXLEN;
    memset(SFX[id], 0, sizeof SFX[id]);
    for (int i = 0; i < mixend && i < MAXLEN; i++) SFX[id][i] = (int16_t)(mixbuf[i] * scale * 32000.0f);
    sfx_len[id] = len;
}

void audio_init(void)
{
    if (ready) return;
    ready = 1;

    /* --- interface blips --- */
    begin(); note(0, 0.035f, 1250, 1250, 0.6f, W_SQ, 0.5f, 1.0f); finish(SFX_MOVE);
    begin(); note(0, 0.04f, 880, 1320, 0.6f, W_SQ, 0.25f, 1.0f); finish(SFX_SELECT);
    begin(); note(0, 0.05f, 659, 659, 0.5f, W_SQ, 0.5f, 1.0f); note(0.05f, 0.09f, 494, 494, 0.5f, W_SQ, 0.5f, 1.0f); finish(SFX_PAUSE);
    begin();                                                         /* error buzz */
    note(0, 0.11f, 150, 110, 0.7f, W_SAW, 0.5f, 0.6f);
    note(0.10f, 0.14f, 120, 80, 0.7f, W_SAW, 0.5f, 1.0f);
    finish(SFX_ERROR);

    /* --- building --- */
    begin();                                                         /* thud + two rising notes */
    note(0, 0.09f, 700, 200, 0.9f, W_NOISE, 0.5f, 1.5f);
    note(0, 0.10f, 160, 70, 0.9f, W_SIN, 0.5f, 1.5f);
    note(0.07f, 0.07f, 523, 523, 0.4f, W_SQ, 0.25f, 1.0f);
    note(0.13f, 0.14f, 784, 784, 0.4f, W_SQ, 0.25f, 1.5f);
    finish(SFX_BUILD);
    begin();                                                         /* upgrade arpeggio */
    note(0.00f, 0.06f, 523, 523, 0.5f, W_SQ, 0.25f, 0.8f);
    note(0.06f, 0.06f, 659, 659, 0.5f, W_SQ, 0.25f, 0.8f);
    note(0.12f, 0.06f, 784, 784, 0.5f, W_SQ, 0.25f, 0.8f);
    note(0.18f, 0.22f, 1047, 1047, 0.5f, W_SQ, 0.25f, 1.6f);
    note(0.18f, 0.22f, 523, 523, 0.3f, W_TRI, 0.5f, 1.6f);
    finish(SFX_UPGRADE);
    begin();                                                         /* coin */
    note(0.00f, 0.05f, 988, 988, 0.5f, W_SQ, 0.5f, 0.5f);
    note(0.05f, 0.26f, 1319, 1319, 0.5f, W_SQ, 0.5f, 1.8f);
    finish(SFX_SELL);

    /* --- combat --- */
    begin();                                                         /* arrow: twang + whoosh */
    note(0, 0.07f, 1900, 650, 0.45f, W_SQ, 0.125f, 2.0f);
    note(0, 0.06f, 5500, 2200, 0.35f, W_NOISE, 0.5f, 1.5f);
    finish(SFX_ARROW);
    begin();                                                         /* cannon: boom */
    note(0, 0.28f, 170, 38, 0.9f, W_SIN, 0.5f, 1.4f);
    note(0, 0.22f, 2200, 180, 0.8f, W_NOISE, 0.5f, 1.4f);
    finish(SFX_CANNON);
    begin();                                                         /* frost: glassy chime */
    note(0.00f, 0.06f, 1568, 1568, 0.35f, W_SIN, 0.5f, 1.2f);
    note(0.04f, 0.06f, 2093, 2093, 0.35f, W_SIN, 0.5f, 1.2f);
    note(0.08f, 0.14f, 2637, 2637, 0.35f, W_SIN, 0.5f, 1.8f);
    note(0.00f, 0.10f, 6000, 3000, 0.12f, W_NOISE, 0.5f, 1.0f);
    finish(SFX_FROST);
    begin();                                                         /* explosion */
    note(0, 0.45f, 3200, 140, 1.0f, W_NOISE, 0.5f, 1.6f);
    note(0, 0.38f, 95, 28, 0.9f, W_SIN, 0.5f, 1.3f);
    finish(SFX_EXPLODE);
    begin();                                                         /* enemy popped */
    note(0, 0.075f, 720, 190, 0.5f, W_SQ, 0.5f, 1.0f);
    note(0, 0.05f, 4000, 1500, 0.3f, W_NOISE, 0.5f, 1.2f);
    finish(SFX_KILL);
    begin();                                                         /* the keep is hit */
    note(0.00f, 0.32f, 230, 60, 0.7f, W_SAW, 0.5f, 1.0f);
    note(0.04f, 0.32f, 240, 64, 0.6f, W_SQ, 0.5f, 1.0f);
    note(0.00f, 0.20f, 900, 120, 0.6f, W_NOISE, 0.5f, 1.4f);
    finish(SFX_LEAK);

    /* --- waves / game flow --- */
    begin();                                                         /* wave horn */
    note(0.00f, 0.15f, 392, 392, 0.5f, W_SQ, 0.25f, 0.4f);
    note(0.15f, 0.32f, 523, 523, 0.5f, W_SQ, 0.25f, 1.2f);
    note(0.00f, 0.47f, 196, 262, 0.35f, W_TRI, 0.5f, 0.6f);
    finish(SFX_WAVE);
    begin();                                                         /* boss: dissonant low drone */
    note(0.00f, 0.42f, 110, 108, 0.6f, W_SAW, 0.5f, 0.3f);
    note(0.00f, 0.42f, 116, 114, 0.5f, W_SAW, 0.5f, 0.3f);
    note(0.42f, 0.50f, 87, 82, 0.6f, W_SAW, 0.5f, 0.8f);
    note(0.42f, 0.50f, 92, 87, 0.5f, W_SAW, 0.5f, 0.8f);
    note(0.00f, 0.90f, 220, 120, 0.25f, W_NOISE, 0.5f, 0.8f);
    finish(SFX_BOSS);
    begin();                                                         /* wave clear fanfare */
    note(0.00f, 0.10f, 523, 523, 0.5f, W_SQ, 0.5f, 0.5f);
    note(0.10f, 0.10f, 659, 659, 0.5f, W_SQ, 0.5f, 0.5f);
    note(0.20f, 0.10f, 784, 784, 0.5f, W_SQ, 0.5f, 0.5f);
    note(0.30f, 0.40f, 1047, 1047, 0.5f, W_SQ, 0.5f, 1.4f);
    note(0.30f, 0.40f, 523, 523, 0.3f, W_TRI, 0.5f, 1.4f);
    finish(SFX_CLEAR);
    begin();                                                         /* new game */
    note(0.00f, 0.08f, 392, 392, 0.5f, W_SQ, 0.25f, 0.5f);
    note(0.08f, 0.08f, 523, 523, 0.5f, W_SQ, 0.25f, 0.5f);
    note(0.16f, 0.08f, 659, 659, 0.5f, W_SQ, 0.25f, 0.5f);
    note(0.24f, 0.30f, 784, 784, 0.5f, W_SQ, 0.25f, 1.4f);
    finish(SFX_START);
    begin();                                                         /* game over: sad descent */
    note(0.00f, 0.20f, 392, 392, 0.5f, W_TRI, 0.5f, 0.3f);
    note(0.20f, 0.20f, 349, 349, 0.5f, W_TRI, 0.5f, 0.3f);
    note(0.40f, 0.20f, 311, 311, 0.5f, W_TRI, 0.5f, 0.3f);
    note(0.60f, 0.45f, 262, 196, 0.6f, W_TRI, 0.5f, 1.0f);
    note(0.60f, 0.45f, 131, 98, 0.4f, W_SAW, 0.5f, 1.0f);
    finish(SFX_OVER);
    begin();                                                         /* victory */
    note(0.00f, 0.10f, 523, 523, 0.5f, W_SQ, 0.5f, 0.4f);
    note(0.12f, 0.10f, 523, 523, 0.5f, W_SQ, 0.5f, 0.4f);
    note(0.24f, 0.10f, 523, 523, 0.5f, W_SQ, 0.5f, 0.4f);
    note(0.36f, 0.22f, 659, 659, 0.5f, W_SQ, 0.5f, 0.6f);
    note(0.60f, 0.10f, 587, 587, 0.5f, W_SQ, 0.5f, 0.4f);
    note(0.72f, 0.36f, 784, 784, 0.5f, W_SQ, 0.5f, 1.4f);
    note(0.72f, 0.36f, 392, 392, 0.3f, W_TRI, 0.5f, 1.4f);
    finish(SFX_WIN);
    begin();                                                         /* thunder: crack, then a long low rumble */
    note(0.00f, 0.06f, 7000, 900, 0.9f, W_NOISE, 0.5f, 1.2f);
    note(0.00f, 1.05f, 700, 90, 0.8f, W_NOISE, 0.5f, 1.1f);
    note(0.05f, 1.00f, 70, 34, 0.9f, W_SIN, 0.5f, 1.0f);
    note(0.30f, 0.75f, 420, 70, 0.45f, W_NOISE, 0.5f, 1.4f);
    finish(SFX_THUNDER);
}

const int16_t *audio_data(int id) { return SFX[id]; }
int            audio_len(int id)  { return sfx_len[id]; }
int            audio_volume(int id) { return VOL[id]; }
