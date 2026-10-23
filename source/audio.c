/*
 * TOWER DEFENSE - sound effect synthesis. See audio.h.
 *
 * Every effect is built at start-up from a small synth, so no audio files are needed. It aims for a clean,
 * modern sound rather than chiptune: 44.1 kHz, band-limited oscillators, FM bells and plucks with smooth
 * envelopes, resonant filtered noise for thumps and whooshes, and a little room reverb on top.
 */
#include "audio.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAXLEN 88192                       /* 2.0 s per effect at 44.1 kHz, a multiple of 16 so rows stay 32-byte aligned */

static int16_t SFX[SFX_COUNT][MAXLEN] __attribute__((aligned(32)));
static int     sfx_len[SFX_COUNT];
static float   mixbuf[MAXLEN];
static float   outbuf[MAXLEN];
static int     mixend;
static uint32_t rng = 0x2545F491u;
static int     ready;

/* playback volume per effect (every effect is normalised first, then scaled by this to balance loudness) */
static const uint8_t VOL[SFX_COUNT] = {
    /* MOVE */ 70,  /* SELECT */ 100, /* BUILD */ 200, /* UPGRADE */ 170, /* SELL */ 150, /* ERROR */ 150,
    /* ARROW */ 100, /* CANNON */ 235, /* FROST */ 120, /* EXPLODE */ 235, /* KILL */ 110, /* LEAK */ 220,
    /* WAVE */ 190, /* BOSS */ 240, /* CLEAR */ 200, /* START */ 190, /* PAUSE */ 120, /* OVER */ 220,
    /* WIN */ 220
};

/* ------------------------------------------------------------------ */
/* Building blocks                                                     */
/* ------------------------------------------------------------------ */
enum { W_SIN, W_TRI, W_SOFT, W_FM };           /* oscillator shapes */
enum { F_LP, F_BP, F_HP };                      /* noise filter types */

#define SR ((float)SFX_RATE)
#define TABN 4096
static float sine_tab[TABN + 1];

static inline float sine01(float ph)            /* ph in [0,1) */
{
    float x = ph * (float)TABN;
    int i = (int)x;
    float fr = x - (float)i;
    return sine_tab[i] + (sine_tab[i + 1] - sine_tab[i]) * fr;
}

static float frand(void)                        /* -1 .. 1 */
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(rng >> 8) / 8388608.0f - 1.0f;
}

/*
 * One pitched voice mixed into the effect being built.
 *   start, dur : seconds
 *   f0 -> f1   : exponential pitch glide in Hz
 *   atk        : attack time (s);  tau : decay time constant (s), 0 = sustained
 *   W_SIN      : pure sine
 *   W_TRI      : soft triangle (sine + a few odd harmonics)
 *   W_SOFT     : warm band-limited saw; a = extra high-harmonic roll-off (0 bright .. 2 dark)
 *   W_FM       : two-operator FM; a = modulator/carrier ratio, b = peak modulation index (fades with the note)
 */
