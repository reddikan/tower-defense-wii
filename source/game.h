/*
 * TOWER DEFENSE - a retro top-down tower defense game.
 *
 * game.c/game.h are completely platform independent (plain C99). The game
 * renders into a 320x180 (16:9) palette-indexed framebuffer, takes its input
 * as a bitmask of BTN_* values, and emits sound effects as events that the
 * platform layer pops and plays. main.c (Wii / libogc) is the only file that
 * knows about the console.
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include "audio.h"

#define SCREEN_W   320
#define SCREEN_H   180          /* 320x180 == 16:9 */
#define TILE       16
#define MAP_W      20
#define MAP_H      10
#define MAP_Y      20           /* map starts below the 20px HUD */
#define NUM_WAVES  20           /* longest hand-made wave table; every level then continues endlessly */
#define NUM_LEVELS 4
#define PAL_N      160          /* 32 base + 32 warm-lit + 32 cool-lit + 32 faint warm + 32 faint cool (ambient glow) */

#define MAX_ENEMIES 160
#define MAX_PROJ    160
#define MAX_PART    800
#define MAX_TEXT    24
#define MAX_RING    24

/* Input bits (platform layer maps the Classic Controller onto these). */
enum {
    BTN_UP    = 1 << 0,
    BTN_DOWN  = 1 << 1,
    BTN_LEFT  = 1 << 2,
    BTN_RIGHT = 1 << 3,
    BTN_A     = 1 << 4,
    BTN_B     = 1 << 5,
    BTN_X     = 1 << 6,
    BTN_Y     = 1 << 7,
    BTN_L     = 1 << 8,
    BTN_R     = 1 << 9,
    BTN_PLUS  = 1 << 10
};

typedef enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_OVER, ST_WIN } GameState;
enum { TW_ARROW, TW_CANNON, TW_FROST, TW_COUNT };
enum { EN_SLIME, EN_BAT, EN_GOLEM, EN_BOSS, EN_ZOMBIE, EN_PUMPKIN, EN_WITCH, EN_CAT, EN_GHOST, EN_COUNT };

typedef struct {
    uint8_t used, type, level;      /* level is 0..2 */
    float   cd, flash;
    int     invested;
} Tower;

typedef struct {
    uint8_t  alive, type;
    uint32_t uid;
    float    dist, x, y, hp, maxhp, slow_t, slow_f, hitflash, ab_t;
    int      seg;
} Enemy;

typedef struct {
    uint8_t  alive, type, level;
    float    x, y, vx, vy, tx, ty;
    int      target;
    uint32_t tuid;
    float    dmg;
} Proj;

typedef struct { float x, y, vx, vy, life; uint8_t col, size; } Part;
typedef struct { float x, y, life; int val; } FText;
typedef struct { float x, y, maxr, life, max_life; uint8_t col; } Ring;

typedef struct {
    GameState state;
    float     state_t;
    uint32_t  rng, frame, next_uid;

    int level, nwaves;              /* current level 0..NUM_LEVELS-1 and its wave count */
    int lives, gold, wave, kills;
    int cx, cy, sel, fast;
    int rep[4];

    Tower towers[MAP_H][MAP_W];
    Enemy en[MAX_ENEMIES];
    Proj  pr[MAX_PROJ];
    Part  pt[MAX_PART];
    FText ft[MAX_TEXT];
    Ring  rg[MAX_RING];

    int     wave_active;
    float   wave_timer;
    uint8_t spawn_q[512];
    int     spawn_n, spawn_i;
    float   spawn_t;
    float   title_t;

    char  msg[40];    float msg_t;
    char  banner[40]; float banner_t; int banner_col;
    float hurt_t, shake_t, fade;
    int   new_best;                  /* this run set a new best wave */
    int   pause_sel;                 /* pause menu: 0 = resume, 1 = sound on/off, 2 = quit to title */

    /* storm weather (levels 2-4): lightning timeline and a private RNG so gameplay is unaffected */
    float    strike_t, strike_amp, bolt_cd, thunder_t;
    int      bolt_x0, bolt_x1, bolt_y1, bolt_seed, thunder_pending;
    uint32_t wx;

    /* sound events: a small ring buffer plus per-effect throttles */
    uint8_t sfxq[32];
    int     sfx_head, sfx_tail;
    float   sfx_cd[SFX_COUNT];

    uint8_t bg[SCREEN_W * MAP_H * TILE];   /* pre-rendered map */
} Game;

extern const uint8_t game_palette[32][3];      /* level 1 base colours */
/* The colours to show right now (levels have their own palettes and lightning flashes brighten them). */
void game_get_palette(const Game *g, uint8_t out[PAL_N][3]);
const char *game_level_name(int level);

void game_init(Game *g);                       /* title screen state */
void game_start(Game *g);                      /* begin a new run on the selected level */
void game_start_level(Game *g, int level);     /* begin a run on a specific level (0-based) */
int  game_best_wave(int level);                /* best wave reached on a level this session */
int  game_sound_on(void);
void game_update(Game *g, uint32_t held, uint32_t pressed, float dt);
void game_render(const Game *g, uint8_t *fb);  /* fb = SCREEN_W*SCREEN_H bytes */
int  game_pop_sound(Game *g);                  /* next SFX_* id to play, or -1 */

/* helpers (also used by the test harness bot) */
int   game_is_buildable(const Game *g, int tx, int ty);
int   game_build(Game *g, int tx, int ty, int type);
int   game_upgrade(Game *g, int tx, int ty);
int   game_sell(Game *g, int tx, int ty);
int   game_tower_cost(int type);
int   game_upgrade_cost(int type, int level);  /* cost to go from level -> level+1 */
float game_tower_range(int type, int level);
float game_path_length(void);
void  game_path_point(float dist, float *x, float *y);
void  game_debug_sheet(uint8_t *fb);           /* sprite sheet, for testing */

#endif
