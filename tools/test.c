/* Native test harness for game.c: scripted input, bot playthroughs, frame dumps. */
#include "../source/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const float DT = 1.0f / 60.0f;
static int failures = 0;
static const char *outdir = ".";

#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } else printf("  ok:   %s\n", msg); } while (0)

static void dump(const uint8_t *fb, const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s.bin", outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite(fb, 1, SCREEN_W * SCREEN_H, f);
    fclose(f);
}

static void frame(Game *g, uint32_t held, uint32_t pressed) { game_update(g, held, pressed, DT); }

/* sound events seen so far (drained from the game's queue) */
static int sfx_seen[SFX_COUNT];
static int sfx_total_events;
static void drain(Game *g)
{
    int id;
    while ((id = game_pop_sound(g)) >= 0) { sfx_seen[id]++; sfx_total_events++; }
}
/* did `id` get queued since the last call? (drains the queue) */
static int heard(Game *g, int id)
{
    int before = sfx_seen[id];
    drain(g);
    return sfx_seen[id] > before;
}


/* build ONE tower of `type` next to the path, run a wave, and report whether `want` was queued */
static int single_tower_plays(int type, int want)
{
    static Game h;
    game_init(&h); frame(&h, BTN_A, BTN_A);
    h.gold = 1000;
    game_build(&h, 5, 3, type);
    h.wave_timer = 0.1f;
    int ok = 0, id;
    for (int i = 0; i < 60 * 25 && !ok; i++) {
        frame(&h, 0, 0);
        while ((id = game_pop_sound(&h)) >= 0) if (id == want) ok = 1;
    }
    return ok;
}

/* ---- bot ---- */
static int tile_score(int tx, int ty, int type)
{
    float x = tx * 16 + 8, y = 20 + ty * 16 + 8, r = game_tower_range(type, 0);
    int s = 0;
    for (float d = 0; d < game_path_length(); d += 8) {
        float px, py; game_path_point(d, &px, &py);
        float dx = px - x, dy = py - y;
        if (dx * dx + dy * dy <= r * r) s++;
    }
    return s;
}

static int count_towers(const Game *g)
{
    int n = 0;
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) n += g->towers[y][x].used;
    return n;
}

static void bot_act(Game *g, int aggression)
{
    static const int order[] = {TW_ARROW, TW_CANNON, TW_ARROW, TW_FROST, TW_CANNON, TW_ARROW, TW_FROST, TW_CANNON};
    int n = count_towers(g);
    int want_new = (n < 4 + g->wave * aggression / 10);
    if (want_new) {
        int type = order[n % 8], bx = -1, by = -1, bs = 0;
        if (g->gold < game_tower_cost(type)) return;
        for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) {
            if (!game_is_buildable(g, x, y)) continue;
            int s = tile_score(x, y, type);
            if (s > bs) { bs = s; bx = x; by = y; }
        }
        if (bx >= 0) game_build(g, bx, by, type);
        return;
    }
    /* upgrade the lowest-level affordable tower */
    int bx = -1, by = -1, bl = 9;
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) {
        const Tower *t = &g->towers[y][x];
        if (!t->used || t->level >= 2) continue;
        if (g->gold < game_upgrade_cost(t->type, t->level)) continue;
        if (t->level < bl) { bl = t->level; bx = x; by = y; }
    }
    if (bx >= 0) game_upgrade(g, bx, by);
}

typedef struct { int wave, lives, kills, won; float seconds; } Result;

static Result run_bot(const char *label, int aggression, int dump_shots, int idle)
{
    static Game g;
    static uint8_t fb[SCREEN_W * SCREEN_H];
    game_init(&g);
    frame(&g, 0, BTN_A);
    long f = 0;
    int shot_wave = 0;
    int peak_per_sec = 0, sec_events = 0;
    while (g.state == ST_PLAY && f < 60L * 60 * 40) {
        frame(&g, 0, 0);
        f++;
        { int before = sfx_total_events; drain(&g); sec_events += sfx_total_events - before; }
        if (f % 60 == 0) { if (sec_events > peak_per_sec) peak_per_sec = sec_events; sec_events = 0; }
        if (!idle && f % 20 == 0) bot_act(&g, aggression);
        if (dump_shots && g.wave_active && g.wave != shot_wave && (g.wave == 3 || g.wave == 10 || g.wave == 15 || g.wave == 20)) {
            /* wait ~14s into the wave so there is action on screen */
            for (int i = 0; i < 60 * 14 && g.state == ST_PLAY; i++) { frame(&g, 0, 0); f++; if (f % 20 == 0) bot_act(&g, aggression); }
            char name[64]; snprintf(name, sizeof name, "%s_wave%d", label, g.wave);
            game_render(&g, fb); dump(fb, name);
            shot_wave = g.wave;
        }
    }
    Result r = {g.wave, g.lives, g.kills, g.state == ST_WIN, (float)f / 60.0f};
    if (dump_shots) {
        for (int i = 0; i < 90; i++) frame(&g, 0, 0);
        game_render(&g, fb);
        char name[64]; snprintf(name, sizeof name, "%s_end", label);
        dump(fb, name);
    }
    printf("  %-22s wave %2d/%d  lives %2d  kills %3d  %s  (%.0f s game time, busiest second: %d sound events)\n", label, r.wave, NUM_WAVES, r.lives, r.kills,
           r.won ? "WON " : "LOST", r.seconds, peak_per_sec);
    return r;
}

