/*
 * TOWER DEFENSE - game logic and software renderer (platform independent).
 */
#include "game.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Palette (32 colours, NES-ish and bright)                            */
/* ------------------------------------------------------------------ */
enum {
    C_BLACK, C_DKGRAY, C_GRAY, C_LIGHT, C_WHITE, C_RED, C_DKRED, C_ORANGE,
    C_YELLOW, C_GREEN, C_DKGREEN, C_LTGREEN, C_CYAN, C_BLUE, C_DKBLUE,
    C_PURPLE, C_PINK, C_BROWN, C_DKBROWN, C_TAN, C_GRASS1, C_GRASS2,
    C_DIRT1, C_DIRT2, C_SKY, C_ICE, C_DKPURPLE, C_SKIN, C_STONE, C_DKSTONE,
    C_TEAL, C_CREAM
};

const uint8_t game_palette[32][3] = {
    { 14,  12,  28}, { 44,  40,  68}, {110, 110, 140}, {205, 205, 225},
    {255, 255, 255}, {225,  45,  55}, {140,  25,  45}, {255, 140,  30},
    {255, 225,  70}, { 65, 200,  75}, { 30, 115,  55}, {150, 235,  95},
    { 70, 215, 235}, { 60, 110, 235}, { 30,  50, 140}, {175,  80, 225},
    {255, 120, 185}, {160, 100,  45}, { 95,  55,  32}, {225, 185, 115},
    { 88, 172,  62}, { 72, 150,  52}, {205, 165, 105}, {180, 138,  84},
    { 84, 146, 242}, {175, 238, 255}, { 95,  45, 145}, {255, 205, 155},
    {150, 152, 172}, { 92,  92, 118}, { 40, 150, 160}, {250, 240, 200}
};

/* every colour -> a darker one (used for translucent shading and shadows) */
static const uint8_t DARK[32] = {
    0, 0, 1, 2, 3, 6, 0, 18, 7, 10, 0, 9, 30, 14, 0, 26,
    15, 18, 0, 17, 21, 10, 23, 18, 14, 12, 0, 19, 29, 1, 10, 3
};

/* ------------------------------------------------------------------ */
/* Game data tables                                                    */
/* ------------------------------------------------------------------ */
static const char *const TW_NAME[TW_COUNT] = {"ARROW", "CANNON", "FROST"};
static const int   TW_COST[TW_COUNT]      = {40, 80, 60};
static const int   TW_UP[TW_COUNT][2]     = {{50, 90}, {100, 170}, {70, 120}};
static const float TW_DMG[TW_COUNT][3]    = {{6, 11, 20}, {22, 40, 75}, {2, 4, 7}};
static const float TW_CD[TW_COUNT][3]     = {{0.45f, 0.38f, 0.30f}, {1.4f, 1.3f, 1.2f}, {0.9f, 0.8f, 0.7f}};
static const float TW_RANGE[TW_COUNT][3]  = {{52, 58, 66}, {44, 48, 54}, {46, 52, 58}};
static const float TW_SPLASH[TW_COUNT][3] = {{0, 0, 0}, {18, 21, 25}, {14, 16, 18}};
static const float TW_SLOWF[3]            = {0.55f, 0.45f, 0.35f};
static const float TW_SLOWT[3]            = {1.5f, 1.8f, 2.2f};
static const float PROJ_SPEED[TW_COUNT]   = {230, 140, 170};

static const struct { float hp, speed; int gold, leak; } EN_DEF[EN_COUNT] = {
    {  24, 28,   4,  1},   /* slime */
    {  14, 50,   5,  1},   /* bat   (flying: cannons can't hit it) */
    { 110, 18,  12,  2},   /* golem */
    {1200, 15, 120, 10}    /* boss  */
};

/* Enemy hit points grow by this fraction of their base HP every wave. */
#ifndef HP_GROWTH
#define HP_GROWTH 0.36f
#endif

/* slimes, bats, golems, bosses per wave */
static const uint8_t WAVES[NUM_WAVES][4] = {
    { 8, 0, 0, 0}, {10, 0, 0, 0}, { 8, 4, 0, 0}, {10, 6, 0, 0}, { 6, 0, 3, 0},
    {10, 8, 0, 0}, { 8, 6, 4, 0}, {12,10, 0, 0}, { 8, 8, 6, 0}, {10, 6, 4, 1},
    {14,10, 0, 0}, {10,10, 8, 0}, {12,14, 6, 0}, {14,10,10, 0}, {10,12, 8, 1},
    {16,16, 8, 0}, {12,14,12, 0}, {16,18,10, 1}, {18,18,14, 0}, {20,20,16, 3}
};

/* minimum seconds between two plays of the same sound effect */
static const float SFX_GAP[SFX_COUNT] = {
    0.03f, 0.05f, 0.10f, 0.10f, 0.10f, 0.20f,   /* move select build upgrade sell error */
    0.09f, 0.10f, 0.11f, 0.09f, 0.075f, 0.15f,  /* arrow cannon frost explode kill leak */
    0.50f, 0.50f, 0.50f, 0.50f, 0.10f, 1.00f, 1.00f /* wave boss clear start pause over win */
};

/* Path (tile coordinates). The enemies walk in from off-screen left. */
#define NWP 8
static const int8_t WP[NWP][2] = {
    {0, 2}, {4, 2}, {4, 7}, {9, 7}, {9, 2}, {14, 2}, {14, 7}, {19, 7}
};
static float   path_x[NWP], path_y[NWP], path_cum[NWP];
static int     path_n;
static float   path_len;
static uint8_t is_path[MAP_H][MAP_W];

/* Scenery: T tree, R rock (both block building), F flower (cosmetic). */
static const char *const DECOR[MAP_H] = {
    "T..F.....T....F...TT",
    ".F....R.........F.T.",
    "......F.........F...",
    "...F...T....T.......",
    ".T......R...F...T...",
    "..F....F...T...R....",
    ".....T.F.......F....",
    "..F.......F..T......",
    ".T.....R....F...T...",
    "T..F.....T....F...TT"
};

/* ------------------------------------------------------------------ */
/* Drawing primitives                                                  */
/* ------------------------------------------------------------------ */
static inline void px(uint8_t *fb, int x, int y, int c)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) fb[y * SCREEN_W + x] = (uint8_t)c;
}

static void rect(uint8_t *fb, int x, int y, int w, int h, int c)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) px(fb, x + i, y + j, c);
}

static void frame_rect(uint8_t *fb, int x, int y, int w, int h, int c)
{
    for (int i = 0; i < w; i++) { px(fb, x + i, y, c); px(fb, x + i, y + h - 1, c); }
    for (int j = 0; j < h; j++) { px(fb, x, y + j, c); px(fb, x + w - 1, y + j, c); }
}

/* translucent darkening of a region */
static void darken(uint8_t *fb, int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int xx = x + i, yy = y + j;
            if ((unsigned)xx < SCREEN_W && (unsigned)yy < SCREEN_H) fb[yy * SCREEN_W + xx] = DARK[fb[yy * SCREEN_W + xx]];
        }
}

/* soft ground shadow: only darkens grass/dirt so it never dirties sprites */
static void shadow_px(uint8_t *fb, int x, int y)
{
    if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H) return;
    uint8_t *p = &fb[y * SCREEN_W + x];
    switch (*p) {
    case C_GRASS1: *p = C_GRASS2; break;
    case C_GRASS2: *p = C_DKGREEN; break;
    case C_DIRT1:  *p = C_DIRT2; break;
    case C_DIRT2:  *p = C_DKBROWN; break;
    default: break;
    }
}

static void shadow_ellipse(uint8_t *fb, int cx, int cy, int rx)
{
    for (int i = -rx + 2; i <= rx - 2; i++) shadow_px(fb, cx + i, cy - 1);
    for (int i = -rx; i <= rx; i++)         shadow_px(fb, cx + i, cy);
    for (int i = -rx + 2; i <= rx - 2; i++) shadow_px(fb, cx + i, cy + 1);
}

static const uint8_t BAYER4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static void fade_black(uint8_t *fb, float amount)
{
    int th = (int)(amount * 16.0f);
    if (th <= 0) return;
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            if (BAYER4[(y & 3) * 4 + (x & 3)] < th) fb[y * SCREEN_W + x] = C_BLACK;
}

static void panel(uint8_t *fb, int x, int y, int w, int h, int border)
{
    rect(fb, x, y, w, h, C_BLACK);
    frame_rect(fb, x, y, w, h, border);
    frame_rect(fb, x + 2, y + 2, w - 4, h - 4, C_DKGRAY);
    px(fb, x, y, C_BLACK); px(fb, x + w - 1, y, C_BLACK);                 /* rounded corners */
    px(fb, x, y + h - 1, C_BLACK); px(fb, x + w - 1, y + h - 1, C_BLACK);
}

/* sine/cosine lookup (1024 steps per turn): rings and range circles are drawn every frame */
#define TRIG_N 1024
static float trig_sin[TRIG_N], trig_cos[TRIG_N];
static int   trig_ready;
static void trig_init(void)
{
    if (trig_ready) return;
    for (int i = 0; i < TRIG_N; i++) {
        trig_sin[i] = sinf((float)i * 2.0f * (float)M_PI / TRIG_N);
        trig_cos[i] = cosf((float)i * 2.0f * (float)M_PI / TRIG_N);
    }
    trig_ready = 1;
}

