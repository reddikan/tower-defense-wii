/*
 * TOWER DEFENSE - Nintendo Wii platform layer (devkitPPC + libogc).
 *
 *   video : the 640x360 palette framebuffer is converted to the Wii's YUY2
 *           external framebuffer, 16:9 (anamorphic) or letterboxed in 4:3
 *   input : Wii Classic Controller (extension plugged into Wii Remote 1)
 *   audio : the synthesized effects are played through libogc's ASND
 *
 * Everything game-related lives in game.c / audio.c, which are plain C99.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gccore.h>
#include <ogc/conf.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>
#include <asndlib.h>

#include "game.h"

/* ---- Classic Controller bits (libogc ORs expansion buttons in at <<16) ---- */
#ifndef WPAD_CLASSIC_BUTTON_UP
#define WPAD_CLASSIC_BUTTON_UP     (0x0001u << 16)
#define WPAD_CLASSIC_BUTTON_LEFT   (0x0002u << 16)
#define WPAD_CLASSIC_BUTTON_ZR     (0x0004u << 16)
#define WPAD_CLASSIC_BUTTON_X      (0x0008u << 16)
#define WPAD_CLASSIC_BUTTON_A      (0x0010u << 16)
#define WPAD_CLASSIC_BUTTON_Y      (0x0020u << 16)
#define WPAD_CLASSIC_BUTTON_B      (0x0040u << 16)
#define WPAD_CLASSIC_BUTTON_ZL     (0x0080u << 16)
#define WPAD_CLASSIC_BUTTON_FULL_R (0x0200u << 16)
#define WPAD_CLASSIC_BUTTON_PLUS   (0x0400u << 16)
#define WPAD_CLASSIC_BUTTON_HOME   (0x0800u << 16)
#define WPAD_CLASSIC_BUTTON_MINUS  (0x1000u << 16)
#define WPAD_CLASSIC_BUTTON_FULL_L (0x2000u << 16)
#define WPAD_CLASSIC_BUTTON_DOWN   (0x4000u << 16)
#define WPAD_CLASSIC_BUTTON_RIGHT  (0x8000u << 16)
#endif

/* ASND sample format: signed 16-bit big-endian mono (libogc calls it VOICE_MONO_16BIT = 1).
 * The old fallback value 2 is VOICE_STEREO_8BIT, which played every sample as two 8-bit halves
 * and made the effects gritty, with hiss in the right speaker. */
#ifndef VOICE_MONO_16BIT
#define VOICE_MONO_16BIT 1
#endif

/* extra bits the platform layer handles itself (above the game's BTN_* bits) */
#define X_HOME  (1u << 16)
#define X_MINUS (1u << 17)

static GXRModeObj *rmode;
static void       *xfb[2];
static int         fbi;
static int         wide;            /* 1 = 16:9 anamorphic, 0 = letterboxed 16:9 inside a 4:3 picture */
static volatile int quit;

static Game    game;
static uint8_t fb[SCREEN_W * SCREEN_H];
static uint8_t pal_y[256], pal_cb[256], pal_cr[256];   /* PAL_N used; the rest stay black */
static u32     black_yuy2;
static u32     linebuf[512];

/* ------------------------------------------------------------------ */
/* video                                                               */
/* ------------------------------------------------------------------ */
static void rgb_to_ycc(int r, int g, int b, uint8_t *oy, uint8_t *ocb, uint8_t *ocr)
{
    int y  = (299 * r + 587 * g + 114 * b) / 1000;
    int cb = (-16874 * r - 33126 * g + 50000 * b + 12800000) / 100000;
    int cr = (50000 * r - 41869 * g - 8131 * b + 12800000) / 100000;
    *oy  = (uint8_t)(y  > 255 ? 255 : (y  < 0 ? 0 : y));
    *ocb = (uint8_t)(cb > 255 ? 255 : (cb < 0 ? 0 : cb));
    *ocr = (uint8_t)(cr > 255 ? 255 : (cr < 0 ? 0 : cr));
}

