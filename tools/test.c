/* Native test harness for game.c: scripted input, bot playthroughs, frame dumps. */
#include "../source/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const float DT = 1.0f / 60.0f;
static int failures = 0;
static const char *outdir = ".";

#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } else printf("  ok:   %s\n", msg); } while (0)

/* frame indices (.bin) plus the palette in force when it was drawn (.pal), for tools/topng.py */
static void dump(const Game *g, const uint8_t *fb, const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s.bin", outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite(fb, 1, SCREEN_W * SCREEN_H, f);
    fclose(f);
    snprintf(path, sizeof path, "%s/%s.pal", outdir, name);
    f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    uint8_t pal[PAL_N][3];
    game_get_palette(g, pal);
    fwrite(pal, 1, sizeof pal, f);
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

/* a more careful player: keeps enough towers for the wave, then pours gold into upgrades (arrows and cannons first) */
static void smart_act(Game *g)
{
    static const int order[] = {TW_ARROW, TW_CANNON, TW_ARROW, TW_FROST, TW_CANNON, TW_ARROW};
    int n = count_towers(g);
    int need = 4 + g->wave * 6 / 10;
    if (n >= need) {
        int bx = -1, by = -1, bl = 9, bt = 9;
        for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) {
            const Tower *t = &g->towers[y][x];
            if (!t->used || t->level >= 2) continue;
            if (t->level < bl || (t->level == bl && t->type < bt)) { bl = t->level; bt = t->type; bx = x; by = y; }
        }
        if (bx >= 0) { if (g->gold >= game_upgrade_cost(g->towers[by][bx].type, g->towers[by][bx].level)) game_upgrade(g, bx, by); return; }
    }
    int type = order[n % 6], bx = -1, by = -1, bs = 0;
    if (g->gold < game_tower_cost(type)) return;
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) {
        if (!game_is_buildable(g, x, y)) continue;
        int sc = tile_score(x, y, type);
        if (sc > bs) { bs = sc; bx = x; by = y; }
    }
    if (bx >= 0) game_build(g, bx, by, type);
}

typedef struct { int wave, lives, kills, won; float seconds; } Result;

static Result run_bot(const char *label, int level, int aggression, int dump_shots, int idle)
{
    static Game g;
    static uint8_t fb[SCREEN_W * SCREEN_H];
    game_init(&g);
    game_start_level(&g, level);
    long f = 0;
    int shot_wave = 0;
    int peak_per_sec = 0, sec_events = 0;
    while (g.state == ST_PLAY && f < 60L * 60 * 40) {
        frame(&g, 0, 0);
        f++;
        { int before = sfx_total_events; drain(&g); sec_events += sfx_total_events - before; }
        if (f % 60 == 0) { if (sec_events > peak_per_sec) peak_per_sec = sec_events; sec_events = 0; }
        if (!idle && f % 20 == 0) { if (aggression == 99) smart_act(&g); else bot_act(&g, aggression); }
        if (dump_shots && g.wave_active && g.wave != shot_wave && (g.wave == 3 || g.wave == 10 || g.wave == 15 || g.wave == 20 || g.wave == g.nwaves)) {
            /* wait ~14s into the wave so there is action on screen */
            for (int i = 0; i < 60 * 14 && g.state == ST_PLAY; i++) { frame(&g, 0, 0); f++; if (f % 20 == 0) { if (aggression == 99) smart_act(&g); else bot_act(&g, aggression); } }
            char name[64]; snprintf(name, sizeof name, "%s_wave%d", label, g.wave);
            game_render(&g, fb); dump(&g, fb, name);
            shot_wave = g.wave;
        }
    }
    Result r = {g.wave, g.lives, g.kills, g.state == ST_WIN, (float)f / 60.0f};
    if (dump_shots) {
        for (int i = 0; i < 90; i++) frame(&g, 0, 0);
        game_render(&g, fb);
        char name[64]; snprintf(name, sizeof name, "%s_end", label);
        dump(&g, fb, name);
    }
    printf("  %-22s wave %2d/%d  lives %2d  kills %3d  %s  (%.0f s game time, busiest second: %d sound events)\n", label, r.wave, g.nwaves, r.lives, r.kills,
           r.won ? "WON " : "LOST", r.seconds, peak_per_sec);
    return r;
}