static void ring_outline(uint8_t *fb, int cx, int cy, float r, int c)
{
    int n = (int)(r * 6.3f) + 8;
    for (int i = 0; i < n; i++) {
        int a = i * TRIG_N / n;
        px(fb, cx + (int)lrintf(trig_cos[a] * r), cy + (int)lrintf(trig_sin[a] * r), c);
    }
}

static void dotted_circle(uint8_t *fb, int cx, int cy, float r, int c, int phase)
{
    int n = (int)(2.0f * (float)M_PI * r / 5.0f);
    if (n < 8) n = 8;
    for (int i = 0; i < n; i++) {
        if (((i + phase) & 1) == 0) continue;
        int a = i * TRIG_N / n;
        int x = cx + (int)lrintf(trig_cos[a] * r), y = cy + (int)lrintf(trig_sin[a] * r);
        rect(fb, x, y, 2, 2, c);              /* chunky 2x2 dots read well at TV distance */
    }
}

/* --- 3x5 bitmap font: each glyph = 5 rows, each row a 3-bit octal digit --- */
static const char *const FONT_CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ .:!-+/?><',";
static const char *const FONT_DATA[] = {
    "75557", "26227", "71747", "71717", "55711", "74717", "74757", "71111", "75757", "75717",
    "25755", "65656", "74447", "65556", "74647", "74644", "74557", "55755", "72227", "11157",
    "55655", "44447", "57755", "65555", "75557", "75744", "75571", "65655", "74717", "72222",
    "55557", "55552", "55775", "55255", "55222", "71247",
    "00000", "00002", "02020", "22202", "00700", "02720", "11244", "71302", "42124", "12421",
    "22000", "00024"
};

static void draw_char(uint8_t *fb, int x, int y, char ch, int col, int s)
{
    const char *p = strchr(FONT_CHARS, toupper((unsigned char)ch));
    if (!p || !*p) return;
    const char *d = FONT_DATA[p - FONT_CHARS];
    for (int r = 0; r < 5; r++) {
        int bits = d[r] - '0';
        for (int c = 0; c < 3; c++)
            if (bits & (4 >> c)) rect(fb, x + c * s, y + r * s, s, s, col);
    }
}

static int text_w(const char *t, int s) { int n = (int)strlen(t); return n ? n * 4 * s - s : 0; }

static void draw_text(uint8_t *fb, int x, int y, const char *t, int col, int s)
{
    for (; *t; t++, x += 4 * s) draw_char(fb, x, y, *t, col, s);
}

static void draw_text_sh(uint8_t *fb, int x, int y, const char *t, int col, int s)
{
    int o = (s + 1) / 2;
    draw_text(fb, x + o, y + o, t, C_BLACK, s);
    draw_text(fb, x, y, t, col, s);
}

static void draw_text_center(uint8_t *fb, int y, const char *t, int col, int s)
{
    draw_text_sh(fb, (SCREEN_W - text_w(t, s)) / 2, y, t, col, s);
}

/* ------------------------------------------------------------------ */
/* Sprites (characters map to palette entries, '.' = transparent)      */
/* ------------------------------------------------------------------ */
typedef struct { int w, h; uint8_t d[256]; } Sprite;
enum { S_SLIME, S_BAT0, S_BAT1, S_GOLEM, S_BOSS, S_T_ARROW, S_T_CANNON, S_T_FROST,
       S_TREE, S_ROCK, S_PORTAL, S_HEART, S_COIN, S_COUNT };
static Sprite SPR[S_COUNT];
static int sprites_ready;

static const char *const R_SLIME[16] = {
    "................", "................", "................", "................",
    ".....kkkkkk.....", "....kmmmmmmk....", "...kmmwmmmmmk...", "..kmmmmmmmmmmk..",
    "..kmmwwmmwwmmk..", "..kmmwkmmwkmmk..", "..kemmmmmmmmek..", "..keemmmmmmeek..",
    "...keeeeeeeek...", "....kkkkkkkk....", "................", "................"
};
static const char *const R_BAT0[16] = {
    "................", "................", ".k............k.", ".kk....kk....kk.",
    ".kPk..kPPk..kPk.", ".kPPk.kPPk.kPPk.", ".kPPPkkPPkkPPPk.", ".kPPPPkppkPPPPk.",
    "..kkPPprrpPPkk..", "...kkPppppPkk...", ".....kkppkk.....", ".......kk.......",
    "................", "................", "................", "................"
};
static const char *const R_BAT1[16] = {
    "................", "................", "................", ".......kk.......",
    "......kppk......", ".....kppppk.....", ".kk..kprrpk..kk.", ".kPkkPPppPPkkPk.",
    ".kPPPPPPPPPPPPk.", "..kPPPPkkPPPPk..", ".....kk..kk.....", "................",
    "................", "................", "................", "................"
};
static const char *const R_GOLEM[16] = {
    "................", "................", ".....kkkkkk.....", "....kSSSSSSk....",
    "....kSySSySk....", "....kSSkkSSk....", "..kkkkSSSSkkkk..", ".kSSSkSllSkSSSk.",
    ".kSSTkSSSSkSTSk.", ".kSTTkSSSTkTTSk.", ".kkkkkSSSSTkkkk.", "....kSSSkkSTk...",
    "....kSSSk.kSTk..", "....kTTTk.kTTk..", "....kkkkk.kkkk..", "................"
};
static const char *const R_BOSS[16] = {
    ".k............k.", ".kk..........kk.", ".kRk........kRk.", ".kRRk.kkkk.kRRk.",
    "..kRRkkrrkkRRk..", "...kkrrrrrrkk...", "...krrrrrrrrk...", "..krwwrrrrwwrk..",
    "..krwkrrrrwkrk..", "..krrrrkkrrrrk..", "..krrrrrrrrrrk..", "...kRwkwkwkwRk..",
    "...kkRRRRRRkk...", ".....kkkkkk.....", "................", "................"
};
static const char *const R_T_ARROW[16] = {
    "................", ".......kk.......", "......keek......", ".....keemek.....",
    "....keemmmek....", "...keeemmmeek...", "..kEEEEEEEEEEk..", "..kkkkkkkkkkkk..",
    "...kSSSSSSSSk...", "...kSgSSSSgSk...", "...kSSSkkSSSk...", "...kSTSkkSTSk...",
    "...kSTSSSSTSk...", "..kkTTTTTTTTkk..", "..kkkkkkkkkkkk..", "................"
};
static const char *const R_T_CANNON[16] = {
    "................", ".....kkkkkk.....", "....kddddddk....", "....kdgddddk....",
    "....kdgddddk....", "....kddddddk....", "....kkddddkk....", "...kRRRRRRRRk...",
    "...kkkkkkkkkk...", "..kSSSSSSSSSSk..", "..kSgSSSSSSgSk..", "..kSSSSddSSSSk..",
    "..kSTSSddSSSTk..", "..kSTTTTTTTTTk..", "..kkkkkkkkkkkk..", "................"
};
static const char *const R_T_FROST[16] = {
    "................", ".......kk.......", "......kiik......", ".....kiwick.....",
    "....kiwiccck....", "...kiwiicccck...", "...kiwiccccbk...", "....kicccbbk....",
    ".....kcbbbk.....", "......kbbk......", ".......kk.......", "...kSSSSSSSSk...",
    "...kSgSSSSgSk...", "..kSTTTTTTTTSk..", "..kkkkkkkkkkkk..", "................"
};
static const char *const R_TREE[16] = {
    "................", ".....kkkkkk.....", "...kkEEmmEEkk...", "..kEEmmmmmmEEk..",
    "..kEmmeemmmeEk..", ".kEEmeeeemmeeEk.", ".kEmmeeeeeemmEk.", ".kEemmeeeeemeEk.",
    ".kEEeemmeeeeEEk.", "..kEEeeeeeeEEk..", "..kkEEEEEEEEkk..", "...kkkkNNkkkk...",
    ".......kNNk.....", "......kNNNNk....", "......kkkkkk....", "................"
};
static const char *const R_ROCK[16] = {
    "................", "................", "................", "................",
    "................", ".......kkkk.....", ".....kkSSSSkk...", "....kSSllSSSSk..",
    "...kSSlSSSSSTSk.", "...kSSSSSSSSTTk.", "..kSSSSSSSSSTTTk", "..kSSSSSSSTTTTTk",
    "..kTTTTTTTTTTTTk", "...kkkkkkkkkkkk.", "................", "................"
};
static const char *const R_PORTAL[16] = {
    "................", ".....kkkkkk.....", "...kkPPPPPPkk...", "..kPPppppppPPk..",
    ".kPppphhhhpppPk.", ".kPphhhwwhhhpPk.", "kPpphhwwwwhhppPk", "kPphhwwwwwwhhpPk",
    "kPphhwwwwwwhhpPk", "kPpphhwwwwhhppPk", ".kPphhhwwhhhpPk.", ".kPppphhhhpppPk.",
    "..kPPppppppPPk..", "...kkPPPPPPkk...", ".....kkkkkk.....", "................"
};
static const char *const R_HEART[6] = {
    ".rr.rr.", "rrrrrrr", "rrrrrrr", ".rrrrr.", "..rrr..", "...r..."
};
static const char *const R_COIN[6] = {
    ".oooo.", "oyyyyo", "oyoyyo", "oyyyyo", "oyyyyo", ".oooo."
};

