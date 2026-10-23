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
    /* WIN */ 220, /* THUNDER */ 235
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

    /* Everything below is built from pure tones: sines, soft triangles and gentle FM. Drums and booms are
       pitch-dropping sines, not noise, so nothing hisses. Only the thunder uses a deep, low-passed rumble. */

    /* --- interface --- */
    begin();                                                         /* cursor: a tiny soft tick */
    tone(0, 0.05f, 1400, 1100, 0.6f, 0.002f, 0.012f, W_SIN, 0, 0);
    finish_fx(SFX_MOVE, 0.0f, 0.0f);

    begin();                                                         /* select: two clean rising notes */
    tone(0.00f, 0.15f, 880, 880, 0.5f, 0.004f, 0.045f, W_SIN, 0, 0);
    tone(0.06f, 0.20f, 1319, 1319, 0.5f, 0.004f, 0.06f, W_SIN, 0, 0);
    finish_fx(SFX_SELECT, 0.3f, 0.3f);

    begin();                                                         /* pause: a calm falling pair */
    tone(0.00f, 0.16f, 784, 784, 0.5f, 0.004f, 0.06f, W_SIN, 0, 0);
    tone(0.08f, 0.24f, 587, 587, 0.5f, 0.004f, 0.08f, W_SIN, 0, 0);
    finish_fx(SFX_PAUSE, 0.3f, 0.3f);

    begin();                                                         /* error: two low soft thuds */
    tone(0.00f, 0.14f, 150, 110, 0.8f, 0.004f, 0.05f, W_SIN, 0, 0);
    tone(0.11f, 0.20f, 125, 90, 0.8f, 0.004f, 0.07f, W_SIN, 0, 0);
    tone(0.00f, 0.14f, 300, 220, 0.2f, 0.004f, 0.04f, W_TRI, 0, 0);
    tone(0.11f, 0.20f, 250, 180, 0.2f, 0.004f, 0.05f, W_TRI, 0, 0);
    finish_fx(SFX_ERROR, 0.0f, 0.0f);

    /* --- building --- */
    begin();                                                         /* build: a firm thunk and a bright chime */
    tone(0, 0.16f, 160, 55, 0.9f, 0.002f, 0.05f, W_SIN, 0, 0);
    bell(0.07f, 784.0f, 0.35f, 0.10f);
    bell(0.13f, 1175.0f, 0.3f, 0.12f);
    finish_fx(SFX_BUILD, 0.3f, 0.4f);

    begin();                                                         /* upgrade: rising sparkle */
    bell(0.00f, C5, 0.4f, 0.12f);
    bell(0.07f, E5, 0.4f, 0.12f);
    bell(0.14f, G5, 0.4f, 0.12f);
    bell(0.21f, C6, 0.45f, 0.18f);
    tone(0.0f, 0.4f, C4, C4, 0.15f, 0.01f, 0.15f, W_SIN, 0, 0);
    finish_fx(SFX_UPGRADE, 0.3f, 0.4f);

    begin();                                                         /* sell: two bright coin pings */
    bell(0.00f, 1319.0f, 0.5f, 0.08f);
    bell(0.07f, 1976.0f, 0.5f, 0.14f);
    finish_fx(SFX_SELL, 0.3f, 0.3f);

    /* --- combat --- */
    begin();                                                         /* arrow: a quick airy pluck */
    tone(0, 0.09f, 1300, 520, 0.5f, 0.002f, 0.022f, W_SIN, 0, 0);
    tone(0, 0.07f, 2600, 1040, 0.12f, 0.002f, 0.015f, W_SIN, 0, 0);
    finish_fx(SFX_ARROW, 0.0f, 0.0f);

    begin();                                                         /* cannon: a deep boom */
    tone(0, 0.55f, 120, 36, 1.0f, 0.002f, 0.16f, W_SIN, 0, 0);
    tone(0, 0.18f, 260, 80, 0.5f, 0.002f, 0.05f, W_SIN, 0, 0);
    tone(0, 0.30f, 70, 40, 0.5f, 0.002f, 0.10f, W_TRI, 0, 0);
    finish_fx(SFX_CANNON, 0.25f, 0.4f);

    begin();                                                         /* frost: glassy shimmer */
    tone(0.00f, 0.8f, 1568, 1568, 0.3f, 0.004f, 0.18f, W_FM, 2.0f, 0.8f);
    tone(0.04f, 0.8f, 2093, 2093, 0.25f, 0.004f, 0.18f, W_FM, 2.0f, 0.7f);
    tone(0.08f, 0.9f, 2637, 2637, 0.22f, 0.004f, 0.2f, W_FM, 2.0f, 0.6f);
    finish_fx(SFX_FROST, 0.35f, 0.6f);

    begin();                                                         /* explosion: big falling thump + rumble */
    tone(0, 0.8f, 100, 28, 1.0f, 0.002f, 0.22f, W_SIN, 0, 0);
    tone(0, 0.3f, 220, 60, 0.6f, 0.002f, 0.07f, W_SIN, 0, 0);
    noise(0, 0.8f, 380, 70, 0.6f, 0.9f, 0.002f, 0.2f, F_LP);
    finish_fx(SFX_EXPLODE, 0.2f, 0.4f);

    begin();                                                         /* enemy popped: a round bloop */
    tone(0, 0.12f, 700, 300, 0.7f, 0.002f, 0.035f, W_SIN, 0, 0);
    tone(0, 0.08f, 1400, 600, 0.15f, 0.002f, 0.02f, W_SIN, 0, 0);
    finish_fx(SFX_KILL, 0.0f, 0.0f);

    begin();                                                         /* the keep is hit: a low sad "womp" */
    tone(0, 0.5f, 196, 98, 0.7f, 0.004f, 0.2f, W_TRI, 0, 0);
    tone(0, 0.5f, 98, 49, 0.6f, 0.004f, 0.2f, W_SIN, 0, 0);
    finish_fx(SFX_LEAK, 0.25f, 0.4f);

    /* --- waves / game flow --- */
    begin();                                                         /* wave horn: two warm notes */
    tone(0.00f, 0.7f, G4, G4, 0.5f, 0.03f, 0.4f, W_TRI, 0, 0);
    tone(0.00f, 0.7f, G4 * 2.0f, G4 * 2.0f, 0.12f, 0.03f, 0.3f, W_SIN, 0, 0);
    tone(0.22f, 0.6f, C5, C5, 0.5f, 0.03f, 0.35f, W_TRI, 0, 0);
    tone(0.22f, 0.6f, C5 * 2.0f, C5 * 2.0f, 0.12f, 0.03f, 0.25f, W_SIN, 0, 0);
    tone(0.00f, 0.8f, 196, 196, 0.25f, 0.04f, 0.4f, W_SIN, 0, 0);
    finish_fx(SFX_WAVE, 0.3f, 0.5f);

    begin();                                                         /* boss: a dark, rising rumble */
    tone(0, 1.2f, 55, 52, 0.7f, 0.08f, 0.7f, W_SIN, 0, 0);
    tone(0, 1.2f, 110, 104, 0.4f, 0.1f, 0.7f, W_TRI, 0, 0);
    tone(0, 1.2f, 116.5f, 110, 0.3f, 0.1f, 0.7f, W_TRI, 0, 0);
    tone(0.5f, 0.6f, 82, 55, 0.6f, 0.004f, 0.22f, W_SIN, 0, 0);
    finish_fx(SFX_BOSS, 0.25f, 0.5f);

    begin();                                                         /* wave clear: bright fanfare */
    bell(0.00f, C5, 0.4f, 0.12f);
    bell(0.10f, E5, 0.4f, 0.12f);
    bell(0.20f, G5, 0.4f, 0.12f);
    bell(0.30f, C6, 0.5f, 0.24f);
    tone(0.30f, 0.7f, C5, C5, 0.2f, 0.02f, 0.35f, W_TRI, 0, 0);
    finish_fx(SFX_CLEAR, 0.3f, 0.5f);

    begin();                                                         /* new game */
    bell(0.00f, G4, 0.4f, 0.10f);
    bell(0.08f, C5, 0.4f, 0.10f);
    bell(0.16f, E5, 0.4f, 0.10f);
    bell(0.24f, G5, 0.5f, 0.26f);
    tone(0.24f, 0.6f, C5, C5, 0.18f, 0.02f, 0.3f, W_TRI, 0, 0);
    finish_fx(SFX_START, 0.3f, 0.5f);

    begin();                                                         /* game over: a slow, sad descent */
    static const float over_f[4] = {G4, F4, EB4, C4};
    for (int i = 0; i < 4; i++) {
        float t = 0.24f * (float)i, f = over_f[i], ln = (i == 3) ? 0.9f : 0.34f, tau = i == 3 ? 0.5f : 0.2f;
        tone(t, ln, f, f * 0.985f, 0.5f, 0.012f, tau, W_TRI, 0, 0);
        tone(t, ln, f * 0.5f, f * 0.5f * 0.985f, 0.35f, 0.012f, tau, W_SIN, 0, 0);
    }
    finish_fx(SFX_OVER, 0.3f, 0.5f);

    begin();                                                         /* victory */
    bell(0.00f, C5, 0.4f, 0.10f);
    bell(0.11f, C5, 0.4f, 0.10f);
    bell(0.22f, C5, 0.4f, 0.10f);
    bell(0.33f, E5, 0.45f, 0.16f);
    bell(0.58f, D5, 0.4f, 0.10f);
    bell(0.70f, G5, 0.5f, 0.28f);
    bell(0.70f, C6, 0.3f, 0.28f);
    tone(0.70f, 0.9f, C5, C5, 0.18f, 0.03f, 0.45f, W_TRI, 0, 0);
    tone(0.70f, 0.9f, E5, E5, 0.14f, 0.03f, 0.45f, W_TRI, 0, 0);
    finish_fx(SFX_WIN, 0.3f, 0.5f);

    begin();                                                         /* thunder: deep rolling rumble, no hiss */
    noise(0, 1.8f, 240, 50, 0.7f, 1.0f, 0.04f, 0.75f, F_LP);
    noise(0.3f, 1.4f, 160, 45, 0.9f, 0.7f, 0.2f, 0.5f, F_LP);
    tone(0.02f, 1.6f, 48, 30, 0.9f, 0.05f, 0.8f, W_SIN, 0, 0);
    finish_fx(SFX_THUNDER, 0.2f, 0.6f);
}

const int16_t *audio_data(int id) { return SFX[id]; }
int            audio_len(int id)  { return sfx_len[id]; }
int            audio_volume(int id) { return VOL[id]; }