static void tone(float start, float dur, float f0, float f1, float vol, float atk, float tau, int wave, float a, float b)
{
    int s0 = (int)(start * SR), n = (int)(dur * SR);
    float ph = 0.0f, pm = 0.0f;
    float glide = (f1 == f0) ? 1.0f : powf(f1 / f0, 1.0f / (float)(n > 1 ? n : 1));
    float f = f0, hw[10];
    for (int h = 1; h <= 9; h++) hw[h] = 1.0f / powf((float)h, 1.0f + a);
    float env = 1.0f, k = (tau > 0.0f) ? expf(-1.0f / (tau * SR)) : 1.0f;
    float atk_n = atk * SR > 1.0f ? atk * SR : 1.0f;
    float rel_n = 0.006f * SR;
    for (int i = 0; i < n && s0 + i < MAXLEN; i++) {
        float amp = env;
        if ((float)i < atk_n) amp *= (float)i / atk_n;
        if ((float)(n - i) < rel_n) amp *= (float)(n - i) / rel_n;
        float v;
        switch (wave) {
        case W_TRI:
            v = sine01(ph) + 0.111f * sine01(ph * 3.0f - floorf(ph * 3.0f)) * -1.0f + 0.04f * sine01(ph * 5.0f - floorf(ph * 5.0f));
            break;
        case W_SOFT: {
            v = 0.0f;
            int nh = (int)(0.42f * SR / (f > 1.0f ? f : 1.0f));
            if (nh > 9) nh = 9;
            for (int h = 1; h <= nh; h++) {
                float x = ph * (float)h;
                v += sine01(x - floorf(x)) * hw[h];
            }
            break;
        }
        case W_FM: {
            float idx = b * env;
            float m = sine01(pm);
            float x = ph + idx * m / (2.0f * (float)M_PI) * 2.0f;
            x -= floorf(x);
            v = sine01(x);
            break;
        }
        default:
            v = sine01(ph);
            break;
        }
        mixbuf[s0 + i] += v * amp * vol;
        if (s0 + i + 1 > mixend) mixend = s0 + i + 1;
        ph += f / SR; if (ph >= 1.0f) ph -= 1.0f;
        pm += f * a / SR; pm -= floorf(pm);
        f *= glide;
        env *= k;
    }
}

/* resonant filtered noise: fc0 -> fc1 is the filter cutoff sweep in Hz, q the resonance */
static void noise(float start, float dur, float fc0, float fc1, float q, float vol, float atk, float tau, int mode)
{
    int s0 = (int)(start * SR), n = (int)(dur * SR);
    float z1 = 0.0f, z2 = 0.0f;                  /* transposed direct form II state */
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float env = 1.0f, k = (tau > 0.0f) ? expf(-1.0f / (tau * SR)) : 1.0f;
    float atk_n = atk * SR > 1.0f ? atk * SR : 1.0f;
    float rel_n = 0.006f * SR;
    float ratio = (fc1 == fc0) ? 1.0f : powf(fc1 / fc0, 1.0f / (float)(n > 1 ? n : 1));
    float fc = fc0;
    for (int i = 0; i < n && s0 + i < MAXLEN; i++) {
        if ((i & 7) == 0) {                       /* retune the filter every 8 samples */
            float w = 2.0f * (float)M_PI * (fc < 0.45f * SR ? fc : 0.45f * SR) / SR;
            float cw = cosf(w), alpha = sinf(w) / (2.0f * q), a0 = 1.0f + alpha;
            switch (mode) {
            case F_LP: b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; break;
            case F_HP: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; break;
            default:   b0 = alpha; b1 = 0; b2 = -alpha; break;
            }
            a1 = -2.0f * cw / a0; a2 = (1.0f - alpha) / a0;
            b0 /= a0; b1 /= a0; b2 /= a0;
        }
        float x = frand();
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        float amp = env;
        if ((float)i < atk_n) amp *= (float)i / atk_n;
        if ((float)(n - i) < rel_n) amp *= (float)(n - i) / rel_n;
        mixbuf[s0 + i] += y * amp * vol;
        if (s0 + i + 1 > mixend) mixend = s0 + i + 1;
        env *= k;
        fc *= ratio;
    }
}

static void begin(void)
{
    memset(mixbuf, 0, sizeof mixbuf);
    mixend = 0;
}