static int pal_of(char c)
{
    switch (c) {
    case 'k': return C_BLACK;   case 'd': return C_DKGRAY;  case 'g': return C_GRAY;
    case 'l': return C_LIGHT;   case 'w': return C_WHITE;   case 'r': return C_RED;
    case 'R': return C_DKRED;   case 'o': return C_ORANGE;  case 'y': return C_YELLOW;
    case 'e': return C_GREEN;   case 'E': return C_DKGREEN; case 'm': return C_LTGREEN;
    case 'c': return C_CYAN;    case 'b': return C_BLUE;    case 'B': return C_DKBLUE;
    case 'p': return C_PURPLE;  case 'h': return C_PINK;    case 'n': return C_BROWN;
    case 'N': return C_DKBROWN; case 't': return C_TAN;     case 'i': return C_ICE;
    case 'P': return C_DKPURPLE;case 's': return C_SKIN;    case 'S': return C_STONE;
    case 'T': return C_DKSTONE;
    default:  return -1;
    }
}

static void make_sprite(Sprite *s, const char *const *rows, int w, int h)
{
    s->w = w; s->h = h;
    for (int y = 0; y < h; y++) {
#ifdef GAME_DEBUG
        if ((int)strlen(rows[y]) != w) fprintf(stderr, "sprite row length error: '%s'\n", rows[y]);
#endif
        for (int x = 0; x < w; x++) {
            int c = pal_of(rows[y][x]);
            s->d[y * w + x] = (c < 0) ? 255 : (uint8_t)c;
        }
    }
}

static void sprites_init(void)
{
    if (sprites_ready) return;
    trig_init();
    make_sprite(&SPR[S_SLIME],    R_SLIME,    16, 16);
    make_sprite(&SPR[S_BAT0],     R_BAT0,     16, 16);
    make_sprite(&SPR[S_BAT1],     R_BAT1,     16, 16);
    make_sprite(&SPR[S_GOLEM],    R_GOLEM,    16, 16);
    make_sprite(&SPR[S_BOSS],     R_BOSS,     16, 16);
    make_sprite(&SPR[S_T_ARROW],  R_T_ARROW,  16, 16);
    make_sprite(&SPR[S_T_CANNON], R_T_CANNON, 16, 16);
    make_sprite(&SPR[S_T_FROST],  R_T_FROST,  16, 16);
    make_sprite(&SPR[S_TREE],     R_TREE,     16, 16);
    make_sprite(&SPR[S_ROCK],     R_ROCK,     16, 16);
    make_sprite(&SPR[S_PORTAL],   R_PORTAL,   16, 16);
    make_sprite(&SPR[S_HEART],    R_HEART,     7,  6);
    make_sprite(&SPR[S_COIN],     R_COIN,      6,  6);
    sprites_ready = 1;
}

/* blit modes: tier 2 = blue trim, tier 3 = gold trim (towers); ghost = see-through placement preview */
enum { M_NORMAL, M_FLASH, M_FROZEN, M_DIM, M_TIER2, M_TIER3, M_GHOST, M_PULSE };
static int blit_phase;

static void blit(uint8_t *fb, const Sprite *s, int x, int y, int mode)
{
    if (mode == M_FROZEN || mode == M_DIM || mode == M_GHOST) {      /* position-dependent (checkerboard) modes */
        for (int j = 0; j < s->h; j++)
            for (int i = 0; i < s->w; i++) {
                int c = s->d[j * s->w + i];
                if (c == 255) continue;
                if (mode == M_FROZEN) { if ((i + j) & 1) c = (c == C_BLACK) ? C_DKBLUE : C_ICE; }
                else if (mode == M_DIM) { if ((i + j) & 1) c = C_DKGRAY; }
                else if ((i + j) & 1) continue;
                px(fb, x + i, y + j, c);
            }
        return;
    }

    /* everything else is a plain colour remap, so build a tiny LUT once and run a tight, pre-clipped loop */
    uint8_t map[32];
    for (int c = 0; c < 32; c++) map[c] = (uint8_t)c;
    switch (mode) {
    case M_FLASH:
        for (int c = 0; c < 32; c++) map[c] = C_WHITE;
        break;
    case M_TIER2:
        map[C_DKSTONE] = C_BLUE; map[C_GRAY] = C_ICE;
        break;
    case M_TIER3:
        map[C_DKSTONE] = C_ORANGE; map[C_GRAY] = C_YELLOW; map[C_STONE] = C_TAN;
        break;
    case M_PULSE:
        map[C_PINK]   = (blit_phase & 1) ? C_PURPLE : C_PINK;
        map[C_PURPLE] = (blit_phase & 1) ? C_PINK : C_PURPLE;
        map[C_WHITE]  = (blit_phase & 1) ? C_PINK : C_WHITE;
        break;
    default:
        break;
    }
    int j0 = y < 0 ? -y : 0, j1 = (y + s->h > SCREEN_H) ? SCREEN_H - y : s->h;
    int i0 = x < 0 ? -x : 0, i1 = (x + s->w > SCREEN_W) ? SCREEN_W - x : s->w;
    for (int j = j0; j < j1; j++) {
        uint8_t *d = fb + (y + j) * SCREEN_W + x;
        const uint8_t *r = s->d + j * s->w;
        for (int i = i0; i < i1; i++) {
            uint8_t c = r[i];
            if (c != 255) d[i] = map[c & 31];
        }
    }
}

static void blit_scaled(uint8_t *fb, const Sprite *s, int x, int y, int k)
{
    for (int j = 0; j < s->h; j++)
        for (int i = 0; i < s->w; i++) {
            int c = s->d[j * s->w + i];
            if (c != 255) rect(fb, x + i * k, y + j * k, k, k, c);
        }
}

/* ------------------------------------------------------------------ */
/* Map / path setup + background pre-render                            */
/* ------------------------------------------------------------------ */
static uint32_t hash2(int x, int y, int s)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void path_init(void)
{
    memset(is_path, 0, sizeof is_path);
    path_n = 0;
    path_x[0] = -8.0f;
    path_y[0] = (float)(MAP_Y + WP[0][1] * TILE + 8);
    path_n = 1;
    for (int i = 1; i < NWP; i++) {
        path_x[path_n] = (float)(WP[i][0] * TILE + 8);
        path_y[path_n] = (float)(MAP_Y + WP[i][1] * TILE + 8);
        path_n++;
    }
    for (int i = 0; i < NWP - 1; i++) {
        int x = WP[i][0], y = WP[i][1], x1 = WP[i + 1][0], y1 = WP[i + 1][1];
        int dx = (x1 > x) - (x1 < x), dy = (y1 > y) - (y1 < y);
        for (;;) {
            is_path[y][x] = 1;
            if (x == x1 && y == y1) break;
            x += dx; y += dy;
        }
    }
    path_cum[0] = 0;
    for (int i = 1; i < path_n; i++) {
        float dx = path_x[i] - path_x[i - 1], dy = path_y[i] - path_y[i - 1];
        path_cum[i] = path_cum[i - 1] + sqrtf(dx * dx + dy * dy);
    }
    path_len = path_cum[path_n - 1];
}

float game_path_length(void) { return path_len; }

void game_path_point(float d, float *x, float *y)
{
    int s = 0;
    if (d < 0) d = 0;
    if (d > path_len) d = path_len;
    while (s < path_n - 2 && d > path_cum[s + 1]) s++;
    float t = (d - path_cum[s]) / (path_cum[s + 1] - path_cum[s]);
    *x = path_x[s] + (path_x[s + 1] - path_x[s]) * t;
    *y = path_y[s] + (path_y[s + 1] - path_y[s]) * t;
}

static int decor_at(int tx, int ty)
{
    switch (DECOR[ty][tx]) { case 'T': return 1; case 'R': return 2; case 'F': return 3; default: return 0; }
}

/* draw into the map buffer (320 x 160) */
static void bgpx(uint8_t *bg, int x, int y, int c)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < MAP_H * TILE) bg[y * SCREEN_W + x] = (uint8_t)c;
}

static void bg_blit(uint8_t *bg, const Sprite *s, int x, int y)
{
    for (int j = 0; j < s->h; j++)
        for (int i = 0; i < s->w; i++) {
            int c = s->d[j * s->w + i];
            if (c != 255) bgpx(bg, x + i, y + j, c);
        }
}