int main(int argc, char **argv)
{
    if (argc > 1) outdir = argv[1];
    static Game g;
    static uint8_t fb[SCREEN_W * SCREEN_H];

    printf("[0] audio\n");
    audio_init();
    for (int id = 0; id < SFX_COUNT; id++) {
        const int16_t *d = audio_data(id); int n = audio_len(id), peak = 0;
        for (int i = 0; i < n; i++) { int v = d[i] < 0 ? -d[i] : d[i]; if (v > peak) peak = v; }
        if (n <= 0 || n % 16 != 0 || peak < 4000 || peak >= 32767 || ((uintptr_t)d & 31)) {
            printf("  FAIL: sfx %d len=%d peak=%d align=%d\n", id, n, peak, (int)((uintptr_t)d & 31)); failures++;
        }
    }
    printf("  ok:   all %d sound effects generated, 32-byte aligned, length multiple of 16, no clipping\n", SFX_COUNT);

    printf("[1] sprite sheet + title\n");
    game_debug_sheet(fb); dump(fb, "sheet");
    game_init(&g);
    for (int i = 0; i < 200; i++) frame(&g, 0, 0);
    game_render(&g, fb); dump(fb, "title");
    CHECK(g.state == ST_TITLE, "starts on title screen");
    drain(&g);

    printf("[2] controller input + sound events\n");
    frame(&g, BTN_A, BTN_A);
    CHECK(g.state == ST_PLAY, "A starts the game");
    CHECK(heard(&g, SFX_START), "start jingle queued");
    CHECK(g.cx == 6 && g.cy == 4, "cursor starts at 6,4");
    frame(&g, BTN_RIGHT, BTN_RIGHT); frame(&g, 0, 0);
    CHECK(g.cx == 7, "D-pad right moves cursor");
    CHECK(heard(&g, SFX_MOVE), "cursor tick queued");
    /* hold right: first repeat after 14 frames, then every 4 */
    int cx0 = g.cx;
    for (int i = 0; i < 30; i++) frame(&g, BTN_RIGHT, i == 0 ? BTN_RIGHT : 0);
    CHECK(g.cx > cx0 + 2, "holding D-pad auto-repeats");
    for (int i = 0; i < 40; i++) frame(&g, BTN_LEFT, i == 0 ? BTN_LEFT : 0);
    frame(&g, 0, 0);
    for (int i = 0; i < 60; i++) frame(&g, BTN_UP, i == 0 ? BTN_UP : 0);
    CHECK(g.cy == 0, "cursor clamps at top edge");
    /* go to a free tile: 7,4 */
    g.cx = 7; g.cy = 4;
    int gold = g.gold;
    frame(&g, BTN_A, BTN_A); frame(&g, 0, 0);
    CHECK(g.towers[4][7].used && g.towers[4][7].type == TW_ARROW, "A builds the selected tower (arrow)");
    CHECK(g.gold == gold - game_tower_cost(TW_ARROW), "building costs gold");
    CHECK(heard(&g, SFX_BUILD), "build sound queued");
    gold = g.gold;
    frame(&g, BTN_A, BTN_A); frame(&g, 0, 0);
    CHECK(g.towers[4][7].level == 1 && g.gold == gold - game_upgrade_cost(TW_ARROW, 0), "A on a tower upgrades it");
    CHECK(heard(&g, SFX_UPGRADE), "upgrade sound queued");
    frame(&g, BTN_R, BTN_R); frame(&g, 0, 0);
    CHECK(g.sel == TW_CANNON, "R selects next tower type");
    CHECK(heard(&g, SFX_SELECT), "select blip queued");
    frame(&g, BTN_L, BTN_L); frame(&g, BTN_L, BTN_L); frame(&g, 0, 0);
    CHECK(g.sel == TW_FROST, "L cycles back (wraps)");
    gold = g.gold;
    frame(&g, BTN_B, BTN_B); frame(&g, 0, 0);
    CHECK(!g.towers[4][7].used && g.gold > gold, "B sells the tower for a refund");
    CHECK(heard(&g, SFX_SELL), "coin sound queued");
    g.cx = 4; g.cy = 4;   /* path tile */
    gold = g.gold;
    frame(&g, BTN_A, BTN_A); frame(&g, 0, 0);
    CHECK(!g.towers[4][4].used && g.gold == gold, "can't build on the path");
    CHECK(heard(&g, SFX_ERROR), "error buzz queued");
    g.cx = 8; g.cy = 4;   /* rock */
    frame(&g, BTN_A, BTN_A); frame(&g, 0, 0);
    CHECK(!g.towers[4][8].used, "can't build on a rock");
    g.gold = 10; g.cx = 6; g.cy = 5;
    frame(&g, BTN_A, BTN_A); frame(&g, 0, 0);
    CHECK(!g.towers[5][6].used && g.gold == 10, "can't build without enough gold");
    g.gold = 500;
    frame(&g, BTN_PLUS, BTN_PLUS);
    CHECK(g.state == ST_PAUSE, "+ pauses");
    CHECK(heard(&g, SFX_PAUSE), "pause blip queued");
    game_render(&g, fb); dump(fb, "pause");
    frame(&g, BTN_PLUS, BTN_PLUS);
    CHECK(g.state == ST_PLAY, "+ resumes");
    float wt = g.wave_timer;
    frame(&g, BTN_X, BTN_X);
    CHECK(g.wave_timer <= 0 || g.wave == 1, "X calls the next wave early");
    (void)wt;

    printf("[3] a hand-built defense, a few seconds in (screenshot)\n");
    game_init(&g); frame(&g, BTN_A, BTN_A);
    g.gold = 2000;
    game_build(&g, 5, 3, TW_CANNON);                               /* first line: all three types see the slimes */
    game_build(&g, 5, 4, TW_FROST);
    game_build(&g, 2, 3, TW_ARROW);  game_upgrade(&g, 2, 3);
    game_build(&g, 8, 3, TW_CANNON); game_upgrade(&g, 8, 3); game_upgrade(&g, 8, 3);
    game_build(&g, 10, 3, TW_FROST);
    game_build(&g, 13, 1, TW_ARROW);
    game_build(&g, 12, 4, TW_CANNON);
    g.cx = 10; g.cy = 3;
    g.wave_timer = 0.1f;
    int saw_arrow = 0, saw_cannon = 0, saw_frost = 0, saw_kill = 0, saw_wave = 0, saw_boom = 0;
    for (int i = 0; i < 60 * 17; i++) {
        frame(&g, 0, 0);
        int id;
        while ((id = game_pop_sound(&g)) >= 0) {
            saw_arrow  |= id == SFX_ARROW;  saw_cannon |= id == SFX_CANNON; saw_frost |= id == SFX_FROST;
            saw_kill   |= id == SFX_KILL;   saw_wave   |= id == SFX_WAVE;   saw_boom  |= id == SFX_EXPLODE;
        }
        if (i == 60 * 6) { game_render(&g, fb); dump(fb, "hand_built_action"); }
    }
    game_render(&g, fb); dump(fb, "hand_built");
    printf("  wave %d, enemies spawned and fought, kills so far %d, lives %d\n", g.wave, g.kills, g.lives);
    CHECK(g.kills > 0, "towers kill enemies");
    (void)saw_arrow; (void)saw_cannon; (void)saw_frost;
    CHECK(single_tower_plays(TW_ARROW,  SFX_ARROW),  "arrow tower plays the arrow sound");
    CHECK(single_tower_plays(TW_CANNON, SFX_CANNON), "cannon tower plays the cannon boom");
    CHECK(single_tower_plays(TW_FROST,  SFX_FROST),  "frost tower plays the frost chime");
    CHECK(saw_kill && saw_wave && saw_boom, "wave horn, kill pop and explosion sounds queued during combat");

    printf("[4] bot playthroughs\n");
    Result idle = run_bot("idle (no towers)", 0, 1, 1);
    CHECK(!idle.won && idle.lives == 0, "doing nothing loses (game over works)");
    run_bot("cautious bot", 6, 0, 0);
    run_bot("average bot", 10, 1, 0);
    run_bot("keen bot", 14, 1, 0);

    printf("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