/* mix down: a clean two-tap echo for a little space (no reverb noise), normalise, 16-bit. No clipping or saturation. */
static void finish_fx(int id, float wet, float room)
{
    int d1 = (int)((0.045f + 0.07f * room) * SR), d2 = d1 * 2 + (int)(0.013f * SR);
    int tail = (wet > 0.0f) ? d2 + (int)(0.05f * SR) : (int)(0.03f * SR);
    int len = mixend + tail;
    if (len > MAXLEN) len = MAXLEN;
    float peak = 0.0f;
    for (int i = 0; i < len; i++) {
        float x = (i < mixend) ? mixbuf[i] : 0.0f;
        if (i >= d1 && i - d1 < mixend) x += wet * 0.6f * mixbuf[i - d1];
        if (i >= d2 && i - d2 < mixend) x += wet * 0.3f * mixbuf[i - d2];
        outbuf[i] = x;
        if (fabsf(x) > peak) peak = fabsf(x);
    }
    int last = len;                                    /* trim the silent tail */
    while (last > 1 && fabsf(outbuf[last - 1]) < peak * 0.003f) last--;
    last += (int)(0.02f * SR);
    if (last < len) len = last;
    int fade = (int)(0.02f * SR);
    float scale = peak > 0.0f ? 0.85f * 32000.0f / peak : 0.0f;
    int outlen = (len + 15) & ~15;
    if (outlen > MAXLEN) outlen = MAXLEN;
    memset(SFX[id], 0, sizeof SFX[id]);
    for (int i = 0; i < len && i < MAXLEN; i++) {
        float x = outbuf[i];
        if (len - i < fade) x *= (float)(len - i) / (float)fade;
        SFX[id][i] = (int16_t)(x * scale);
    }
    sfx_len[id] = outlen;
}

#define C4 261.63f
#define D4 293.66f
#define EB4 311.13f
#define F4 349.23f
#define G4 392.00f
#define A4 440.00f
#define C5 523.25f
#define D5 587.33f
#define E5 659.25f
#define G5 783.99f
#define A5 880.00f
#define B5 987.77f
#define C6 1046.50f
#define E6 1318.51f
#define G6 1567.98f
#define C7 2093.00f

/* a bell/ping: FM with a slightly inharmonic ratio */
static void bell(float t, float f, float vol, float tau)
{
    tone(t, tau * 5.0f, f, f, vol, 0.002f, tau, W_FM, 3.0f, 1.1f);
    tone(t, tau * 3.0f, f * 2.0f, f * 2.0f, vol * 0.25f, 0.002f, tau * 0.5f, W_SIN, 0, 0);
}