static void build_bg(Game *g)
{
    uint8_t *bg = g->bg;
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            int x0 = tx * TILE, y0 = ty * TILE;
            if (is_path[ty][tx]) {
                for (int j = 0; j < TILE; j++) for (int i = 0; i < TILE; i++) bgpx(bg, x0 + i, y0 + j, C_DIRT1);
                for (int k = 0; k < 9; k++) {
                    uint32_t h = hash2(tx, ty, k);
                    bgpx(bg, x0 + 2 + (int)(h % 12), y0 + 2 + (int)((h >> 8) % 12), C_DIRT2);
                }
                for (int k = 0; k < 2; k++) {
                    uint32_t h = hash2(tx, ty, 50 + k);
                    int ox = 2 + (int)(h % 11), oy = 2 + (int)((h >> 8) % 11);
                    bgpx(bg, x0 + ox, y0 + oy, C_TAN); bgpx(bg, x0 + ox + 1, y0 + oy, C_TAN);
                    bgpx(bg, x0 + ox, y0 + oy + 1, C_DIRT2); bgpx(bg, x0 + ox + 1, y0 + oy + 1, C_DIRT2);
                }
                /* edges where the path meets grass */
                int l = tx > 0 && !is_path[ty][tx - 1], r = tx < MAP_W - 1 && !is_path[ty][tx + 1];
                int u = ty > 0 && !is_path[ty - 1][tx], d = ty < MAP_H - 1 && !is_path[ty + 1][tx];
                for (int k = 0; k < TILE; k++) {
                    if (l) { bgpx(bg, x0, y0 + k, C_DKBROWN);          bgpx(bg, x0 + 1, y0 + k, C_DIRT2); }
                    if (r) { bgpx(bg, x0 + 15, y0 + k, C_DKBROWN);     bgpx(bg, x0 + 14, y0 + k, C_DIRT2); }
                    if (u) { bgpx(bg, x0 + k, y0, C_DKBROWN);          bgpx(bg, x0 + k, y0 + 1, C_DIRT2); }
                    if (d) { bgpx(bg, x0 + k, y0 + 15, C_DKBROWN);     bgpx(bg, x0 + k, y0 + 14, C_DIRT2); }
                }
                /* soften the outer corners of bends */
                if (l && u) bgpx(bg, x0 + 1, y0 + 1, C_GRASS1);
                if (r && u) bgpx(bg, x0 + 14, y0 + 1, C_GRASS1);
                if (l && d) bgpx(bg, x0 + 1, y0 + 14, C_GRASS1);
                if (r && d) bgpx(bg, x0 + 14, y0 + 14, C_GRASS1);
            } else {
                for (int j = 0; j < TILE; j++) for (int i = 0; i < TILE; i++) bgpx(bg, x0 + i, y0 + j, C_GRASS1);
                for (int k = 0; k < 5; k++) {
                    uint32_t h = hash2(tx, ty, 100 + k);
                    int ox = 1 + (int)(h % 12), oy = 1 + (int)((h >> 8) % 13);
                    bgpx(bg, x0 + ox, y0 + oy + 1, C_GRASS2); bgpx(bg, x0 + ox + 1, y0 + oy, C_GRASS2);
                    bgpx(bg, x0 + ox + 2, y0 + oy + 1, C_GRASS2);
                }
                for (int k = 0; k < 3; k++) {
                    uint32_t h = hash2(tx, ty, 200 + k);
                    bgpx(bg, x0 + (int)(h % 16), y0 + (int)((h >> 8) % 16), C_LTGREEN);
                }
                int dc = decor_at(tx, ty);
                if (dc == 1) {
                    for (int i = -5; i <= 5; i++) { bgpx(bg, x0 + 8 + i, y0 + 14, C_GRASS2); bgpx(bg, x0 + 8 + i, y0 + 15, C_GRASS2); }
                    bg_blit(bg, &SPR[S_TREE], x0, y0);
                } else if (dc == 2) {
                    for (int i = -6; i <= 6; i++) bgpx(bg, x0 + 8 + i, y0 + 14, C_GRASS2);
                    bg_blit(bg, &SPR[S_ROCK], x0, y0);
                } else if (dc == 3) {
                    static const uint8_t cols[3] = {C_PINK, C_YELLOW, C_WHITE};
                    for (int k = 0; k < 3; k++) {
                        uint32_t h = hash2(tx, ty, 300 + k);
                        int ox = 2 + (int)(h % 11), oy = 2 + (int)((h >> 8) % 11), c = cols[(h >> 16) % 3];
                        bgpx(bg, x0 + ox, y0 + oy - 1, c); bgpx(bg, x0 + ox - 1, y0 + oy, c);
                        bgpx(bg, x0 + ox + 1, y0 + oy, c); bgpx(bg, x0 + ox, y0 + oy + 1, c);
                        bgpx(bg, x0 + ox, y0 + oy, C_ORANGE);
                    }
                }
            }
        }

    /* enemy portal at the entrance (pulses at runtime, drawn over this) */
    bg_blit(bg, &SPR[S_PORTAL], WP[0][0] * TILE, WP[0][1] * TILE);

    /* the keep at the exit (drawn procedurally; the flag animates at runtime) */
    {
        int x = WP[NWP - 1][0] * TILE, y = WP[NWP - 1][1] * TILE;
        for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) bgpx(bg, x + i, y + j, C_DIRT1);
        for (int j = 4; j < 16; j++) for (int i = 1; i < 15; i++) bgpx(bg, x + i, y + j, C_STONE);
        for (int j = 4; j < 16; j++) { bgpx(bg, x + 1, y + j, C_BLACK); bgpx(bg, x + 14, y + j, C_BLACK); }
        for (int i = 1; i < 15; i++) { bgpx(bg, x + i, y + 15, C_BLACK); }
        for (int j = 5; j < 15; j++) bgpx(bg, x + 13, y + j, C_DKSTONE);          /* shaded right side */
        for (int k = 0; k < 4; k++) {          /* merlons */
            for (int j = 2; j < 5; j++) for (int i = 0; i < 3; i++) bgpx(bg, x + 1 + k * 4 + i, y + j, C_STONE);
            for (int i = 0; i < 3; i++) bgpx(bg, x + 1 + k * 4 + i, y + 1, C_BLACK);
            bgpx(bg, x + k * 4, y + 2, C_BLACK); bgpx(bg, x + k * 4, y + 3, C_BLACK);
        }
        for (int j = 8; j < 16; j++) for (int i = 5; i < 11; i++) bgpx(bg, x + i, y + j, C_DKBROWN);
        for (int i = 6; i < 10; i++) bgpx(bg, x + i, y + 7, C_DKBROWN);
        bgpx(bg, x + 9, y + 12, C_YELLOW);                                         /* door handle */
        for (int j = 5; j < 7; j++) for (int i = 3; i < 5; i++) bgpx(bg, x + i, y + j, C_DKSTONE);
        for (int j = 5; j < 7; j++) for (int i = 11; i < 13; i++) bgpx(bg, x + i, y + j, C_DKSTONE);
        for (int j = 0; j < 5; j++) bgpx(bg, x + 8, y + j, C_BLACK);          /* flag pole */
    }
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
static uint32_t rnd(Game *g)
{
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->rng = x;
    return x;
}
static float rndf(Game *g) { return (float)(rnd(g) >> 8) * (1.0f / 16777216.0f); }

/* queue a sound effect for the platform layer (throttled per effect) */
static void sfx(Game *g, int id)
{
    if (g->sfx_cd[id] > 0) return;
    g->sfx_cd[id] = SFX_GAP[id];
    int n = (g->sfx_head + 1) & 31;
    if (n == g->sfx_tail) return;                 /* queue full: drop */
    g->sfxq[g->sfx_head] = (uint8_t)id;
    g->sfx_head = n;
}

int game_pop_sound(Game *g)
{
    if (g->sfx_tail == g->sfx_head) return -1;
    int id = g->sfxq[g->sfx_tail];
    g->sfx_tail = (g->sfx_tail + 1) & 31;
    return id;
}

static void set_msg(Game *g, const char *m)
{
    snprintf(g->msg, sizeof g->msg, "%s", m);
    g->msg_t = 1.6f;
    sfx(g, SFX_ERROR);
}

static void set_banner(Game *g, const char *m, int col)
{
    snprintf(g->banner, sizeof g->banner, "%s", m);
    g->banner_t = 2.0f;
    g->banner_col = col;
}

static void add_part(Game *g, float x, float y, float vx, float vy, float life, int col, int size)
{
    for (int i = 0; i < MAX_PART; i++)
        if (g->pt[i].life <= 0) {
            g->pt[i] = (Part){x, y, vx, vy, life, (uint8_t)col, (uint8_t)size};
            return;
        }
}

static void burst(Game *g, float x, float y, int n, int c1, int c2, float speed, float life)
{
    for (int i = 0; i < n; i++) {
        float a = rndf(g) * 2.0f * (float)M_PI, s = speed * (0.35f + 0.65f * rndf(g));
        add_part(g, x, y, cosf(a) * s, sinf(a) * s, life * (0.6f + 0.4f * rndf(g)), (i & 1) ? c1 : c2, 1 + (int)(rnd(g) & 1));
    }
}

static void add_ring(Game *g, float x, float y, float maxr, float life, int col)
{
    for (int i = 0; i < MAX_RING; i++)
        if (g->rg[i].life <= 0) { g->rg[i] = (Ring){x, y, maxr, life, life, (uint8_t)col}; return; }
}

static void add_text(Game *g, float x, float y, int val)
{
    for (int i = 0; i < MAX_TEXT; i++)
        if (g->ft[i].life <= 0) { g->ft[i] = (FText){x, y, 0.8f, val}; return; }
}

int   game_tower_cost(int type)                { return TW_COST[type]; }
int   game_upgrade_cost(int type, int level)   { return level >= 2 ? 0 : TW_UP[type][level]; }
float game_tower_range(int type, int level)    { return TW_RANGE[type][level]; }

/* ------------------------------------------------------------------ */
/* Building                                                            */
/* ------------------------------------------------------------------ */
int game_is_buildable(const Game *g, int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return 0;
    if (is_path[ty][tx]) return 0;
    if (decor_at(tx, ty) == 1 || decor_at(tx, ty) == 2) return 0;
    return !g->towers[ty][tx].used;
}

static void tile_center(int tx, int ty, float *x, float *y)
{
    *x = (float)(tx * TILE + 8);
    *y = (float)(MAP_Y + ty * TILE + 8);
}

int game_build(Game *g, int tx, int ty, int type)
{
    if (!game_is_buildable(g, tx, ty)) { set_msg(g, "CAN'T BUILD HERE"); return 0; }
    if (g->gold < TW_COST[type])       { set_msg(g, "NOT ENOUGH GOLD"); return 0; }
    g->gold -= TW_COST[type];
    Tower *t = &g->towers[ty][tx];
    t->used = 1; t->type = (uint8_t)type; t->level = 0; t->cd = 0.25f; t->invested = TW_COST[type]; t->flash = 0;
    float x, y; tile_center(tx, ty, &x, &y);
    burst(g, x, y + 5, 12, C_TAN, C_DIRT2, 45, 0.4f);
    add_ring(g, x, y + 5, 11, 0.25f, C_WHITE);
    sfx(g, SFX_BUILD);
    return 1;
}

