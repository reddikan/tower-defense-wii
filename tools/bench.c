/*
 * Worst-case frame-cost benchmark for the 60 fps target.
 *
 * Fills the whole map with level-3 towers, then plays wave 20 (3 bosses, ~50 enemies on screen,
 * hundreds of particles, rings, projectiles) and times, per frame:
 *    update  = game_update (simulation)
 *    render  = game_render (640x360 palette framebuffer)
 *    present = palette -> YUY2 640x480 conversion + copy (the same work main.c does on the Wii)
 */
#define _POSIX_C_SOURCE 199309L
#include "../source/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static uint8_t pal_y[256], pal_cb[256], pal_cr[256];
static uint32_t xfb[320 * 480];          /* 640x480 YUY2 */
static uint32_t linebuf[512];

static void rgb_to_ycc(int r, int g, int b, uint8_t *oy, uint8_t *ocb, uint8_t *ocr)
{
    int y  = (299 * r + 587 * g + 114 * b) / 1000;
    int cb = (-16874 * r - 33126 * g + 50000 * b + 12800000) / 100000;
    int cr = (50000 * r - 41869 * g - 8131 * b + 12800000) / 100000;
    *oy  = (uint8_t)(y  > 255 ? 255 : (y  < 0 ? 0 : y));
    *ocb = (uint8_t)(cb > 255 ? 255 : (cb < 0 ? 0 : cb));
    *ocr = (uint8_t)(cr > 255 ? 255 : (cr < 0 ? 0 : cr));
}

/* identical in structure to present() in main.c (16:9 anamorphic path) */
static void present(const uint8_t *src)
{
    int W = 640, H = 480, words = W / 2, last = -1;
    for (int y = 0; y < H; y++) {
        uint32_t *row = xfb + (size_t)y * words;
        int sr = y * SCREEN_H / H;
        if (sr != last) {
            const uint8_t *s = src + sr * SCREEN_W;
            for (int i = 0; i < words; i++) {
                int a = s[(2 * i) * SCREEN_W / W], b2 = s[(2 * i + 1) * SCREEN_W / W];
                linebuf[i] = ((uint32_t)pal_y[a] << 24) | ((uint32_t)((pal_cb[a] + pal_cb[b2]) >> 1) << 16) | ((uint32_t)pal_y[b2] << 8) | (uint32_t)((pal_cr[a] + pal_cr[b2]) >> 1);
            }
            last = sr;
        }
        memcpy(row, linebuf, (size_t)words * 4);
    }
}

static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

static void report(const char *name, double *v, int n)
{
    double sum = 0;
    for (int i = 0; i < n; i++) sum += v[i];
    qsort(v, n, sizeof *v, cmp);
    printf("  %-8s avg %.3f ms   99th pct %.3f ms   worst %.3f ms\n", name, sum / n, v[(int)(n * 0.99)], v[n - 1]);
}

int main(void)
{
    static Game g;
    static uint8_t fb[SCREEN_W * SCREEN_H];
    for (int i = 0; i < PB; i++) rgb_to_ycc(game_palette[i][0], game_palette[i][1], game_palette[i][2], &pal_y[i], &pal_cb[i], &pal_cr[i]);

    static const char *names[4] = {
        "A) whole map full of level-3 towers (143), wave 20",
        "B) crowded: only 18 level-1 towers, so ~50 enemies pile up on screen, wave 20",
        "C) same as B but in fast-forward (2 simulation steps per frame)",
        "D) storm level 4 (rain, lightning, glow) with 143 level-3 towers, wave 18"
    };
    for (int pass = 0; pass < 4; pass++) {
        game_init(&g);
        game_start_level(&g, pass == 3 ? 3 : 0);
        g.gold = 1000000;
        int n = 0, want = (pass == 0 || pass == 3) ? 1000 : 18;
        for (int y = 0; y < MAP_H && n < want; y++) for (int x = 0; x < MAP_W && n < want; x++)
            if (game_is_buildable(&g, x, y)) {
                game_build(&g, x, y, n % 3); n++;
                if (pass == 0 || pass == 3) { game_upgrade(&g, x, y); game_upgrade(&g, x, y); }
            }
        g.lives = 1000000; g.wave = pass == 3 ? 17 : 19; g.wave_timer = 0.1f; g.fast = (pass == 2);
        printf("%s\n", names[pass]);

        enum { WARM = 120, N = 60 * 40 };
        static double tu[N], tr[N], tp[N], tt[N];
        int frames = 0, peak_enemies = 0, peak_parts = 0;
        for (int f = 0; f < N + WARM; f++) {
            double t0 = now_ms();
            game_update(&g, 0, 0, 1.0f / 60.0f);
            double t1 = now_ms();
            game_render(&g, fb);
            double t2 = now_ms();
            present(fb);
            double t3 = now_ms();
            while (game_pop_sound(&g) >= 0) {}
            if (f < WARM) continue;                       /* skip cold-cache warm-up frames */
            tu[frames] = t1 - t0; tr[frames] = t2 - t1; tp[frames] = t3 - t2; tt[frames] = t3 - t0; frames++;
            int e = 0, p = 0;
            for (int i = 0; i < MAX_ENEMIES; i++) e += g.en[i].alive;
            for (int i = 0; i < MAX_PART; i++) p += g.pt[i].life > 0;
            if (e > peak_enemies) peak_enemies = e;
            if (p > peak_parts) peak_parts = p;
        }
        printf("  (%d frames, peak %d enemies and %d particles on screen at once)\n", frames, peak_enemies, peak_parts);
        report("update", tu, frames);
        report("render", tr, frames);
        report("present", tp, frames);
        report("TOTAL", tt, frames);
        printf("  worst frame uses %.1f%% of the 16.67 ms budget on this PC\n\n", tt[frames - 1] / 16.67 * 100.0);
    }
    return 0;
}