void audio_init(void)
{
    if (ready) return;
    ready = 1;
    for (int i = 0; i <= TABN; i++) sine_tab[i] = sinf((float)i * 2.0f * (float)M_PI / (float)TABN);

    /* Sci-fi / laser style: fast pitch sweeps, FM "zaps" and bright synth pings, with a short space echo.
       Everything is a clean tone (no noise layers), so nothing hisses. */

    /* --- interface --- */
    begin();                                                         /* cursor: a tiny blip */
    tone(0, 0.05f, 1800, 2300, 0.6f, 0.002f, 0.014f, W_SIN, 0, 0);
    finish_fx(SFX_MOVE, 0.0f, 0.0f);

    begin();                                                         /* select: bip-boop */
    tone(0.00f, 0.08f, 900, 1200, 0.5f, 0.003f, 0.03f, W_SIN, 0, 0);
    tone(0.07f, 0.16f, 1500, 1900, 0.5f, 0.003f, 0.05f, W_SIN, 0, 0);
    finish_fx(SFX_SELECT, 0.4f, 0.3f);

    begin();                                                         /* pause: a quick downward sweep */
    tone(0, 0.22f, 1500, 500, 0.5f, 0.004f, 0.09f, W_SIN, 0, 0);
    tone(0, 0.22f, 3000, 1000, 0.1f, 0.004f, 0.06f, W_SIN, 0, 0);
    finish_fx(SFX_PAUSE, 0.4f, 0.3f);

    begin();                                                         /* error: low "nope" */
    tone(0.00f, 0.16f, 220, 120, 0.7f, 0.003f, 0.07f, W_FM, 1.0f, 1.8f);
    tone(0.12f, 0.22f, 180, 80, 0.7f, 0.003f, 0.09f, W_FM, 1.0f, 1.8f);
    finish_fx(SFX_ERROR, 0.0f, 0.0f);

    /* --- building --- */
    begin();                                                         /* build: power-up sweep + ping */
    tone(0, 0.16f, 250, 1100, 0.55f, 0.004f, 0.07f, W_SIN, 0, 0);
    tone(0, 0.16f, 125, 550, 0.35f, 0.004f, 0.07f, W_SIN, 0, 0);
    bell(0.14f, 1568.0f, 0.4f, 0.10f);
    finish_fx(SFX_BUILD, 0.4f, 0.4f);

    begin();                                                         /* upgrade: rising laser arpeggio */
    for (int i = 0; i < 4; i++) {
        static const float nf[4] = {C5, E5, G5, C6};
        tone(0.07f * (float)i, 0.12f, nf[i] * 0.7f, nf[i] * 1.5f, 0.4f, 0.003f, 0.05f, W_FM, 1.0f, 1.2f);
    }
    bell(0.28f, C7, 0.3f, 0.2f);
    finish_fx(SFX_UPGRADE, 0.4f, 0.5f);

    begin();                                                         /* sell: coin zap */
    tone(0.00f, 0.09f, 900, 2200, 0.5f, 0.002f, 0.04f, W_SIN, 0, 0);
    tone(0.08f, 0.25f, 2600, 2600, 0.5f, 0.002f, 0.1f, W_FM, 1.0f, 1.4f);
    finish_fx(SFX_SELL, 0.4f, 0.3f);

    /* --- combat --- */
    begin();                                                         /* arrow: laser "pew" */
    tone(0, 0.14f, 3000, 380, 0.55f, 0.001f, 0.06f, W_FM, 1.0f, 2.5f);
    tone(0, 0.14f, 1500, 190, 0.25f, 0.001f, 0.05f, W_SIN, 0, 0);
    finish_fx(SFX_ARROW, 0.25f, 0.2f);

    begin();                                                         /* cannon: heavy plasma blast */
    tone(0, 0.5f, 320, 38, 1.0f, 0.001f, 0.16f, W_SIN, 0, 0);
    tone(0, 0.35f, 900, 70, 0.55f, 0.001f, 0.1f, W_FM, 1.0f, 4.0f);
    tone(0, 0.25f, 2600, 300, 0.25f, 0.001f, 0.05f, W_FM, 1.0f, 2.5f);
    finish_fx(SFX_CANNON, 0.35f, 0.5f);

    begin();                                                         /* frost: icy rising ray */
    tone(0.00f, 0.5f, 700, 3200, 0.3f, 0.01f, 0.25f, W_FM, 2.0f, 1.5f);
    tone(0.00f, 0.5f, 710, 3240, 0.3f, 0.01f, 0.25f, W_SIN, 0, 0);
    tone(0.10f, 0.7f, 2637, 2637, 0.2f, 0.004f, 0.2f, W_FM, 3.0f, 1.0f);
    finish_fx(SFX_FROST, 0.45f, 0.6f);

    begin();                                                         /* explosion: big metallic space boom */
    tone(0, 0.9f, 220, 24, 1.0f, 0.001f, 0.25f, W_SIN, 0, 0);
    tone(0, 0.6f, 1400, 50, 0.6f, 0.001f, 0.15f, W_FM, 1.0f, 5.0f);
    tone(0, 0.3f, 3200, 200, 0.3f, 0.001f, 0.06f, W_FM, 1.4f, 3.0f);
    noise(0, 0.7f, 300, 60, 0.6f, 0.5f, 0.002f, 0.2f, F_LP);
    finish_fx(SFX_EXPLODE, 0.35f, 0.6f);

    begin();                                                         /* enemy zapped: short descending zap + pop */
    tone(0, 0.12f, 1800, 220, 0.5f, 0.001f, 0.04f, W_FM, 1.0f, 3.0f);
    tone(0.03f, 0.1f, 400, 120, 0.5f, 0.001f, 0.03f, W_SIN, 0, 0);
    finish_fx(SFX_KILL, 0.0f, 0.0f);

    begin();                                                         /* the keep is hit: warning siren drop */
    tone(0.00f, 0.25f, 900, 450, 0.55f, 0.004f, 0.12f, W_FM, 1.0f, 2.0f);
    tone(0.20f, 0.35f, 700, 250, 0.55f, 0.004f, 0.15f, W_FM, 1.0f, 2.0f);
    tone(0, 0.5f, 70, 40, 0.6f, 0.003f, 0.2f, W_SIN, 0, 0);
    finish_fx(SFX_LEAK, 0.35f, 0.4f);

    /* --- waves / game flow --- */
    begin();                                                         /* wave start: rising sweep + synth chord */
    tone(0.00f, 0.3f, 250, 700, 0.35f, 0.02f, 0.2f, W_SIN, 0, 0);
    tone(0.28f, 0.6f, G4, G4, 0.35f, 0.01f, 0.3f, W_FM, 1.0f, 1.2f);
    tone(0.28f, 0.6f, D5, D5, 0.3f, 0.01f, 0.3f, W_FM, 1.0f, 1.2f);
    tone(0.28f, 0.6f, G5, G5, 0.25f, 0.01f, 0.3f, W_FM, 1.0f, 1.2f);
    finish_fx(SFX_WAVE, 0.4f, 0.5f);

    begin();                                                         /* boss: deep warping alarm */
    tone(0, 1.2f, 60, 45, 0.8f, 0.05f, 0.8f, W_FM, 1.0f, 3.0f);
    tone(0.0f, 0.5f, 200, 600, 0.35f, 0.02f, 0.3f, W_FM, 1.0f, 2.0f);
    tone(0.5f, 0.6f, 600, 150, 0.35f, 0.02f, 0.3f, W_FM, 1.0f, 2.0f);
    tone(0.0f, 1.2f, 30, 28, 0.6f, 0.05f, 0.9f, W_SIN, 0, 0);
    finish_fx(SFX_BOSS, 0.35f, 0.5f);

    begin();                                                         /* wave clear: bright synth arpeggio */
    for (int i = 0; i < 5; i++) {
        static const float nf[5] = {C5, E5, G5, C6, E6};
        tone(0.07f * (float)i, 0.2f, nf[i], nf[i], 0.35f, 0.003f, 0.1f, W_FM, 2.0f, 1.0f);
    }
    bell(0.35f, G6, 0.3f, 0.25f);
    finish_fx(SFX_CLEAR, 0.45f, 0.5f);

    begin();                                                         /* new game: warp-up */
    tone(0.00f, 0.35f, 150, 1500, 0.4f, 0.01f, 0.2f, W_FM, 1.0f, 1.5f);
    tone(0.30f, 0.5f, C5, C5, 0.3f, 0.01f, 0.25f, W_FM, 2.0f, 1.0f);
    tone(0.30f, 0.5f, G5, G5, 0.25f, 0.01f, 0.25f, W_FM, 2.0f, 1.0f);
    tone(0.30f, 0.5f, C6, C6, 0.2f, 0.01f, 0.25f, W_FM, 2.0f, 1.0f);
    finish_fx(SFX_START, 0.4f, 0.5f);

    begin();                                                         /* game over: long power-down */
    tone(0, 1.3f, 700, 35, 0.6f, 0.01f, 0.7f, W_FM, 1.0f, 2.5f);
    tone(0, 1.3f, 350, 30, 0.5f, 0.01f, 0.7f, W_SIN, 0, 0);
    finish_fx(SFX_OVER, 0.4f, 0.5f);

    begin();                                                         /* victory arpeggio (unused in endless mode) */
    for (int i = 0; i < 6; i++) {
        static const float nf[6] = {C5, E5, G5, C6, E6, G6};
        tone(0.08f * (float)i, 0.25f, nf[i], nf[i], 0.35f, 0.003f, 0.12f, W_FM, 2.0f, 1.0f);
    }
    finish_fx(SFX_WIN, 0.45f, 0.5f);
}

const int16_t *audio_data(int id) { return SFX[id]; }
int            audio_len(int id)  { return sfx_len[id]; }
int            audio_volume(int id) { return VOL[id]; }