int game_upgrade(Game *g, int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return 0;
    Tower *t = &g->towers[ty][tx];
    if (!t->used) return 0;
    if (t->level >= 2)               { set_msg(g, "ALREADY MAX LEVEL"); return 0; }
    int cost = TW_UP[t->type][t->level];
    if (g->gold < cost)              { set_msg(g, "NOT ENOUGH GOLD"); return 0; }
    g->gold -= cost; t->invested += cost; t->level++;
    float x, y; tile_center(tx, ty, &x, &y);
    burst(g, x, y, 16, C_YELLOW, C_WHITE, 60, 0.55f);
    add_ring(g, x, y, 15, 0.35f, C_YELLOW);
    sfx(g, SFX_UPGRADE);
    return 1;
}

int game_sell(Game *g, int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return 0;
    Tower *t = &g->towers[ty][tx];
    if (!t->used) return 0;
    int refund = t->invested * 70 / 100;
    g->gold += refund;
    float x, y; tile_center(tx, ty, &x, &y);
    add_text(g, x, y - 6, refund);
    burst(g, x, y, 12, C_GRAY, C_LIGHT, 50, 0.4f);
    sfx(g, SFX_SELL);
    memset(t, 0, sizeof *t);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Simulation                                                          */
/* ------------------------------------------------------------------ */
static void spawn_enemy(Game *g, int type)
{
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (!g->en[i].alive) {
            Enemy *e = &g->en[i];
            memset(e, 0, sizeof *e);
            e->alive = 1; e->type = (uint8_t)type; e->uid = ++g->next_uid;
            float scale = 1.0f + HP_GROWTH * (float)(g->wave > 0 ? g->wave - 1 : 0);
            e->hp = e->maxhp = EN_DEF[type].hp * scale;
            e->x = path_x[0]; e->y = path_y[0]; e->slow_f = 1.0f;
            return;
        }
}

static int enemies_alive(const Game *g)
{
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) n += g->en[i].alive;
    return n;
}

static void start_wave(Game *g)
{
    g->wave++;
    const uint8_t *w = WAVES[g->wave - 1];
    int n = 0;
    for (int t = 0; t < 4; t++)
        for (int k = 0; k < w[t] && n < 256; k++) g->spawn_q[n++] = (uint8_t)t;
    for (int i = n - 1; i > 0; i--) {                    /* shuffle */
        int j = (int)(rnd(g) % (uint32_t)(i + 1));
        uint8_t tmp = g->spawn_q[i]; g->spawn_q[i] = g->spawn_q[j]; g->spawn_q[j] = tmp;
    }
    g->spawn_n = n; g->spawn_i = 0; g->spawn_t = 0.6f; g->wave_active = 1;
    char b[40];
    if (w[3]) snprintf(b, sizeof b, "BOSS WAVE %d!", g->wave);
    else      snprintf(b, sizeof b, "WAVE %d", g->wave);
    set_banner(g, b, w[3] ? C_RED : C_YELLOW);
    sfx(g, w[3] ? SFX_BOSS : SFX_WAVE);
}

static void kill_enemy(Game *g, Enemy *e)
{
    static const uint8_t c1[EN_COUNT] = {C_LTGREEN, C_PINK, C_LIGHT, C_ORANGE};
    static const uint8_t c2[EN_COUNT] = {C_GREEN, C_PURPLE, C_STONE, C_RED};
    int gold = EN_DEF[e->type].gold;
    g->gold += gold; g->kills++;
    add_text(g, e->x, e->y - 8, gold);
    burst(g, e->x, e->y, e->type == EN_BOSS ? 44 : 12, c1[e->type], c2[e->type],
          e->type == EN_BOSS ? 95 : 60, 0.5f);
    if (e->type == EN_BOSS) {
        add_ring(g, e->x, e->y, 34, 0.5f, C_ORANGE);
        add_ring(g, e->x, e->y, 22, 0.35f, C_YELLOW);
        g->shake_t = 0.5f;
        sfx(g, SFX_EXPLODE);
    } else {
        sfx(g, SFX_KILL);
    }
    e->alive = 0;
}

static void hurt_enemy(Game *g, Enemy *e, float dmg)
{
    e->hp -= dmg;
    e->hitflash = 0.07f;
    if (e->hp <= 0) kill_enemy(g, e);
}

static void update_enemies(Game *g, float dt)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->en[i];
        if (!e->alive) continue;
        if (e->hitflash > 0) e->hitflash -= dt;
        float f = 1.0f;
        if (e->slow_t > 0) { e->slow_t -= dt; f = e->slow_f; }
        e->dist += EN_DEF[e->type].speed * f * dt;
        if (e->dist >= path_len) {
            e->alive = 0;
            if (g->state == ST_PLAY) {
                g->lives -= EN_DEF[e->type].leak;
                g->hurt_t = 0.45f;
                g->shake_t = EN_DEF[e->type].leak >= 10 ? 0.6f : 0.25f;
                burst(g, 304, e->y, 14, C_RED, C_ORANGE, 75, 0.5f);
                add_ring(g, 304, e->y, 16, 0.3f, C_RED);
                sfx(g, SFX_LEAK);
                if (g->lives <= 0) { g->lives = 0; g->state = ST_OVER; g->state_t = 0; sfx(g, SFX_OVER); }
            }
            continue;
        }
        while (e->seg < path_n - 2 && e->dist > path_cum[e->seg + 1]) e->seg++;
        float t = (e->dist - path_cum[e->seg]) / (path_cum[e->seg + 1] - path_cum[e->seg]);
        e->x = path_x[e->seg] + (path_x[e->seg + 1] - path_x[e->seg]) * t;
        e->y = path_y[e->seg] + (path_y[e->seg + 1] - path_y[e->seg]) * t;
    }
}

static void fire(Game *g, int tx, int ty, Tower *t, int ei)
{
    for (int i = 0; i < MAX_PROJ; i++)
        if (!g->pr[i].alive) {
            Proj *p = &g->pr[i];
            float x, y; tile_center(tx, ty, &x, &y);
            memset(p, 0, sizeof *p);
            p->alive = 1; p->type = t->type; p->level = t->level;
            p->x = x; p->y = y - 3;
            p->target = ei; p->tuid = g->en[ei].uid;
            p->tx = g->en[ei].x; p->ty = g->en[ei].y;
            p->dmg = TW_DMG[t->type][t->level];
            t->flash = 0.07f;
            if (t->type == TW_CANNON) {
                burst(g, p->x, p->y - 4, 6, C_LIGHT, C_GRAY, 35, 0.35f);
                sfx(g, SFX_CANNON);
            } else {
                add_part(g, p->x, p->y - 2, 0, -12, 0.15f, t->type == TW_FROST ? C_ICE : C_WHITE, 1);
                sfx(g, t->type == TW_FROST ? SFX_FROST : SFX_ARROW);
            }
            return;
        }
}

static void update_towers(Game *g, float dt)
{
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            Tower *t = &g->towers[ty][tx];
            if (!t->used) continue;
            if (t->flash > 0) t->flash -= dt;
            t->cd -= dt;
            if (t->cd > 0) continue;
            float x, y; tile_center(tx, ty, &x, &y);
            float range = TW_RANGE[t->type][t->level];
            int best = -1; float bestd = -1;
            for (int i = 0; i < MAX_ENEMIES; i++) {
                const Enemy *e = &g->en[i];
                if (!e->alive) continue;
                if (t->type == TW_CANNON && e->type == EN_BAT) continue;   /* can't hit flyers */
                float dx = e->x - x, dy = e->y - y;
                if (dx * dx + dy * dy > range * range) continue;
                if (e->dist > bestd) { bestd = e->dist; best = i; }          /* furthest along */
            }
            if (best >= 0) { fire(g, tx, ty, t, best); t->cd = TW_CD[t->type][t->level]; }
            else t->cd = 0;
        }
}

static void hit_projectile(Game *g, Proj *p)
{
    Enemy *tg = NULL;
    if (p->target >= 0 && g->en[p->target].alive && g->en[p->target].uid == p->tuid) tg = &g->en[p->target];

    if (p->type == TW_ARROW) {
        if (tg) {
            hurt_enemy(g, tg, p->dmg);
            add_part(g, p->x, p->y, -p->vx * 0.2f, -p->vy * 0.2f - 10, 0.2f, C_WHITE, 1);
            add_part(g, p->x, p->y, p->vy * 0.2f, -p->vx * 0.2f - 10, 0.2f, C_TAN, 1);
        }
    } else if (p->type == TW_CANNON) {
        float r = TW_SPLASH[TW_CANNON][p->level];
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *e = &g->en[i];
            if (!e->alive || e->type == EN_BAT) continue;
            float dx = e->x - p->x, dy = e->y - p->y;
            if (dx * dx + dy * dy <= r * r) hurt_enemy(g, e, p->dmg);
        }
        burst(g, p->x, p->y, 18, C_ORANGE, C_YELLOW, 80, 0.45f);
        burst(g, p->x, p->y, 8, C_DKSTONE, C_GRAY, 40, 0.55f);
        add_ring(g, p->x, p->y, r + 3, 0.28f, C_ORANGE);
        if (p->level == 2) g->shake_t = g->shake_t > 0.12f ? g->shake_t : 0.12f;
        sfx(g, SFX_EXPLODE);
    } else {
        float r = TW_SPLASH[TW_FROST][p->level];
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *e = &g->en[i];
            if (!e->alive) continue;
            float dx = e->x - p->x, dy = e->y - p->y;
            if (dx * dx + dy * dy <= r * r) {
                if (e->slow_t <= 0 || TW_SLOWF[p->level] < e->slow_f) e->slow_f = TW_SLOWF[p->level];
                if (TW_SLOWT[p->level] > e->slow_t) e->slow_t = TW_SLOWT[p->level];
            }
        }
        if (tg) hurt_enemy(g, tg, p->dmg);
        burst(g, p->x, p->y, 12, C_ICE, C_CYAN, 55, 0.4f);
        add_ring(g, p->x, p->y, r, 0.3f, C_ICE);
    }
    p->alive = 0;
}