/* levels have their own colours and lightning flashes brighten them, so the palette is refreshed every frame */
static void update_palette(const Game *g)
{
    uint8_t p[PAL_N][3];
    game_get_palette(g, p);
    for (int i = 0; i < PAL_N; i++) rgb_to_ycc(p[i][0], p[i][1], p[i][2], &pal_y[i], &pal_cb[i], &pal_cr[i]);
}

static void apply_wide(int w)
{
    wide = w;
    int vw = w ? 678 : 640;
    rmode->viWidth   = vw;
    rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - vw) / 2;
    VIDEO_Configure(rmode);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

/* Convert the 640x360 palette image into the YUY2 external framebuffer (crisp pixels). */
static void present(const uint8_t *src)
{
    u32 *dst   = (u32 *)xfb[fbi];
    int  W     = rmode->fbWidth;
    int  H     = rmode->xfbHeight;
    int  words = W / 2;                       /* 2 pixels per 32-bit word */
    int  top = 0, bh = H;
    if (!wide) { bh = W * 9 / 16; top = (H - bh) / 2; }   /* 16:9 picture inside a 4:3 frame */

    int last = -1;
    for (int y = 0; y < H; y++) {
        u32 *row = dst + (size_t)y * words;
        if (y < top || y >= top + bh) {
            for (int i = 0; i < words; i++) row[i] = black_yuy2;
            continue;
        }
        int sr = (y - top) * SCREEN_H / bh;
        if (sr != last) {
            const uint8_t *s = src + sr * SCREEN_W;
            for (int i = 0; i < words; i++) {            /* two different pixels per word; they share their chroma */
                int a = s[(2 * i) * SCREEN_W / W], b2 = s[(2 * i + 1) * SCREEN_W / W];
                linebuf[i] = ((u32)pal_y[a] << 24) | ((u32)((pal_cb[a] + pal_cb[b2]) >> 1) << 16) | ((u32)pal_y[b2] << 8) | (u32)((pal_cr[a] + pal_cr[b2]) >> 1);
            }
            last = sr;
        }
        memcpy(row, linebuf, (size_t)words * 4);
    }
    DCFlushRange(dst, (u32)W * (u32)H * VI_DISPLAY_PIX_SZ);
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */
static u32 read_buttons(void)
{
    WPAD_ScanPads();
    u32 h = WPAD_ButtonsHeld(0), b = 0;
    if (h & WPAD_CLASSIC_BUTTON_UP)    b |= BTN_UP;
    if (h & WPAD_CLASSIC_BUTTON_DOWN)  b |= BTN_DOWN;
    if (h & WPAD_CLASSIC_BUTTON_LEFT)  b |= BTN_LEFT;
    if (h & WPAD_CLASSIC_BUTTON_RIGHT) b |= BTN_RIGHT;
    if (h & WPAD_CLASSIC_BUTTON_A)     b |= BTN_A;
    if (h & WPAD_CLASSIC_BUTTON_B)     b |= BTN_B;
    if (h & WPAD_CLASSIC_BUTTON_X)     b |= BTN_X;
    if (h & WPAD_CLASSIC_BUTTON_Y)     b |= BTN_Y;
    if (h & WPAD_CLASSIC_BUTTON_ZL)    b |= BTN_L;       /* ZL / ZR: change tower type */
    if (h & WPAD_CLASSIC_BUTTON_ZR)    b |= BTN_R;
    if (h & WPAD_CLASSIC_BUTTON_FULL_R) b |= BTN_ZIN;   /* right back trigger: zoom in  */
    if (h & WPAD_CLASSIC_BUTTON_FULL_L) b |= BTN_ZOUT;  /* left back trigger: zoom out */
    if (h & WPAD_CLASSIC_BUTTON_PLUS)  b |= BTN_PLUS;
    if (h & WPAD_CLASSIC_BUTTON_MINUS) b |= X_MINUS;
    if ((h & WPAD_CLASSIC_BUTTON_HOME) || (h & WPAD_BUTTON_HOME)) b |= X_HOME;
    return b;
}

/* ------------------------------------------------------------------ */
/* audio                                                               */
/* ------------------------------------------------------------------ */
static void play_sfx(int id)
{
    s32 v = ASND_GetFirstUnusedVoice();
    if (v < 0) return;                         /* all voices busy: skip this one */
    int vol = audio_volume(id);
    ASND_SetVoice(v, VOICE_MONO_16BIT, SFX_RATE, 0, (void *)audio_data(id), audio_len(id) * 2, vol, vol, NULL);
}

static void on_reset(u32 irq, void *ctx) { (void)irq; (void)ctx; quit = 1; }
static void on_power(void) { quit = 1; }

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    VIDEO_Init();
    WPAD_Init();
    CONF_Init();
    ASND_Init();
    ASND_Pause(0);
    SYS_SetResetCallback(on_reset);
    SYS_SetPowerCallback(on_power);

    game_init(&game);                          /* also synthesizes the sound effects */
    for (int id = 0; id < SFX_COUNT; id++)     /* ASND reads samples by DMA: flush the cache */
        DCFlushRange((void *)audio_data(id), (u32)audio_len(id) * 2);

    black_yuy2 = 0x00800080u;                          /* Y=0, Cb=Cr=128 */
    for (int i = 0; i < 256; i++) { pal_y[i] = 0; pal_cb[i] = 128; pal_cr[i] = 128; }
    update_palette(&game);

    rmode = VIDEO_GetPreferredMode(NULL);       /* 480p if the console/Dolphin has progressive scan enabled, else 480i */
    if ((rmode->viTVMode >> 2) == VI_PAL)       /* never run at 50 Hz: use PAL60 so the game is a true 60 fps */
        rmode = &TVEurgb60Hz480IntDf;
    xfb[0] = SYS_AllocateFramebuffer(rmode);
    xfb[1] = SYS_AllocateFramebuffer(rmode);

    /* Start in full 16:9 mode no matter what the console's aspect-ratio flag says: Dolphin's default flag is
     * 4:3 even when the window is widescreen, which used to need a press of MINUS. MINUS still toggles a
     * letterboxed picture for anyone playing on a real 4:3 TV. */
    wide = 1;
    rmode->viWidth   = wide ? 678 : 640;
    rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - rmode->viWidth) / 2;
    VIDEO_Configure(rmode);

    game_render(&game, fb);
    present(fb);
    VIDEO_SetNextFramebuffer(xfb[fbi]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    const float dt = 1.0f / 60.0f;              /* the game simulates in fixed 1/60 s steps */
    u32 prev = 0;
    u64 last = gettime();
    float avg_us = 8000.0f;                     /* smoothed time spent on a frame's work (not counting the wait for vsync) */

    while (!quit) {
        u64 frame_start = gettime();
        u32 held = read_buttons();
        u32 pressed = held & ~prev;
        prev = held;

        if (held & X_HOME) break;                                   /* back to the Homebrew Channel / menu */
        if (pressed & X_MINUS) apply_wide(!wide);                   /* fix a squashed / stretched picture */

        /* VIDEO_WaitVSync() below locks us to 60 fps. If a frame ever runs long (it shouldn't), run extra
         * simulation steps so the game keeps its speed instead of slowing down. */
        u64 now = gettime();
        float elapsed = (float)diff_usec(last, now) * 1e-6f;
        last = now;
        int steps = (int)(elapsed / dt + 0.5f);
        if (steps < 1) steps = 1;
        if (steps > 3) steps = 3;
        for (int s = 0; s < steps; s++)
            game_update(&game, held & 0xFFFFu, s == 0 ? (pressed & 0xFFFFu) : 0, dt);

        int id;
        while ((id = game_pop_sound(&game)) >= 0) play_sfx(id);

        game_render(&game, fb);
        update_palette(&game);
        fbi ^= 1;
        present(fb);
        /* safety net: if the soft lighting makes frames run long, drop to a lighter look until it speeds up again */
        avg_us = avg_us * 0.92f + (float)diff_usec(frame_start, gettime()) * 0.08f;
        if (avg_us > 14000.0f) game.lite = 1;
        else if (avg_us < 8500.0f) game.lite = 0;
        VIDEO_SetNextFramebuffer(xfb[fbi]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
    }

    ASND_End();
    return 0;
}