int main(int argc, char **argv)
{
    if (argc > 1) outdir = argv[1];
    const char *only = argc > 2 ? argv[2] : NULL;          /* "levels": skip straight to the storm-level tests */
    static Game g;
    static uint8_t fb[SCREEN_W * SCREEN_H];
    if (only) goto levels;

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
    game_debug_sheet(fb); dump(&g, fb, "sheet");
    game_init(&g);
    for (int i = 0; i < 200; i++) frame(&g, 0, 0);
    game_render(&g, fb); dump(&g, fb, "title");
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
    game_render(&g, fb); dump(&g, fb, "pause");
    frame(&g, BTN_PLUS, BTN_PLUS);
    CHECK(g.state == ST_PLAY, "+ resumes");
    frame(&g, BTN_PLUS, BTN_PLUS);
    frame(&g, BTN_DOWN, BTN_DOWN);
    CHECK(g.state == ST_PAUSE && g.pause_sel == 1, "pause menu: down selects quit");
    game_render(&g, fb); dump(&g, fb, "pause_quit");
    frame(&g, BTN_A, BTN_A);
    CHECK(g.state == ST_TITLE, "quit from the pause menu returns to the title screen");
    game_start(&g);
    CHECK(g.state == ST_PLAY, "a new run can start after quitting");
    frame(&g, BTN_PLUS, BTN_PLUS); frame(&g, BTN_A, BTN_A);
    CHECK(g.state == ST_PLAY, "A on RESUME continues the game");
    g.gold = 500; g.cx = 6; g.cy = 5;
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
        if (i == 60 * 6) { game_render(&g, fb); dump(&g, fb, "hand_built_action"); }
    }
    game_render(&g, fb); dump(&g, fb, "hand_built");
    printf("  wave %d, enemies spawned and fought, kills so far %d, lives %d\n", g.wave, g.kills, g.lives);
    CHECK(g.kills > 0, "towers kill enemies");
    (void)saw_arrow; (void)saw_cannon; (void)saw_frost;
    CHECK(single_tower_plays(TW_ARROW,  SFX_ARROW),  "arrow tower plays the arrow sound");
    CHECK(single_tower_plays(TW_CANNON, SFX_CANNON), "cannon tower plays the cannon boom");
    CHECK(single_tower_plays(TW_FROST,  SFX_FROST),  "frost tower plays the frost chime");
    CHECK(saw_kill && saw_wave && saw_boom, "wave horn, kill pop and explosion sounds queued during combat");

    printf("[4] bot playthroughs\n");
    Result idle = run_bot("idle (no towers)", 0, 0, 1, 1);
    CHECK(!idle.won && idle.lives == 0, "doing nothing loses (game over works)");
    run_bot("cautious bot", 0, 6, 0, 0);
    run_bot("average bot", 0, 10, 1, 0);
    run_bot("keen bot", 0, 14, 1, 0);