static void update_projectiles(Game *g, float dt)
{
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *p = &g->pr[i];
        if (!p->alive) continue;
        if (p->target >= 0 && g->en[p->target].alive && g->en[p->target].uid == p->tuid) {
            p->tx = g->en[p->target].x; p->ty = g->en[p->target].y;
        }
        float dx = p->tx - p->x, dy = p->ty - p->y, d = sqrtf(dx * dx + dy * dy);
        float step = PROJ_SPEED[p->type] * dt;
        if (d <= step + 1.5f) {
            p->vx = dx; p->vy = dy;
            hit_projectile(g, p);
        } else {
            p->vx = dx / d * PROJ_SPEED[p->type];
            p->vy = dy / d * PROJ_SPEED[p->type];
            p->x += p->vx * dt; p->y += p->vy * dt;
            /* trails */
            if (p->type == TW_CANNON && (g->frame & 1)) add_part(g, p->x, p->y, 0, -8, 0.22f, C_GRAY, 1);
            else if (p->type == TW_FROST && (g->frame & 1)) add_part(g, p->x, p->y, 0, 0, 0.2f, (g->frame & 2) ? C_CYAN : C_ICE, 1);
        }
    }
}

static void update_fx(Game *g, float dt)
{
    for (int i = 0; i < MAX_PART; i++) {
        Part *p = &g->pt[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->x += p->vx * dt; p->y += p->vy * dt;
        p->vy += 70.0f * dt;
    }
    for (int i = 0; i < MAX_TEXT; i++) {
        FText *t = &g->ft[i];
        if (t->life <= 0) continue;
        t->life -= dt; t->y -= 18.0f * dt;
    }
    for (int i = 0; i < MAX_RING; i++) if (g->rg[i].life > 0) g->rg[i].life -= dt;
    if (g->msg_t > 0)    g->msg_t -= dt;
    if (g->banner_t > 0) g->banner_t -= dt;
    if (g->hurt_t > 0)   g->hurt_t -= dt;
    if (g->shake_t > 0)  g->shake_t -= dt;
}

static void sim_step(Game *g, float dt)
{
    if (!g->wave_active) {
        if (g->wave < NUM_WAVES) {
            g->wave_timer -= dt;
            if (g->wave_timer <= 0) start_wave(g);
        }
    } else if (g->spawn_i < g->spawn_n) {
        g->spawn_t -= dt;
        if (g->spawn_t <= 0) {
            int type = g->spawn_q[g->spawn_i++];
            spawn_enemy(g, type);
            static const float gap[EN_COUNT] = {0.75f, 0.45f, 1.3f, 2.5f};
            g->spawn_t = gap[type];
        }
    } else if (enemies_alive(g) == 0) {
        g->wave_active = 0;
        int bonus = 15 + 3 * g->wave;
        g->gold += bonus;
        char b[40];
        if (g->wave >= NUM_WAVES) {
            g->state = ST_WIN; g->state_t = 0;
            sfx(g, SFX_WIN);
        } else {
            snprintf(b, sizeof b, "WAVE CLEAR! +%dG", bonus);
            set_banner(g, b, C_LTGREEN);
            g->wave_timer = 12.0f;
            sfx(g, SFX_CLEAR);
        }
    }
    update_enemies(g, dt);
    if (g->state != ST_PLAY) return;
    update_towers(g, dt);
    update_projectiles(g, dt);
    update_fx(g, dt);
}

/* ------------------------------------------------------------------ */
/* Public: init / start / update                                       */
/* ------------------------------------------------------------------ */
void game_init(Game *g)
{
    sprites_init();
    audio_init();
    path_init();
    memset(g, 0, sizeof *g);
    g->rng = 0x1badf00du;
    g->state = ST_TITLE;
    g->cx = 6; g->cy = 4;
    build_bg(g);
}

void game_start(Game *g)
{
    uint32_t seed = g->rng ^ g->frame * 2654435761u;
    memset(g->towers, 0, sizeof g->towers);
    memset(g->en, 0, sizeof g->en);
    memset(g->pr, 0, sizeof g->pr);
    memset(g->pt, 0, sizeof g->pt);
    memset(g->ft, 0, sizeof g->ft);
    memset(g->rg, 0, sizeof g->rg);
    g->state = ST_PLAY; g->state_t = 0;
    g->lives = 20; g->gold = 120; g->wave = 0; g->kills = 0;
    g->cx = 6; g->cy = 4; g->sel = TW_ARROW; g->fast = 0;
    g->wave_active = 0; g->wave_timer = 18.0f;
    g->msg_t = 0; g->banner_t = 0; g->hurt_t = 0; g->shake_t = 0; g->fade = 1.0f;
    g->rng = seed ? seed : 0x1badf00du;
    set_banner(g, "GET READY!", C_CYAN);
    sfx(g, SFX_START);
}

static void move_cursor(Game *g, uint32_t held, uint32_t pressed)
{
    static const uint32_t bits[4] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT};
    static const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
    for (int i = 0; i < 4; i++) {
        int move = 0;
        if (pressed & bits[i]) { move = 1; g->rep[i] = 14; }
        else if (held & bits[i]) { if (--g->rep[i] <= 0) { move = 1; g->rep[i] = 4; } }
        else g->rep[i] = 0;
        if (move) {
            int ox = g->cx, oy = g->cy;
            g->cx += dx[i]; g->cy += dy[i];
            if (g->cx < 0) g->cx = 0;
            if (g->cx >= MAP_W) g->cx = MAP_W - 1;
            if (g->cy < 0) g->cy = 0;
            if (g->cy >= MAP_H) g->cy = MAP_H - 1;
            if (g->cx != ox || g->cy != oy) sfx(g, SFX_MOVE);
        }
    }
}

