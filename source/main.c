/*
 * TOWER DEFENSE - Nintendo Wii platform layer (devkitPPC + libogc).
 *
 *   video : the 320x180 palette framebuffer is converted to the Wii's YUY2
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

#ifndef VOICE_MONO16
#define VOICE_MONO16 2              /* signed 16-bit big-endian mono */
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
static u32     pal_yuy2[128];      /* PAL_N used; the rest stay black */
static u32     black_yuy2;
static u32     linebuf[512];

/* ------------------------------------------------------------------ */
/* video                                                               */
/* ------------------------------------------------------------------ */
static u32 rgb_to_yuy2(int r, int g, int b)
{
    int y  = (299 * r + 587 * g + 114 * b) / 1000;
    int cb = (-16874 * r - 33126 * g + 50000 * b + 12800000) / 100000;
    int cr = (50000 * r - 41869 * g - 8131 * b + 12800000) / 100000;
    y  = y  > 255 ? 255 : (y  < 0 ? 0 : y);
    cb = cb > 255 ? 255 : (cb < 0 ? 0 : cb);
    cr = cr > 255 ? 255 : (cr < 0 ? 0 : cr);
    return ((u32)y << 24) | ((u32)cb << 16) | ((u32)y << 8) | (u32)cr;   /* two identical pixels */
}

/* levels have their own colours and lightning flashes brighten them, so the palette is refreshed every frame */
static void update_palette(const Game *g)
{
    uint8_t p[PAL_N][3];
    game_get_palette(g, p);
    for (int i = 0; i < PAL_N; i++) pal_yuy2[i] = rgb_to_yuy2(p[i][0], p[i][1], p[i][2]);
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

/* Convert the 320x180 palette image into the YUY2 external framebuffer. */
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
            for (int i = 0; i < words; i++) linebuf[i] = pal_yuy2[s[i * SCREEN_W / words] & 127];
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
    if (h & (WPAD_CLASSIC_BUTTON_FULL_L | WPAD_CLASSIC_BUTTON_ZL)) b |= BTN_L;
    if (h & (WPAD_CLASSIC_BUTTON_FULL_R | WPAD_CLASSIC_BUTTON_ZR)) b |= BTN_R;
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
    ASND_SetVoice(v, VOICE_MONO16, SFX_RATE, 0, (void *)audio_data(id), audio_len(id) * 2, vol, vol, NULL);
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

    black_yuy2 = rgb_to_yuy2(0, 0, 0);
    for (int i = 0; i < 128; i++) pal_yuy2[i] = black_yuy2;
    update_palette(&game);

    rmode = VIDEO_GetPreferredMode(NULL);       /* 480p if the console/Dolphin has progressive scan enabled, else 480i */
    if ((rmode->viTVMode >> 2) == VI_PAL)       /* never run at 50 Hz: use PAL60 so the game is a true 60 fps */
        rmode = &TVEurgb60Hz480IntDf;
    xfb[0] = SYS_AllocateFramebuffer(rmode);
    xfb[1] = SYS_AllocateFramebuffer(rmode);

    /* follow the console's aspect-ratio setting (in Dolphin: Config > Wii > Aspect Ratio) */
    wide = (CONF_GetAspectRatio() == CONF_ASPECT_16_9);
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

    while (!quit) {
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
        VIDEO_SetNextFramebuffer(xfb[fbi]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
    }

    ASND_End();
    return 0;
}