levels:
    printf("[5] storm levels (2-4)\n");
    for (int lv = 1; lv < NUM_LEVELS; lv++) {
        char name[64], msg[96];
        game_init(&g); game_start_level(&g, lv);
        snprintf(msg, sizeof msg, "level %d \"%s\": %d waves, path %.0f px", lv + 1, game_level_name(lv), g.nwaves, game_path_length());
        CHECK(g.state == ST_PLAY && g.level == lv && g.nwaves >= 15, msg);
        int free_tiles = 0;
        for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) free_tiles += game_is_buildable(&g, x, y);
        snprintf(msg, sizeof msg, "level %d has plenty of room to build (%d free tiles)", lv + 1, free_tiles);
        CHECK(free_tiles >= 90, msg);
        /* weather: rain + lightning + thunder run on their own; a strike changes the palette */
        int thunder = 0;
        uint8_t pal0[PAL_N][3], pal1[PAL_N][3];
        game_get_palette(&g, pal0);
        int flashed = 0;
        for (int i = 0; i < 60 * 30; i++) {
            frame(&g, 0, 0);
            int id; while ((id = game_pop_sound(&g)) >= 0) thunder += id == SFX_THUNDER;
            game_get_palette(&g, pal1);
            if (memcmp(pal0, pal1, sizeof pal0)) flashed = 1;
        }
        snprintf(msg, sizeof msg, "level %d: lightning flashes the palette and thunder follows (%d claps in 30 s)", lv + 1, thunder);
        CHECK(flashed && thunder > 0, msg);
        /* scene: let the bot dig in for a while, then take screenshots (calm, bolt, mid-flash) */
        game_start_level(&g, lv);
        for (int i = 0; i < 60 * 80 && g.state == ST_PLAY; i++) {
            frame(&g, 0, 0);
            if (i % 20 == 0) bot_act(&g, 14);
            if (i == 60 * 60) g.wave_timer = 0;
        }
        for (int i = 0; i < 60 * 20; i++) { frame(&g, 0, 0); if (i % 20 == 0) bot_act(&g, 14); }
        g.strike_amp = 1.0f; g.bolt_x0 = 90; g.bolt_x1 = 120; g.bolt_y1 = 110; g.bolt_seed = 1234;
        g.strike_t = 1.0f; game_render(&g, fb); snprintf(name, sizeof name, "L%d_calm", lv + 1); dump(&g, fb, name);
        g.strike_t = 0.02f; game_render(&g, fb); snprintf(name, sizeof name, "L%d_bolt", lv + 1); dump(&g, fb, name);
        g.strike_t = 0.30f; game_render(&g, fb); snprintf(name, sizeof name, "L%d_afterflash", lv + 1); dump(&g, fb, name);
    }
    /* living scenery: trees sway, flames flicker, wind drifts over the grass */
    {
        static uint8_t a[SCREEN_W * SCREEN_H], b[SCREEN_W * SCREEN_H];
        game_start_level(&g, 0);
        int tree_changes = 0, grass_changes = 0;
        game_render(&g, a);
        for (int i = 0; i < 40; i++) {
            for (int k = 0; k < 6; k++) frame(&g, 0, 0);
            game_render(&g, b);
            for (int y = 20; y < 36; y++) for (int x = 0; x < 16; x++) tree_changes += a[y * SCREEN_W + x] != b[y * SCREEN_W + x];   /* tree tile 0,0 */
            for (int y = 100; y < 116; y++) for (int x = 150; x < 210; x++) grass_changes += a[y * SCREEN_W + x] != b[y * SCREEN_W + x];
            memcpy(a, b, sizeof a);
        }
        CHECK(tree_changes > 40, "level 1: the tree in the corner sways");
        CHECK(grass_changes > 20, "level 1: wind shimmers over the grass");
    }
    /* storm title preview with the level picker */
    game_start_level(&g, 0); game_init(&g);                    /* the title remembers the last level: reset it to 1 */
    frame(&g, BTN_RIGHT, BTN_RIGHT);
    CHECK(g.level == 1 && g.state == ST_TITLE, "title screen: right on the D-pad picks level 2");
    for (int i = 0; i < 90; i++) frame(&g, 0, 0);
    game_render(&g, fb); dump(&g, fb, "title_L2");
    frame(&g, BTN_LEFT, BTN_LEFT); frame(&g, BTN_LEFT, BTN_LEFT);
    CHECK(g.level == NUM_LEVELS - 1, "title screen: left wraps around to the last level");
    frame(&g, BTN_A, BTN_A);
    CHECK(g.state == ST_PLAY && g.level == NUM_LEVELS - 1, "A starts the chosen level");

    printf("[6] level progression + bot playthroughs\n");
    {
        game_init(&g); game_start_level(&g, 0);
        g.wave = g.nwaves - 1; g.wave_active = 1; g.spawn_i = g.spawn_n = 0;     /* about to finish level 1 */
        g.wave = g.nwaves;
        for (int i = 0; i < 5; i++) frame(&g, 0, 0);
        CHECK(g.state == ST_WIN, "clearing the last wave wins the level");
        for (int i = 0; i < 90; i++) frame(&g, 0, 0);
        game_render(&g, fb); dump(&g, fb, "level_clear");
        frame(&g, BTN_A, BTN_A);
        CHECK(g.state == ST_PLAY && g.level == 1 && g.wave == 0 && g.lives == 20, "A after a win starts the next level fresh");
    }
    for (int lv = 1; lv < NUM_LEVELS; lv++) {
        char lab[48];
        snprintf(lab, sizeof lab, "L%d cautious bot", lv + 1); run_bot(lab, lv, 6, 0, 0);
        snprintf(lab, sizeof lab, "L%d average bot", lv + 1);  run_bot(lab, lv, 10, 1, 0);
        snprintf(lab, sizeof lab, "L%d keen bot", lv + 1);     run_bot(lab, lv, 14, 1, 0);
        snprintf(lab, sizeof lab, "L%d careful player", lv + 1); run_bot(lab, lv, 99, 0, 0);
        snprintf(lab, sizeof lab, "L%d idle", lv + 1);
        Result idle2 = run_bot(lab, lv, 0, 0, 1);
        CHECK(!idle2.won, "doing nothing never wins a storm level");
    }

    printf("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