void game_update(Game *g, uint32_t held, uint32_t pressed, float dt)
{
    g->frame++;
    for (int i = 0; i < SFX_COUNT; i++) if (g->sfx_cd[i] > 0) g->sfx_cd[i] -= dt;
    if (g->fade > 0) g->fade -= dt * 2.2f;

    switch (g->state) {
    case ST_TITLE:
        g->title_t += dt;
        if (g->title_t > 1.1f) { g->title_t = 0; spawn_enemy(g, (int)(g->frame / 66) % 3); }
        update_enemies(g, dt);
        update_fx(g, dt);
        if (pressed & (BTN_A | BTN_PLUS)) game_start(g);
        return;
    case ST_PAUSE:
        if (pressed & (BTN_PLUS | BTN_A)) { g->state = ST_PLAY; sfx(g, SFX_PAUSE); }
        return;
    case ST_OVER:
    case ST_WIN:
        g->state_t += dt;
        update_fx(g, dt);
        if (g->state_t > 1.2f && (pressed & BTN_A)) { game_init(g); }
        return;
    case ST_PLAY:
        break;
    }

    /* --- input --- */
    move_cursor(g, held, pressed);
    if (pressed & BTN_L) { g->sel = (g->sel + TW_COUNT - 1) % TW_COUNT; sfx(g, SFX_SELECT); }
    if (pressed & BTN_R) { g->sel = (g->sel + 1) % TW_COUNT;            sfx(g, SFX_SELECT); }
    if (pressed & BTN_Y) { g->fast = !g->fast; sfx(g, SFX_SELECT); }
    if (pressed & BTN_PLUS) { g->state = ST_PAUSE; sfx(g, SFX_PAUSE); return; }
    if (pressed & BTN_A) {
        if (g->towers[g->cy][g->cx].used) game_upgrade(g, g->cx, g->cy);
        else game_build(g, g->cx, g->cy, g->sel);
    }
    if (pressed & BTN_B) {
        if (g->towers[g->cy][g->cx].used) game_sell(g, g->cx, g->cy);
    }
    if ((pressed & BTN_X) && !g->wave_active && g->wave < NUM_WAVES) {
        int bonus = (int)g->wave_timer;
        if (bonus > 0 && g->wave > 0) { g->gold += bonus; add_text(g, 160, 40, bonus); }
        g->wave_timer = 0;
    }

    int steps = g->fast ? 2 : 1;
    for (int s = 0; s < steps && g->state == ST_PLAY; s++) sim_step(g, dt);
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */
static int tower_sprite(int type) { return S_T_ARROW + type; }

static void draw_tower(uint8_t *fb, const Game *g, const Tower *t, int tx, int ty)
{
    int x = tx * TILE, y = MAP_Y + ty * TILE;
    /* ground shadow: bottom edge + right side (light comes from the top-left) */
    for (int i = 1; i < 15; i++) shadow_px(fb, x + i, y + 15);
    for (int j = 9; j < 16; j++) { shadow_px(fb, x + 14, y + j); shadow_px(fb, x + 15, y + j); }
    blit(fb, &SPR[tower_sprite(t->type)], x, y, t->level == 0 ? M_NORMAL : (t->level == 1 ? M_TIER2 : M_TIER3));
    int n = t->level + 1, start = 8 - (n * 3 - 1) / 2;
    for (int i = 0; i < n; i++) {
        rect(fb, x + start + i * 3, y + 14, 2, 2, C_YELLOW);
        px(fb, x + start + i * 3, y + 15, C_ORANGE); px(fb, x + start + i * 3 + 1, y + 15, C_ORANGE);
    }
    if (t->flash > 0) {                                  /* muzzle flash */
        int mx = x + 8, my = y + (t->type == TW_FROST ? 2 : 1);
        int c = t->type == TW_FROST ? C_ICE : (t->type == TW_CANNON ? C_ORANGE : C_YELLOW);
        px(fb, mx, my, C_WHITE); px(fb, mx - 1, my, c); px(fb, mx + 1, my, c);
        px(fb, mx, my - 1, c); px(fb, mx, my + 1, c);
    }
    if (t->level == 2) {                                 /* level-3 sparkle */
        uint32_t h = hash2(tx, ty, (int)(g->frame / 48));
        int ph = (int)(g->frame % 48);
        if (ph < 8) {
            int sx = x + 3 + (int)(h % 10), sy = y + 1 + (int)((h >> 8) % 6);
            px(fb, sx, sy, C_WHITE);
            if (ph > 1 && ph < 6) { px(fb, sx - 1, sy, C_YELLOW); px(fb, sx + 1, sy, C_YELLOW); px(fb, sx, sy - 1, C_YELLOW); px(fb, sx, sy + 1, C_YELLOW); }
        }
    }
}

static void draw_enemy(uint8_t *fb, const Game *g, const Enemy *e)
{
    int spr, bob = 0, mode = M_NORMAL;
    switch (e->type) {
    case EN_SLIME: spr = S_SLIME; bob = (((g->frame >> 3) + e->uid) & 1) ? -1 : 0; break;
    case EN_BAT:   spr = ((g->frame >> 2) & 1) ? S_BAT1 : S_BAT0; bob = (int)lrintf(sinf((float)g->frame * 0.15f + (float)e->uid) * 1.5f); break;
    case EN_GOLEM: spr = S_GOLEM; bob = (((g->frame >> 4) + e->uid) & 1) ? -1 : 0; break;
    default:       spr = S_BOSS;  bob = (((g->frame >> 3) + e->uid) & 1) ? -1 : 0; break;
    }
    if (e->slow_t > 0) mode = M_FROZEN;
    if (e->hitflash > 0) mode = M_FLASH;
    int x = (int)lrintf(e->x) - 8, y = (int)lrintf(e->y) - 8 + bob;
    if (e->type == EN_BAT) y -= 3;
    /* soft shadow (smaller and lighter for flyers, which hover above the ground) */
    int sy = (int)lrintf(e->y) + 5;
    shadow_ellipse(fb, (int)lrintf(e->x), sy + (e->type == EN_BAT ? 3 : 0), e->type == EN_BOSS ? 7 : (e->type == EN_BAT ? 3 : 5));
    blit(fb, &SPR[spr], x, y, mode);
    if (e->hp < e->maxhp) {
        int w = e->type == EN_BOSS ? 16 : 10, bx = (int)lrintf(e->x) - w / 2, by = y - 2;
        int fillw = (int)(w * e->hp / e->maxhp + 0.5f);
        if (fillw < 1) fillw = 1;
        rect(fb, bx - 1, by - 1, w + 2, 3, C_BLACK);
        rect(fb, bx, by, w, 1, C_DKRED);
        float f = e->hp / e->maxhp;
        rect(fb, bx, by, fillw, 1, f > 0.5f ? C_GREEN : (f > 0.25f ? C_YELLOW : C_RED));
    }
}

static void draw_projectile(uint8_t *fb, const Proj *p)
{
    int x = (int)lrintf(p->x), y = (int)lrintf(p->y);
    if (p->type == TW_ARROW) {
        float len = sqrtf(p->vx * p->vx + p->vy * p->vy);
        if (len < 1) len = 1;
        float ux = p->vx / len, uy = p->vy / len;
        px(fb, (int)lrintf(p->x - ux * 4), (int)lrintf(p->y - uy * 4), C_BROWN);
        px(fb, (int)lrintf(p->x - ux * 2), (int)lrintf(p->y - uy * 2), C_TAN);
        px(fb, x, y, C_WHITE);
        px(fb, (int)lrintf(p->x + ux), (int)lrintf(p->y + uy), C_LIGHT);
    } else if (p->type == TW_CANNON) {
        rect(fb, x - 1, y - 1, 3, 3, C_BLACK);
        px(fb, x - 1, y - 1, C_GRAY);
    } else {
        px(fb, x, y, C_WHITE);
        px(fb, x - 1, y, C_CYAN); px(fb, x + 1, y, C_CYAN);
        px(fb, x, y - 1, C_CYAN); px(fb, x, y + 1, C_CYAN);
    }
}

static void tower_info_text(const Game *g, char *buf, size_t n)
{
    const Tower *t = &g->towers[g->cy][g->cx];
    if (g->msg_t > 0) { snprintf(buf, n, "%s", g->msg); return; }
    if (t->used) {
        int sell = t->invested * 70 / 100;
        int dmg = (int)(TW_DMG[t->type][t->level] + 0.5f);
        if (t->level < 2)
            snprintf(buf, n, "%s L%d DMG %d  A:UPGRADE %dG  B:SELL %dG", TW_NAME[t->type], t->level + 1, dmg, TW_UP[t->type][t->level], sell);
        else
            snprintf(buf, n, "%s L3 MAX DMG %d  B:SELL %dG", TW_NAME[t->type], dmg, sell);
    } else if (!game_is_buildable(g, g->cx, g->cy)) {
        snprintf(buf, n, "CAN'T BUILD HERE");
    } else {
        snprintf(buf, n, "A:BUILD %s %dG%s  L/R:CHANGE", TW_NAME[g->sel], TW_COST[g->sel],
                 g->sel == TW_CANNON ? " (GROUND ONLY)" : "");
    }
}

static void draw_hud(uint8_t *fb, const Game *g)
{
    char b[64];
    rect(fb, 0, 0, SCREEN_W, 18, C_BLACK);
    rect(fb, 0, 18, SCREEN_W, 1, C_DKGRAY);
    rect(fb, 0, 19, SCREEN_W, 1, C_GRAY);
    /* lives (heart blinks when low) */
    if (g->lives > 5 || (g->frame & 16)) blit(fb, &SPR[S_HEART], 4, 3, M_NORMAL);
    snprintf(b, sizeof b, "%d", g->lives);
    draw_text(fb, 14, 4, b, (g->hurt_t > 0 || g->lives <= 5) ? C_RED : C_WHITE, 1);
    /* gold */
    blit(fb, &SPR[S_COIN], 38, 3, M_NORMAL);
    snprintf(b, sizeof b, "%d", g->gold);
    draw_text(fb, 47, 4, b, C_YELLOW, 1);
    /* wave */
    snprintf(b, sizeof b, "WAVE %d/%d", g->wave < 1 ? 1 : g->wave, NUM_WAVES);
    draw_text(fb, 84, 4, b, C_CYAN, 1);
    /* status */
    if (!g->wave_active && g->wave < NUM_WAVES) {
        snprintf(b, sizeof b, "NEXT WAVE %d  X:CALL NOW", (int)ceilf(g->wave_timer));
        draw_text(fb, 130, 4, b, C_LTGREEN, 1);
    } else if (g->wave_active) {
        int left = (g->spawn_n - g->spawn_i);
        for (int i = 0; i < MAX_ENEMIES; i++) left += g->en[i].alive;
        snprintf(b, sizeof b, "ENEMIES %d", left);
        draw_text(fb, 130, 4, b, C_ORANGE, 1);
    }
    if (g->fast) draw_text(fb, 232, 4, ">>", C_YELLOW, 1);
    /* context line */
    tower_info_text(g, b, sizeof b);
    draw_text(fb, 4, 12, b, g->msg_t > 0 ? C_ORANGE : C_LIGHT, 1);
    /* tower selector */
    for (int i = 0; i < TW_COUNT; i++) {
        int x = 264 + i * 18;
        blit(fb, &SPR[tower_sprite(i)], x, 2, g->gold >= TW_COST[i] ? M_NORMAL : M_DIM);
        if (i == g->sel) {
            frame_rect(fb, x - 1, 1, 18, 17, C_YELLOW);
            if (g->frame & 16) frame_rect(fb, x - 2, 0, 20, 19, C_ORANGE);
        }
    }
}

static void draw_cursor(uint8_t *fb, const Game *g)
{
    int x = g->cx * TILE, y = MAP_Y + g->cy * TILE;
    const Tower *t = &g->towers[g->cy][g->cx];
    int buildable = game_is_buildable(g, g->cx, g->cy);
    int col = t->used ? C_YELLOW : (buildable && g->gold >= TW_COST[g->sel] ? C_LTGREEN : C_RED);
    if (buildable) blit(fb, &SPR[tower_sprite(g->sel)], x, y, M_GHOST);   /* placement preview */
    int o = ((g->frame >> 4) & 1) ? 1 : 0;
    int x0 = x - o, y0 = y - o, x1 = x + TILE - 1 + o, y1 = y + TILE - 1 + o;
    for (int k = 0; k < 5; k++) {
        px(fb, x0 + k, y0, col);     px(fb, x0, y0 + k, col);
        px(fb, x1 - k, y0, col);     px(fb, x1, y0 + k, col);
        px(fb, x0 + k, y1, col);     px(fb, x0, y1 - k, col);
        px(fb, x1 - k, y1, col);     px(fb, x1, y1 - k, col);
    }
    /* range preview */
    int type = t->used ? t->type : g->sel, lvl = t->used ? t->level : 0;
    if (t->used || buildable)
        dotted_circle(fb, x + 8, y + 8, TW_RANGE[type][lvl], t->used ? C_YELLOW : C_WHITE, (int)(g->frame >> 3));
}

static void draw_keep_flag(uint8_t *fb, const Game *g)
{
    int x = WP[NWP - 1][0] * TILE, y = MAP_Y + WP[NWP - 1][1] * TILE, f = (int)((g->frame >> 4) & 1);
    rect(fb, x + 9, y, 4, 1, C_RED);
    rect(fb, x + 9 + f, y + 1, 4, 1, C_RED);
    rect(fb, x + 9, y + 2, 3, 1, C_DKRED);
    px(fb, x + 12 + f, y + 1, C_DKRED);
}

static void draw_world(uint8_t *fb, const Game *g)
{
    memcpy(fb + MAP_Y * SCREEN_W, g->bg, sizeof g->bg);
    blit_phase = (int)(g->frame >> 3);
    blit(fb, &SPR[S_PORTAL], WP[0][0] * TILE, MAP_Y + WP[0][1] * TILE, M_PULSE);
    draw_keep_flag(fb, g);
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++)
            if (g->towers[ty][tx].used) draw_tower(fb, g, &g->towers[ty][tx], tx, ty);
    for (int i = 0; i < MAX_ENEMIES; i++) if (g->en[i].alive) draw_enemy(fb, g, &g->en[i]);
    for (int i = 0; i < MAX_RING; i++) {
        const Ring *r = &g->rg[i];
        if (r->life <= 0) continue;
        float t = 1.0f - r->life / r->max_life;
        ring_outline(fb, (int)r->x, (int)r->y, r->maxr * (0.25f + 0.75f * sqrtf(t)), t > 0.6f ? DARK[r->col] : r->col);
    }
    for (int i = 0; i < MAX_PROJ; i++) if (g->pr[i].alive) draw_projectile(fb, &g->pr[i]);
    for (int i = 0; i < MAX_PART; i++) {
        const Part *p = &g->pt[i];
        if (p->life > 0) rect(fb, (int)p->x, (int)p->y, p->size, p->size, p->col);
    }
    for (int i = 0; i < MAX_TEXT; i++) {
        const FText *t = &g->ft[i];
        if (t->life <= 0) continue;
        char b[16]; snprintf(b, sizeof b, "+%d", t->val);
        draw_text_sh(fb, (int)t->x - text_w(b, 1) / 2, (int)t->y, b, C_YELLOW, 1);
    }
}

static void shake_map(uint8_t *fb, int dx, int dy)
{
    static uint8_t tmp[SCREEN_W * MAP_H * TILE];
    memcpy(tmp, fb + MAP_Y * SCREEN_W, sizeof tmp);
    for (int y = 0; y < MAP_H * TILE; y++) {
        int sy = y - dy; if (sy < 0) sy = 0; if (sy >= MAP_H * TILE) sy = MAP_H * TILE - 1;
        for (int x = 0; x < SCREEN_W; x++) {
            int sx = x - dx; if (sx < 0) sx = 0; if (sx >= SCREEN_W) sx = SCREEN_W - 1;
            fb[(MAP_Y + y) * SCREEN_W + x] = tmp[sy * SCREEN_W + sx];
        }
    }
}

static void draw_title(uint8_t *fb, const Game *g)
{
    draw_world(fb, g);
    rect(fb, 0, 0, SCREEN_W, MAP_Y, C_BLACK);

    /* logo */
    panel(fb, 28, 4, 264, 84, C_YELLOW);
    static const uint8_t cols1[5] = {C_RED, C_ORANGE, C_YELLOW, C_LTGREEN, C_CYAN};
    static const uint8_t cols2[7] = {C_BLUE, C_PURPLE, C_PINK, C_RED, C_ORANGE, C_YELLOW, C_LTGREEN};
    const char *w1 = "TOWER", *w2 = "DEFENSE";
    int x1 = (SCREEN_W - text_w(w1, 5)) / 2, x2 = (SCREEN_W - text_w(w2, 5)) / 2;
    for (int i = 0; w1[i]; i++) {
        char s[2] = {w1[i], 0};
        int yo = (int)lrintf(sinf((float)g->frame * 0.08f + (float)i * 0.7f) * 2.0f);
        draw_text_sh(fb, x1 + i * 20, 11 + yo, s, cols1[i], 5);
    }
    for (int i = 0; w2[i]; i++) {
        char s[2] = {w2[i], 0};
        int yo = (int)lrintf(sinf((float)g->frame * 0.08f + (float)(i + 5) * 0.7f) * 2.0f);
        draw_text_sh(fb, x2 + i * 20, 40 + yo, s, cols2[i], 5);
    }
    draw_text_center(fb, 74, "20 WAVES - 3 TOWERS - 1 KEEP", C_LIGHT, 1);
    /* mascots: a tower on the left, the boss on the right */
    blit_scaled(fb, &SPR[S_T_ARROW], 36, 20, 3);
    blit_scaled(fb, &SPR[S_BOSS], 236, 20 + (int)((g->frame >> 4) & 1), 3);

    panel(fb, 28, 98, 264, 80, C_GRAY);
    draw_text_center(fb, 106, "PRESS A TO START", ((g->frame >> 4) & 1) ? C_WHITE : C_YELLOW, 2);
    draw_text_center(fb, 128, "D-PAD MOVE   A BUILD/UPGRADE   B SELL", C_LIGHT, 1);
    draw_text_center(fb, 138, "L/R CHANGE TOWER   X CALL WAVE   Y FAST", C_LIGHT, 1);
    draw_text_center(fb, 148, "+ PAUSE   - SCREEN MODE   HOME EXIT", C_LIGHT, 1);
    draw_text_center(fb, 162, "CLASSIC CONTROLLER", C_CYAN, 1);
}

void game_render(const Game *g, uint8_t *fb)
{
    sprites_init();
    if (g->state == ST_TITLE) {
        draw_title(fb, g);
        if (g->fade > 0) fade_black(fb, g->fade);
        return;
    }

    draw_world(fb, g);
    if (g->state == ST_PLAY || g->state == ST_PAUSE) draw_cursor(fb, g);
    if (g->shake_t > 0) {
        int amp = g->shake_t > 0.25f ? 2 : 1;
        int dx = (int)(hash2((int)g->frame, 1, 7) % (uint32_t)(2 * amp + 1)) - amp;
        int dy = (int)(hash2((int)g->frame, 2, 9) % (uint32_t)(2 * amp + 1)) - amp;
        shake_map(fb, dx, dy);
    }
    if (g->hurt_t > 0 && (g->frame & 2)) frame_rect(fb, 0, MAP_Y, SCREEN_W, MAP_H * TILE, C_RED);
    draw_hud(fb, g);

    if (g->banner_t > 0 && g->state == ST_PLAY) {
        int y = 78 - (g->banner_t > 1.7f ? (int)((g->banner_t - 1.7f) * 40) : 0);
        darken(fb, 0, y - 5, SCREEN_W, 27);
        draw_text_center(fb, y, g->banner, g->banner_col, 3);
    }
    if (g->state == ST_PAUSE) {
        darken(fb, 0, MAP_Y, SCREEN_W, MAP_H * TILE);
        panel(fb, 80, 66, 160, 50, C_GRAY);
        draw_text_center(fb, 74, "PAUSED", C_YELLOW, 3);
        draw_text_center(fb, 98, "PRESS + TO RESUME", C_LIGHT, 1);
    } else if (g->state == ST_OVER || g->state == ST_WIN) {
        char b[48];
        int win = g->state == ST_WIN;
        darken(fb, 0, MAP_Y, SCREEN_W, MAP_H * TILE);
        panel(fb, 60, 50, 200, 84, win ? C_YELLOW : C_RED);
        draw_text_center(fb, 58, win ? "YOU WIN!" : "GAME OVER", win ? C_YELLOW : C_RED, 4);
        snprintf(b, sizeof b, "WAVE %d   KILLS %d", g->wave, g->kills);
        draw_text_center(fb, 88, b, C_WHITE, 1);
        draw_text_center(fb, 100, win ? "THE KEEP STANDS!" : "THE KEEP HAS FALLEN...", C_LIGHT, 1);
        if (g->state_t > 1.2f && ((g->frame >> 4) & 1)) draw_text_center(fb, 118, "PRESS A", C_CYAN, 1);
    }
    if (g->fade > 0) fade_black(fb, g->fade);
}

/* Sprite sheet for the test harness */
void game_debug_sheet(uint8_t *fb)
{
    sprites_init();
    memset(fb, C_GRASS1, SCREEN_W * SCREEN_H);
    for (int i = 0; i < S_COUNT; i++) {
        int x = 6 + (i % 7) * 44, y = 6 + (i / 7) * 36;
        rect(fb, x - 2, y - 2, 20, 20, C_DKGRAY);
        blit(fb, &SPR[i], x, y, M_NORMAL);
        blit(fb, &SPR[i], x + 20, y, M_FROZEN);
    }
    /* tower tiers */
    for (int t = 0; t < 3; t++)
        for (int l = 0; l < 3; l++)
            blit(fb, &SPR[S_T_ARROW + t], 10 + t * 60 + l * 18, 90, l == 0 ? M_NORMAL : (l == 1 ? M_TIER2 : M_TIER3));
}
