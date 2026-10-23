/*
 * TOWER DEFENSE - game logic and software renderer (platform independent).
 */
#include "game.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/* DARK for any palette index: keeps the glow ramp (0 base, 32 warm-lit, 64 cool-lit) */
static inline uint8_t dark_of(uint8_t c) { return (uint8_t)((c & 0xE0) | DARK[c & 31]); }

/* ------------------------------------------------------------------ */
/* Game data tables                                                    */
/* ------------------------------------------------------------------ */
static const char *const TW_NAME[TW_COUNT] = {"ARROW", "CANNON", "FROST", "STORM"};
static const int   TW_COST[TW_COUNT]      = {40, 80, 60, 110};
static const int   TW_UP[TW_COUNT][2]     = {{50, 90}, {100, 170}, {70, 120}, {120, 190}};
static const float TW_DMG[TW_COUNT][3]    = {{6, 11, 20}, {22, 40, 75}, {2, 4, 7}, {10, 18, 32}};
static const float TW_CD[TW_COUNT][3]     = {{0.45f, 0.38f, 0.30f}, {1.4f, 1.3f, 1.2f}, {0.9f, 0.8f, 0.7f}, {1.0f, 0.9f, 0.8f}};
static const float TW_RANGE[TW_COUNT][3]  = {{52, 58, 66}, {44, 48, 54}, {46, 52, 58}, {50, 56, 62}};
static const float TW_SPLASH[TW_COUNT][3] = {{0, 0, 0}, {18, 21, 25}, {14, 16, 18}, {0, 0, 0}};
static const int   STORM_CHAIN[3]         = {3, 4, 5};        /* the storm bolt jumps to this many monsters */
static const float TW_SLOWF[3]            = {0.55f, 0.45f, 0.35f};
static const float TW_SLOWT[3]            = {1.5f, 1.8f, 2.2f};
static const float PROJ_SPEED[TW_COUNT]   = {230, 140, 170, 0};

static const struct { float hp, speed; int gold, leak; } EN_DEF[EN_COUNT] = {
    {  24, 28,   4,  1},   /* slime */
    {  14, 50,   5,  1},   /* bat   (flying: cannons can't hit it) */
    { 110, 18,  12,  2},   /* golem */
    {1200, 15, 120, 10},   /* boss  */
    {  60, 24,   8,  2},   /* zombie (from wave 5) */
    { 220, 20,  16,  3},   /* pumpkin zombie (wave 10): splits into two zombies when it dies */
    {  90, 34,  14,  3},   /* witch (wave 15): flies, and heals the monsters around her */
    {  40, 62,   7,  2},   /* magical black cat (wave 20): very fast, blinks forward */
    { 160, 30,  20,  4}    /* ghost (wave 25): flies, tough */
};
static int is_flyer(int type) { return type == EN_BAT || type == EN_WITCH || type == EN_GHOST; }

/* Enemy hit points grow by this fraction of their base HP every wave. */
/* hit points also multiply by this every wave (on top of HP_GROWTH) */
#ifndef HP_COMPOUND
#define HP_COMPOUND 1.03f
#endif

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
    0.50f, 0.50f, 0.50f, 0.50f, 0.10f, 1.00f, 1.00f, /* wave boss clear start pause over win */
    0.08f, 1.50f                                     /* zap thunder */
};

/* Storm levels (2-4): same enemies, bigger numbers */
static const uint8_t WAVES2[15][4] = {
    { 8, 0, 0, 0}, {10, 0, 0, 0}, { 8, 4, 0, 0}, {10, 6, 0, 0}, { 6, 0, 3, 0},
    {10, 8, 0, 0}, { 8, 6, 4, 0}, {12,10, 0, 0}, { 8, 8, 6, 0}, {10, 6, 4, 1},
    {14,10, 0, 0}, {10,10, 8, 0}, {12,14, 6, 1}, {14,10,10, 0}, {10,12, 8, 2}
};
static const uint8_t WAVES3[15][4] = {
    {10, 0, 0, 0}, { 8, 4, 0, 0}, {10, 6, 0, 0}, { 6, 0, 3, 0}, {10, 8, 0, 0},
    { 8, 6, 4, 0}, {12,10, 0, 0}, { 8, 8, 6, 0}, {10, 6, 4, 1}, {14,10, 0, 0},
    {10,10, 8, 0}, {12,14, 6, 0}, {14,10,10, 1}, {16,12, 8, 0}, {14,14,10, 2}
};
static const uint8_t WAVES4[18][4] = {
    { 8, 4, 0, 0}, {10, 6, 0, 0}, { 6, 0, 3, 0}, {10, 8, 0, 0}, { 8, 6, 4, 0},
    {12,10, 0, 0}, { 8, 8, 6, 0}, {10, 6, 4, 1}, {14,10, 0, 0}, {10,10, 8, 0},
    {12,14, 6, 1}, {14,10,10, 0}, {10,12, 8, 1}, {16,16, 8, 0}, {12,14,12, 0},
    {16,18,10, 1}, {18,18,14, 0}, {20,20,16, 3}
};

#ifndef HPM2
#define HPM2 1.0f
#endif
#ifndef HPM3
#define HPM3 1.1f
#endif
#ifndef HPM4
#define HPM4 1.15f
#endif

/* Path waypoints are tile coordinates. The enemies walk in from off-screen left to the keep at the end. */
#define MAXWP 14
typedef struct {
    const char *name;
    int nwp;
    int8_t wp[MAXWP][2];
    int nwaves;
    const uint8_t (*waves)[4];
    float hp_mult;               /* enemy hit points are multiplied by this on top of HP_GROWTH */
    int start_gold;
} LevelDef;

static const LevelDef LEVELS[NUM_LEVELS] = {
    { "GREEN MEADOW", 8, {{0, 2}, {4, 2}, {4, 7}, {9, 7}, {9, 2}, {14, 2}, {14, 7}, {19, 7}},
      20, WAVES, 1.0f, 120 },
    { "STORMY MOOR", 10, {{0, 5}, {3, 5}, {3, 1}, {7, 1}, {7, 8}, {11, 8}, {11, 3}, {16, 3}, {16, 6}, {19, 6}},
      15, WAVES2, HPM2, 150 },
    { "HAUNTED MARSH", 12, {{0, 8}, {2, 8}, {2, 1}, {5, 1}, {5, 6}, {8, 6}, {8, 1}, {12, 1}, {12, 8}, {15, 8}, {15, 3}, {19, 3}},
      15, WAVES3, HPM3, 170 },
    { "THUNDER PEAK", 6, {{0, 1}, {16, 1}, {16, 4}, {3, 4}, {3, 8}, {19, 8}},
      18, WAVES4, HPM4, 200 },
};

static const LevelDef *LV = &LEVELS[0];       /* the level whose path/map is currently set up */
static float   path_x[MAXWP + 1], path_y[MAXWP + 1], path_cum[MAXWP + 1];
static int     path_n;
static float   path_len;
static uint8_t is_path[MAP_H][MAP_W];

/* Level 1 scenery: T tree, R rock (both block building), F flower (cosmetic).
 * The storm levels place their scenery from a hash (see storm_decor) so each has its own landscape. */
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
            if ((unsigned)xx < SCREEN_W && (unsigned)yy < SCREEN_H) fb[yy * SCREEN_W + xx] = dark_of(fb[yy * SCREEN_W + xx]);
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

/*
 * Glow: palette entries 32-63 are "warm-lit" copies of colours 0-31 and 64-95 are "cool-lit" copies, so
 * lighting a pixel is just adding 32 or 64 to its index. A dithered falloff makes a soft halo.
 * buf is a w=SCREEN_W image with h rows (the map buffer, or the screen's map rows).
 */
static void glow_buf(uint8_t *buf, int h, int cx, int cy, int r, int ramp)
{
    int y0 = cy - r, y1 = cy + r, x0 = cx - r, x1 = cx + r;
    if (y0 < 0) y0 = 0;
    if (y1 >= h) y1 = h - 1;
    if (x0 < 0) x0 = 0;
    if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
    int r2 = r * r, full = ramp * 32, dim = (ramp + 2) * 32;
    for (int y = y0; y <= y1; y++) {
        uint8_t *row = buf + y * SCREEN_W;
        int dy = y - cy;
        for (int x = x0; x <= x1; x++) {
            int dx = x - cx, d2 = dx * dx + dy * dy;
            if (d2 > r2) continue;
            uint8_t c = row[x];
            if (c >= 32) continue;
            int t = d2 * 64 / (r2 + 1), b = BAYER4[(y & 3) * 4 + (x & 3)];
            int add;
            if (t < 8) add = full;                                   /* bright core */
            else if (t < 24) add = (b < (24 - t)) ? full : dim;      /* core melts into the faint tint */
            else if (t < 40) add = dim;                              /* soft ambient tint */
            else if (b < (64 - t) * 16 / 24) add = dim;              /* feathered edge */
            else continue;
            row[x] = (uint8_t)(c + add);
        }
    }
}

/* glow straight onto the screen's map rows */
static void gl2(uint8_t *fb, int cx, int cy, int r, int ramp)
{
    glow_buf(fb + MAP_Y * SCREEN_W, MAP_H * TILE, cx, cy - MAP_Y, r, ramp);
}

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
typedef struct { int w, h; uint8_t d[1024]; } Sprite;
enum { S_SLIME, S_BAT0, S_BAT1, S_GOLEM, S_BOSS, S_T_ARROW, S_T_CANNON, S_T_FROST, S_T_STORM,
       S_TREE, S_ROCK, S_PORTAL, S_HEART, S_COIN, S_DEAD, S_GRAVE, S_LAMP, S_SKEL0, S_SKEL1, S_PUMP0, S_PUMP1, S_PUMP2,
       S_ZOMBIE, S_PUMPZ, S_WITCH, S_CAT, S_GHOST, S_COUNT };
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
static const char *const R_ZOMBIE[16] = {
    "................", ".....kkkkkk.....", "....kEeeeeEk....", "....kewkkwek....",
    "....keeeeeek....", "....kekkkkek....", ".....kkeekk.....", "..kkkkNnnNkkkk..",
    ".keekNnnnnNkeek.", ".kekkNnNNnNkkek.", "..k.kNnnnnNk.k..", ".....kNNNNk.....",
    ".....kBBkBBk....", ".....kBk.kBk....", "....kkkk.kkkk...", "................"
};
static const char *const R_PUMPZ[16] = {
    "................", "......kEk.......", ".....kkEkk......", "....koooooook...",
    "...kooyooyooyk..", "...kokkookkook..", "...kooookkooook.", "...kooykykykook.",
    "....kookkkkoook.", "..kkkkkNnnkkkk..", ".keekkNnnnNkkeek", ".kekk.NnnnN.kek.",
    ".....kBBkBBk....", ".....kBk.kBk....", "....kkkk.kkkk...", "................"
};
static const char *const R_WITCH[16] = {
    ".......kk.......", "......kPPk......", ".....kPPPPk.....", "....kPppppPk....",
    "..kkkkkkkkkkkk..", "....kmmmmmmk....", "....kmykkymk....", "....kmmmkmmk....",
    ".....kkmmkk.....", "...kPPPPPPPPk...", "..kPPpPPPPpPPk..", ".nnnnnnnnnnnnnn.",
    "..kPPkkkkkkPPk..", "...kk......kk...", "................", "................"
};
static const char *const R_CAT[16] = {
    "................", "................", "................", "..k.k...........",
    "..kkk......kk...", "..kkkk.....kkk..", "..kykyk....kk...", "..kkkkkk...kk...",
    "...kkkkkkkkkk...", "...kkkkkkkkkk...", "...kkdkkkkdkk...", "...kkkkkkkkkk...",
    "...kdk....kdk...", "...kkk....kkk...", "................", "................"
};
static const char *const R_GHOST[16] = {
    "................", ".....kkkkkk.....", "....kwwwwwwk....", "...kwwwwwwwwk...",
    "...kwkkwwkkwk...", "...kwkkwwkkwk...", "...kwwwkkwwwk...", "...kwwwkkwwwk...",
    "..kwwwwwwwwwwk..", "..kwwwwwwwwwwk..", "..kwlwwwwwwlwk..", "..kwwwwwwwwwwk..",
    "..kwk.kwwk.kwk..", "...k...kk...k...", "................", "................"
};
static const char *const R_T_ARROW[16] = {
    "................", "................", "................", "................",
    "................", "....k......k....", "....kS....Sk....", "....kSS..SSk....",
    "....kkSSSSkk....", "...kSSSSSSSSk...", "...kSgSSSSgSk...", "...kSSSeeSSSk...",
    "...kSSSeeSSSk...", "..kSTTTTTTTTSk..", "..kkTTTTTTTTkk..", "..kkkkkkkkkkkk.."
};
static const char *const R_T_CANNON[16] = {
    "................", "................", "................", "................",
    "................", "....k......k....", "....kS....Sk....", "....kSS..SSk....",
    "....kkSSSSkk....", "...kSSSSSSSSk...", "...kSgSSSSgSk...", "...kSSSooSSSk...",
    "...kSSSooSSSk...", "..kSTTTTTTTTSk..", "..kkTTTTTTTTkk..", "..kkkkkkkkkkkk.."
};
static const char *const R_T_FROST[16] = {
    "................", "................", "................", "................",
    "................", "....k......k....", "....kS....Sk....", "....kSS..SSk....",
    "....kkSSSSkk....", "...kSSSSSSSSk...", "...kSgSSSSgSk...", "...kSSSccSSSk...",
    "...kSSSccSSSk...", "..kSTTTTTTTTSk..", "..kkTTTTTTTTkk..", "..kkkkkkkkkkkk.."
};
static const char *const R_T_STORM[16] = {
    "................", "................", "................", "................",
    "................", "....k......k....", "....kS....Sk....", "....kSS..SSk....",
    "....kkSSSSkk....", "...kSSSSSSSSk...", "...kSgSSSSgSk...", "...kSSSppSSSk...",
    "...kSSSppSSSk...", "..kSTTTTTTTTSk..", "..kkTTTTTTTTkk..", "..kkkkkkkkkkkk.."
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
static const char *const R_DEAD[16] = {
    "................", "...k.......k....", "...kNk....kNk...", "....kNk..kNk....",
    "..k..kNkkNk..k..", "..kNk.kNNk.kNk..", "...kNkkNNkkNk...", "....kNkNNkNk....",
    ".....kkNNNNk....", "......kNNNk.....", "......kNnNk.....", "......kNnNk.....",
    ".....kNNnNNk....", "....kNNnnNNNk...", "....kkkkkkkkk...", "................"
};
static const char *const R_GRAVE[16] = {
    "................", "................", ".....kkkkkk.....", "....kSSSSSSk....",
    "...kSSSkkSSSk...", "...kSSSkkSSSk...", "...kSkkkkkkSk...", "...kSSSkkSSSk...",
    "...kSSSkkSSSk...", "...kSSSkkSSTk...", "...kSSSSSSSTk...", "..kkkkkkkkkkkk..",
    "..kNNNNNNNNNNk..", "...kkkkkkkkkk...", "................", "................"
};
static const char *const R_LAMP[16] = {
    "................", ".....kkkkkk.....", "....kkyyyykk....", "....kywwwwyk....",
    "....kyywwyyk....", "....kkyyyykk....", ".....kkkkkk.....", "......kddk......",
    "......kddk......", "......kddk......", "......kddk......", "......kddk......",
    ".....kddddk.....", ".....kkkkkk.....", "................", "................"
};
static const char *const R_SKEL[16] = {
    "................", ".....kkkkkk.....", "....kwwwwwwk....", "....kwkwwkwk....",
    "....kwkwwkwk....", ".....kwwwwk.....", ".....kwkwkk.....", "......kkkk......",
    "...kk.kllk.kk...", "...kwkkllkkwk...", "...kwkllllkwk...", "...kwkkllkkwk...",
    "....k.kllk.k....", ".....kkllkk.....", ".....kl.kl......", "....kkk.kkk....."
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

/* jack-o'-lantern: three flicker frames differ only in the colour of the carved face */
static void make_pumpkin(Sprite *s, int face)
{
    static const uint8_t faces[3] = {C_YELLOW, C_ORANGE, C_WHITE};
    int fc = faces[face];
    s->w = 16; s->h = 16;
    memset(s->d, 255, sizeof s->d);
    for (int y = 3; y < 15; y++)
        for (int x = 1; x < 15; x++) {
            float dx = ((float)x - 7.5f) / 6.9f, dy = ((float)y - 9.0f) / 5.7f;
            if (dx * dx + dy * dy <= 1.0f) {
                int c = C_ORANGE;
                if (x == 4 || x == 11 || x == 7 || x == 8) c = C_BROWN;          /* ribs */
                if (dx > 0.45f && dy > -0.2f) c = (c == C_BROWN) ? C_DKBROWN : C_BROWN;   /* shaded side */
                s->d[y * 16 + x] = (uint8_t)c;
            }
        }
    for (int y = 3; y < 15; y++)                                   /* outline */
        for (int x = 1; x < 15; x++) {
            if (s->d[y * 16 + x] == 255) continue;
            int edge = x == 1 || x == 14 || y == 3 || y == 14 ||
                       s->d[y * 16 + x - 1] == 255 || s->d[y * 16 + x + 1] == 255 ||
                       s->d[(y - 1) * 16 + x] == 255 || s->d[(y + 1) * 16 + x] == 255;
            if (edge) s->d[y * 16 + x] = C_BLACK;
        }
    static const int8_t face_px[][2] = {                           /* eyes, nose, zig-zag grin */
        {4, 8}, {5, 8}, {6, 8}, {5, 7}, {9, 8}, {10, 8}, {11, 8}, {10, 7}, {7, 9}, {8, 9},
        {4, 11}, {5, 12}, {6, 11}, {7, 12}, {8, 12}, {9, 11}, {10, 12}, {11, 11}
    };
    for (unsigned i = 0; i < sizeof face_px / sizeof face_px[0]; i++) s->d[face_px[i][1] * 16 + face_px[i][0]] = (uint8_t)fc;
    s->d[5 * 16 + 4] = C_YELLOW; s->d[5 * 16 + 5] = C_YELLOW;     /* highlight */
    s->d[1 * 16 + 7] = C_DKBROWN; s->d[1 * 16 + 8] = C_DKBROWN;   /* stem + leaf */
    s->d[2 * 16 + 7] = C_DKBROWN; s->d[2 * 16 + 8] = C_DKBROWN; s->d[2 * 16 + 9] = C_GREEN;
    s->d[3 * 16 + 7] = C_BLACK; s->d[3 * 16 + 8] = C_BLACK;
    s->d[2 * 16 + 10] = C_GREEN;
}

/* the boss: a giant ghost king, 32x32 (twice the size of the other monsters), drawn procedurally */
static void make_boss(Sprite *s)
{
    uint8_t m[32 * 32];
    memset(m, 0, sizeof m);
    s->w = 32; s->h = 32;
    memset(s->d, 255, sizeof s->d);
    for (int y = 4; y < 31; y++)                                   /* body: a round head widening into a ragged robe */
        for (int x = 0; x < 32; x++) {
            float dx = (float)x + 0.5f - 16.0f, hw;
            if (y < 14) { float t = (14.0f - (float)y) / 10.0f; hw = 11.0f * sqrtf(1.0f - t * t); }
            else hw = 11.0f + (float)(y - 14) * 0.25f;
            int bottom = 26 + (((x >> 2) & 1) ? 4 : 0) - (((x >> 2) % 3) == 2 ? 1 : 0);
            if (fabsf(dx) <= hw && y <= bottom) m[y * 32 + x] = 1;
        }
    for (int side = 0; side < 2; side++)                           /* long reaching arms with claws */
        for (int y = 14; y < 26; y++)
            for (int i = 0; i < 9; i++) {
                int x = side ? 31 - i : i;
                int top = 14 + i / 2 + (i < 3 ? 2 : 0), bot = 17 + i / 2 + (i < 3 ? 4 : 0);
                if (y >= top && y <= bot) m[y * 32 + x] = 1;
                if (i < 3 && (y == 24 || y == 25) && (i & 1) == 0) m[y * 32 + x] = 1;      /* claws */
            }
    for (int y = 1; y < 7; y++)                                    /* crown */
        for (int x = 9; x < 23; x++) {
            int band = y >= 5;
            int spike = (x == 9 || x == 10 || x == 15 || x == 16 || x == 21 || x == 22) && y >= 2 + ((x == 15 || x == 16) ? 0 : 1);
            if (band || spike) m[y * 32 + x] = 2;
        }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            if (!m[y * 32 + x]) continue;
            int edge = x == 0 || x == 31 || y == 0 || y == 31 || !m[y * 32 + x - 1] || !m[y * 32 + x + 1] || !m[(y - 1) * 32 + x] || !m[(y + 1) * 32 + x];
            int c;
            if (m[y * 32 + x] == 2) c = edge ? C_BLACK : ((y >= 5 && ((x + y) % 5 == 0)) ? C_RED : (y >= 5 ? C_ORANGE : C_YELLOW));
            else if (edge) c = C_BLACK;
            else {
                c = C_WHITE;
                if (x >= 20 || y >= 25) c = C_LIGHT;
                if (x >= 26 || y >= 29) c = C_GRAY;
            }
            s->d[y * 32 + x] = (uint8_t)c;
        }
    for (int e = 0; e < 2; e++) {                                  /* hollow eyes with burning red pupils */
        int cx = e ? 21 : 11;
        for (int y = 9; y < 17; y++)
            for (int x = cx - 3; x <= cx + 3; x++) {
                float ex = ((float)x - (float)cx) / 3.4f, ey = ((float)y - 12.5f) / 4.2f;
                if (ex * ex + ey * ey <= 1.0f) s->d[y * 32 + x] = C_BLACK;
            }
        for (int y = 12; y < 15; y++) for (int x = cx - 1; x <= cx + 1; x++) s->d[y * 32 + x] = (uint8_t)((x == cx && y == 13) ? C_YELLOW : C_RED);
    }
    for (int y = 19; y < 26; y++)                                  /* a wide, jagged mouth */
        for (int x = 11; x < 21; x++) {
            int inset = (y == 19 || y == 25) ? 2 : ((y == 20 || y == 24) ? 1 : 0);
            if (x >= 11 + inset && x < 21 - inset) s->d[y * 32 + x] = C_BLACK;
        }
    for (int x = 12; x < 20; x += 2) { s->d[20 * 32 + x] = C_WHITE; s->d[24 * 32 + x + 1] = C_WHITE; }
}

static void sprites_init(void)
{
    if (sprites_ready) return;
    trig_init();
    make_sprite(&SPR[S_SLIME],    R_SLIME,    16, 16);
    make_sprite(&SPR[S_BAT0],     R_BAT0,     16, 16);
    make_sprite(&SPR[S_BAT1],     R_BAT1,     16, 16);
    make_sprite(&SPR[S_GOLEM],    R_GOLEM,    16, 16);
    make_boss(&SPR[S_BOSS]);
    make_sprite(&SPR[S_ZOMBIE],   R_ZOMBIE,   16, 16);
    make_sprite(&SPR[S_PUMPZ],    R_PUMPZ,    16, 16);
    make_sprite(&SPR[S_WITCH],    R_WITCH,    16, 16);
    make_sprite(&SPR[S_CAT],      R_CAT,      16, 16);
    make_sprite(&SPR[S_GHOST],    R_GHOST,    16, 16);
    make_sprite(&SPR[S_T_ARROW],  R_T_ARROW,  16, 16);
    make_sprite(&SPR[S_T_CANNON], R_T_CANNON, 16, 16);
    make_sprite(&SPR[S_T_FROST],  R_T_FROST,  16, 16);
    make_sprite(&SPR[S_T_STORM],  R_T_STORM,  16, 16);
    make_sprite(&SPR[S_TREE],     R_TREE,     16, 16);
    make_sprite(&SPR[S_ROCK],     R_ROCK,     16, 16);
    make_sprite(&SPR[S_PORTAL],   R_PORTAL,   16, 16);
    make_sprite(&SPR[S_DEAD],     R_DEAD,     16, 16);
    make_sprite(&SPR[S_GRAVE],    R_GRAVE,    16, 16);
    make_sprite(&SPR[S_LAMP],     R_LAMP,     16, 16);
    make_sprite(&SPR[S_SKEL0],    R_SKEL,     16, 16);
    SPR[S_SKEL1] = SPR[S_SKEL0];                                   /* second frame: head and shoulders lean 1px */
    for (int y = 1; y < 8; y++)
        for (int x = 15; x > 0; x--) SPR[S_SKEL1].d[y * 16 + x] = SPR[S_SKEL0].d[y * 16 + x - 1];
    for (int y = 1; y < 8; y++) SPR[S_SKEL1].d[y * 16] = 255;
    for (int i = 0; i < 3; i++) make_pumpkin(&SPR[S_PUMP0 + i], i);
    make_sprite(&SPR[S_HEART],    R_HEART,     7,  6);
    make_sprite(&SPR[S_COIN],     R_COIN,      6,  6);
    sprites_ready = 1;
}

/* blit modes: tier 2 = blue trim, tier 3 = gold trim (towers); ghost = see-through placement preview */
enum { M_NORMAL, M_FLASH, M_FROZEN, M_DIM, M_TIER2, M_TIER3, M_GHOST, M_PULSE };
static int blit_phase;
static int blit_add;                         /* ambient light: palette offset added to every sprite pixel (0, 32, 64, 96, 128) */

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
            if (c != 255) d[i] = (uint8_t)(map[c & 31] + (map[c & 31] < 32 && mode != M_FLASH ? blit_add : 0));
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

static void path_init(int level)
{
    LV = &LEVELS[level];
    memset(is_path, 0, sizeof is_path);
    path_n = 0;
    path_x[0] = -8.0f;
    path_y[0] = (float)(MAP_Y + LV->wp[0][1] * TILE + 8);
    path_n = 1;
    for (int i = 1; i < LV->nwp; i++) {
        path_x[path_n] = (float)(LV->wp[i][0] * TILE + 8);
        path_y[path_n] = (float)(MAP_Y + LV->wp[i][1] * TILE + 8);
        path_n++;
    }
    for (int i = 0; i < LV->nwp - 1; i++) {
        int x = LV->wp[i][0], y = LV->wp[i][1], x1 = LV->wp[i + 1][0], y1 = LV->wp[i + 1][1];
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

/* scenery codes (the numbers 1-3 are level 1's: tree, rock, flower) */
enum { D_NONE, D_TREE, D_ROCK, D_FLORA, D_DEAD, D_GRAVE, D_LAMP, D_PUMPKIN, D_SKEL, D_BONES };
static uint8_t decor_map[MAP_H][MAP_W];
static uint8_t decor_ramp[MAP_H][MAP_W];     /* 0/1/2: is the ground under this tile lit (warm/cool) by a glow? */
/* grass sample points for the wind shimmer */
static uint16_t wind_pt[2 * MAP_W * MAP_H][2];
static int      wind_n;

static int decor_at(int tx, int ty) { return decor_map[ty][tx]; }
static int decor_blocks(int d) { return d != D_NONE && d != D_FLORA && d != D_BONES; }

static void decor_init(int level)
{
    memset(decor_map, 0, sizeof decor_map);
    memset(decor_ramp, 0, sizeof decor_ramp);
    if (level == 0) {
        for (int ty = 0; ty < MAP_H; ty++)
            for (int tx = 0; tx < MAP_W; tx++)
                switch (DECOR[ty][tx]) {
                case 'T': decor_map[ty][tx] = D_TREE; break;
                case 'R': decor_map[ty][tx] = D_ROCK; break;
                case 'F': decor_map[ty][tx] = D_FLORA; break;
                default: break;
                }
        return;
    }
    /* storm levels: a hash scatters the scenery, thinner next to the road so there is room to build */
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            if (is_path[ty][tx]) continue;
            int near = (tx > 0 && is_path[ty][tx - 1]) || (tx < MAP_W - 1 && is_path[ty][tx + 1]) ||
                       (ty > 0 && is_path[ty - 1][tx]) || (ty < MAP_H - 1 && is_path[ty + 1][tx]);
            int h = (int)(hash2(tx, ty, level * 41 + 7) % 100);
            int pick = (int)(hash2(tx, ty, level * 29 + 5) % 100);
            int d = D_NONE;
            int lit = (int)(hash2(tx, ty, level * 13 + 3) % 100);
            if (near && lit < 5) d = D_LAMP;
            else if (h < (near ? 5 : 16)) {
                /* halloween mix: dead trees, rocks, gravestones, jack-o'-lanterns and skeletons */
                static const uint8_t cut[3][4] = {      /* cumulative %: dead, rock, grave, pumpkin (rest: skeleton) */
                    {25, 38, 55, 82}, {18, 28, 48, 78}, {15, 40, 52, 80}
                };
                const uint8_t *c = cut[level - 1];
                d = pick < c[0] ? D_DEAD : pick < c[1] ? D_ROCK : pick < c[2] ? D_GRAVE : pick < c[3] ? D_PUMPKIN : D_SKEL;
            } else if (h < (near ? 12 : 28)) d = (pick < 35) ? D_BONES : D_FLORA;
            decor_map[ty][tx] = (uint8_t)d;
        }
}

/* grass sample points for the wind shimmer (tiles with nothing standing on them) */
static void wind_init(void)
{
    wind_n = 0;
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            if (is_path[ty][tx] || decor_blocks(decor_map[ty][tx])) continue;
            for (int k = 0; k < 2; k++) {
                uint32_t h = hash2(tx, ty, 900 + k);
                wind_pt[wind_n][0] = (uint16_t)(tx * TILE + 1 + (int)(h % 14));
                wind_pt[wind_n][1] = (uint16_t)(ty * TILE + 1 + (int)((h >> 8) % 14));
                wind_n++;
            }
        }
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

/* the haunted house at the exit (drawn procedurally; its windows flicker at runtime) */
static void bg_keep(uint8_t *bg, int storm)
{
    int x = LV->wp[LV->nwp - 1][0] * TILE, y = LV->wp[LV->nwp - 1][1] * TILE;
    if (storm != 2) {                                    /* 2 = just the house, drawn over the finished (lit) ground */
        for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) bgpx(bg, x + i, y + j, C_DIRT1);
        for (int i = 2; i < 14; i++) bgpx(bg, x + i, y + 15, C_DKBROWN);                 /* ground shadow */
    }
    for (int j = 7; j < 15; j++) for (int i = 2; i < 14; i++) bgpx(bg, x + i, y + j, C_STONE);      /* walls */
    for (int j = 7; j < 15; j++) { bgpx(bg, x + 2, y + j, C_BLACK); bgpx(bg, x + 13, y + j, C_BLACK); }
    for (int j = 8; j < 15; j++) { bgpx(bg, x + 12, y + j, C_DKSTONE); bgpx(bg, x + 11, y + j, C_DKSTONE); }   /* shaded side */
    for (int i = 2; i < 14; i++) bgpx(bg, x + i, y + 14, C_BLACK);
    for (int k = 0; k < 6; k++) bgpx(bg, x + 3 + k * 2, y + 12 + (k & 1), C_DKSTONE);   /* cracks */
    for (int j = 11; j < 14; j++) bgpx(bg, x + 4, y + j, C_STONE);
    /* chimney */
    for (int j = 1; j < 5; j++) for (int i = 11; i < 13; i++) bgpx(bg, x + i, y + j, C_STONE);
    for (int j = 1; j < 5; j++) { bgpx(bg, x + 10, y + j, C_BLACK); bgpx(bg, x + 13, y + j, C_BLACK); }
    bgpx(bg, x + 11, y + 0, C_BLACK); bgpx(bg, x + 12, y + 0, C_BLACK);
    /* roof: a steep, dark, slightly crooked peak */
    for (int j = 0; j < 7; j++) {
        int hw = 1 + j, l = 8 - hw, r = 7 + hw;
        for (int i = l; i <= r; i++) bgpx(bg, x + i, y + j, (i > 8 + j / 2) ? C_BLACK : C_DKPURPLE);
        bgpx(bg, x + l, y + j, C_BLACK); bgpx(bg, x + r, y + j, C_BLACK);
        if (j == 0) { bgpx(bg, x + 7, y, C_BLACK); bgpx(bg, x + 8, y, C_BLACK); }
    }
    for (int i = 0; i < 16; i++) bgpx(bg, x + i, y + 7, C_BLACK);                    /* eaves */
    for (int j = 1; j < 7; j += 2) for (int i = 8 - j; i < 8 + j; i += 2) bgpx(bg, x + i, y + j + 1, C_DKPURPLE);   /* shingles */
    /* windows (dark here; they flicker live), a boarded door and a rotten shutter */
    for (int j = 9; j < 11; j++) for (int i = 4; i < 6; i++) bgpx(bg, x + i, y + j, C_DKBLUE);
    for (int j = 9; j < 11; j++) for (int i = 10; i < 12; i++) bgpx(bg, x + i, y + j, C_DKBLUE);
    for (int j = 11; j < 15; j++) for (int i = 6; i < 10; i++) bgpx(bg, x + i, y + j, C_DKBROWN);
    for (int i = 6; i < 10; i++) bgpx(bg, x + i, y + 11, C_BLACK);
    bgpx(bg, x + 6, y + 12, C_BLACK); bgpx(bg, x + 9, y + 12, C_BLACK);
    bgpx(bg, x + 8, y + 13, C_YELLOW);                                               /* doorknob */
    bgpx(bg, x + 3, y + 9, C_BLACK); bgpx(bg, x + 3, y + 10, C_BLACK);
    bgpx(bg, x + 12, y + 9, C_BLACK); bgpx(bg, x + 12, y + 10, C_BLACK);
}

/* ---- storm levels: dark, wet ground with glowing scenery ---- */
static void bg_puddle(uint8_t *bg, int cx, int cy)
{
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -4; dx <= 4; dx++)
            if (dx * dx * 4 + dy * dy * 16 <= 64) bgpx(bg, cx + dx, cy + dy, C_DKBLUE);
    bgpx(bg, cx - 2, cy - 1, C_BLUE); bgpx(bg, cx - 1, cy - 1, C_BLUE);
    bgpx(bg, cx + 2, cy, C_SKY);
}

/* a skull and crossed bones lying in the grass */
static void bg_bones(uint8_t *bg, int x0, int y0, uint32_t h)
{
    int sh = (int)(h % 3) - 1;
    for (int k = 0; k < 6; k++) {                                       /* two crossed bones */
        bgpx(bg, x0 + 2 + k + sh, y0 + 11 + k / 3, C_LIGHT); bgpx(bg, x0 + 13 - k + sh, y0 + 11 + k / 3, C_LIGHT);
    }
    bgpx(bg, x0 + 1 + sh, y0 + 10, C_WHITE); bgpx(bg, x0 + 1 + sh, y0 + 12, C_WHITE);
    bgpx(bg, x0 + 14 + sh, y0 + 10, C_WHITE); bgpx(bg, x0 + 14 + sh, y0 + 12, C_WHITE);
    for (int i = 5; i <= 10; i++) { bgpx(bg, x0 + i + sh, y0 + 6, C_LIGHT); bgpx(bg, x0 + i + sh, y0 + 7, C_WHITE); bgpx(bg, x0 + i + sh, y0 + 8, C_WHITE); }
    for (int i = 6; i <= 9; i++) bgpx(bg, x0 + i + sh, y0 + 9, C_LIGHT);
    bgpx(bg, x0 + 6 + sh, y0 + 7, C_BLACK); bgpx(bg, x0 + 9 + sh, y0 + 7, C_BLACK);   /* eye sockets */
    bgpx(bg, x0 + 6 + sh, y0 + 8, C_BLACK); bgpx(bg, x0 + 9 + sh, y0 + 8, C_BLACK);
    bgpx(bg, x0 + 7 + sh, y0 + 9, C_BLACK); bgpx(bg, x0 + 8 + sh, y0 + 9, C_GRAY);
}

/* glowing plants: blue flowers (moor), toadstools (marsh), crystals (peak) */
static void bg_flora(uint8_t *bg, int level, int x0, int y0, int tx, int ty)
{
    for (int k = 0; k < 3; k++) {
        uint32_t h = hash2(tx, ty, 300 + k);
        int ox = 3 + (int)(h % 10), oy = 4 + (int)((h >> 8) % 8);
        if (level == 1) {                                   /* flower */
            bgpx(bg, x0 + ox, y0 + oy + 1, C_DKGREEN); bgpx(bg, x0 + ox, y0 + oy + 2, C_DKGREEN);
            bgpx(bg, x0 + ox, y0 + oy - 1, C_CYAN); bgpx(bg, x0 + ox - 1, y0 + oy, C_CYAN);
            bgpx(bg, x0 + ox + 1, y0 + oy, C_CYAN); bgpx(bg, x0 + ox, y0 + oy, C_WHITE);
        } else if (level == 2) {                            /* toadstool */
            for (int i = -2; i <= 2; i++) bgpx(bg, x0 + ox + i, y0 + oy, C_LTGREEN);
            for (int i = -1; i <= 1; i++) bgpx(bg, x0 + ox + i, y0 + oy - 1, C_LTGREEN);
            bgpx(bg, x0 + ox - 1, y0 + oy, C_WHITE); bgpx(bg, x0 + ox + 1, y0 + oy - 1, C_WHITE);
            bgpx(bg, x0 + ox, y0 + oy + 1, C_CREAM); bgpx(bg, x0 + ox, y0 + oy + 2, C_CREAM);
        } else {                                            /* crystal */
            for (int j = 0; j < 5; j++) bgpx(bg, x0 + ox, y0 + oy - 2 + j, C_PINK);
            for (int j = 1; j < 4; j++) { bgpx(bg, x0 + ox - 1, y0 + oy - 2 + j, C_PURPLE); bgpx(bg, x0 + ox + 1, y0 + oy - 2 + j, C_PURPLE); }
            bgpx(bg, x0 + ox, y0 + oy - 2, C_WHITE);
        }
    }
}

static void build_bg_storm(Game *g)
{
    uint8_t *bg = g->bg;
    int lv = g->level, sd = lv * 1000;
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            int x0 = tx * TILE, y0 = ty * TILE;
            int dc = decor_at(tx, ty);
            if (is_path[ty][tx]) {
                for (int j = 0; j < TILE; j++) for (int i = 0; i < TILE; i++) bgpx(bg, x0 + i, y0 + j, C_DIRT1);
                for (int k = 0; k < 10; k++) {
                    uint32_t h = hash2(tx, ty, sd + k);
                    bgpx(bg, x0 + 2 + (int)(h % 12), y0 + 2 + (int)((h >> 8) % 12), C_DIRT2);
                }
                for (int k = 0; k < 3; k++) {                       /* wet cobbles */
                    uint32_t h = hash2(tx, ty, sd + 50 + k);
                    int ox = 2 + (int)(h % 11), oy = 2 + (int)((h >> 8) % 11);
                    bgpx(bg, x0 + ox, y0 + oy, C_TAN); bgpx(bg, x0 + ox + 1, y0 + oy, C_TAN);
                    bgpx(bg, x0 + ox, y0 + oy + 1, C_DIRT2); bgpx(bg, x0 + ox + 1, y0 + oy + 1, C_DIRT2);
                }
                int l = tx > 0 && !is_path[ty][tx - 1], r = tx < MAP_W - 1 && !is_path[ty][tx + 1];
                int u = ty > 0 && !is_path[ty - 1][tx], d = ty < MAP_H - 1 && !is_path[ty + 1][tx];
                for (int k = 0; k < TILE; k++) {
                    if (l) { bgpx(bg, x0, y0 + k, C_DKBROWN);          bgpx(bg, x0 + 1, y0 + k, C_DIRT2); }
                    if (r) { bgpx(bg, x0 + 15, y0 + k, C_DKBROWN);     bgpx(bg, x0 + 14, y0 + k, C_DIRT2); }
                    if (u) { bgpx(bg, x0 + k, y0, C_DKBROWN);          bgpx(bg, x0 + k, y0 + 1, C_DIRT2); }
                    if (d) { bgpx(bg, x0 + k, y0 + 15, C_DKBROWN);     bgpx(bg, x0 + k, y0 + 14, C_DIRT2); }
                }
                if (l && u) bgpx(bg, x0 + 1, y0 + 1, C_GRASS1);
                if (r && u) bgpx(bg, x0 + 14, y0 + 1, C_GRASS1);
                if (l && d) bgpx(bg, x0 + 1, y0 + 14, C_GRASS1);
                if (r && d) bgpx(bg, x0 + 14, y0 + 14, C_GRASS1);
                uint32_t hp = hash2(tx, ty, sd + 90);
                if (hp % 100 < 30) bg_puddle(bg, x0 + 5 + (int)((hp >> 8) % 7), y0 + 6 + (int)((hp >> 16) % 5));
            } else {
                for (int j = 0; j < TILE; j++) for (int i = 0; i < TILE; i++) bgpx(bg, x0 + i, y0 + j, C_GRASS1);
                for (int k = 0; k < 6; k++) {
                    uint32_t h = hash2(tx, ty, sd + 100 + k);
                    int ox = 1 + (int)(h % 12), oy = 1 + (int)((h >> 8) % 13);
                    bgpx(bg, x0 + ox, y0 + oy + 1, C_GRASS2); bgpx(bg, x0 + ox + 1, y0 + oy, C_GRASS2);
                    bgpx(bg, x0 + ox + 2, y0 + oy + 1, C_GRASS2);
                }
                for (int k = 0; k < 3; k++) {
                    uint32_t h = hash2(tx, ty, sd + 200 + k);
                    bgpx(bg, x0 + (int)(h % 16), y0 + (int)((h >> 8) % 16), C_DKGREEN);
                }
                uint32_t hg = hash2(tx, ty, sd + 250);
                if (hg % 100 < 25) bgpx(bg, x0 + (int)(hg >> 8) % 16, y0 + (int)(hg >> 16) % 16, C_SKY);   /* wet gleam */
                if (dc == D_NONE && (hash2(tx, ty, sd + 91) % 100) < 7)
                    bg_puddle(bg, x0 + 5 + (int)(hash2(tx, ty, sd + 92) % 7), y0 + 7 + (int)(hash2(tx, ty, sd + 93) % 4));
                switch (dc) {
                case D_DEAD:
                    for (int i = -5; i <= 5; i++) { bgpx(bg, x0 + 8 + i, y0 + 14, C_GRASS2); bgpx(bg, x0 + 8 + i, y0 + 15, C_GRASS2); }
                    break;                                              /* drawn live (it sways) */
                case D_PUMPKIN: case D_SKEL:
                    for (int i = -5; i <= 5; i++) bgpx(bg, x0 + 8 + i, y0 + 15, C_GRASS2);
                    break;                                              /* drawn live (flicker / idle) */
                case D_BONES:
                    bg_bones(bg, x0, y0, hash2(tx, ty, 700)); break;
                case D_GRAVE:
                    for (int i = -6; i <= 6; i++) bgpx(bg, x0 + 8 + i, y0 + 14, C_GRASS2);
                    bg_blit(bg, &SPR[S_GRAVE], x0, y0); break;
                case D_ROCK:
                    for (int i = -6; i <= 6; i++) bgpx(bg, x0 + 8 + i, y0 + 14, C_GRASS2);
                    bg_blit(bg, &SPR[S_ROCK], x0, y0); break;
                case D_LAMP:
                    for (int i = -4; i <= 4; i++) bgpx(bg, x0 + 8 + i, y0 + 15, C_GRASS2);
                    break;                                              /* drawn live (flame flickers) */
                case D_FLORA:
                    bg_flora(bg, lv, x0, y0, tx, ty); break;
                default: break;
                }
            }
        }

    bg_blit(bg, &SPR[S_PORTAL], LV->wp[0][0] * TILE, LV->wp[0][1] * TILE);
    bg_keep(bg, 1);

    /* light sources are baked in: lamps and the keep glow warm, plants and the portal glow cool */
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            int dc = decor_at(tx, ty);
            if (dc == D_LAMP)  glow_buf(bg, MAP_H * TILE, tx * TILE + 8, ty * TILE + 6, 16, 1);
            if (dc == D_PUMPKIN) glow_buf(bg, MAP_H * TILE, tx * TILE + 8, ty * TILE + 9, 11, 1);
            if (dc == D_FLORA && !is_path[ty][tx]) glow_buf(bg, MAP_H * TILE, tx * TILE + 8, ty * TILE + 9, 9, 2);
        }
    glow_buf(bg, MAP_H * TILE, LV->wp[LV->nwp - 1][0] * TILE + 8, LV->wp[LV->nwp - 1][1] * TILE + 8, 22, 1);
    glow_buf(bg, MAP_H * TILE, LV->wp[0][0] * TILE + 8, LV->wp[0][1] * TILE + 8, 20, 2);

    bg_keep(bg, 2);                                      /* the house keeps its own colours inside the glow */

    /* remember whether each live-drawn scenery tile sits in a glow, so its sprite can be lit the same way */
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            int v = bg[(ty * TILE + 12) * SCREEN_W + tx * TILE + 8];
            decor_ramp[ty][tx] = (uint8_t)(v >> 5);          /* 0 unlit, 1 warm, 2 cool, 3 faint warm, 4 faint cool */
        }

    /* vignette: the edges of the map fall into darkness */
    for (int y = 0; y < MAP_H * TILE; y++)
        for (int x = 0; x < SCREEN_W; x++) {
            float nx = (float)(x - 160) / 160.0f, ny = (float)(y - 80) / 80.0f;
            float v = nx * nx * 0.8f + ny * ny * 0.9f - 0.55f;
            if (v <= 0) continue;
            int th = (int)(v * 20.0f);
            uint8_t *p = &bg[y * SCREEN_W + x];
            if (BAYER4[(y & 3) * 4 + (x & 3)] < th) *p = dark_of(*p);
            if (v > 0.5f && BAYER4[((y + 2) & 3) * 4 + ((x + 1) & 3)] < (int)((v - 0.5f) * 20.0f)) *p = dark_of(*p);
        }
}

static void build_bg(Game *g)
{
    if (g->level > 0) { build_bg_storm(g); return; }
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
                    /* the tree itself is drawn live (it sways) */
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
    bg_blit(bg, &SPR[S_PORTAL], LV->wp[0][0] * TILE, LV->wp[0][1] * TILE);

    bg_keep(bg, 0);

    /* gentle vignette so the corners sit back and the middle of the map glows */
    for (int y = 0; y < MAP_H * TILE; y++)
        for (int x = 0; x < SCREEN_W; x++) {
            float nx = (float)(x - 160) / 160.0f, ny = (float)(y - 80) / 80.0f;
            float v = nx * nx * 0.7f + ny * ny * 0.8f - 0.75f;
            if (v <= 0) continue;
            uint8_t *p = &bg[y * SCREEN_W + x];
            if (BAYER4[(y & 3) * 4 + (x & 3)] < (int)(v * 16.0f)) *p = dark_of(*p);
        }
}

/* ------------------------------------------------------------------ */
/* Palettes: one per level, plus lit (glow) copies and lightning flashes */
/* ------------------------------------------------------------------ */
typedef struct { uint8_t g1[3], g2[3], dg[3], d1[3], d2[3], tn[3], db[3], warm[3], cool[3], flash[3]; } Theme;
static const Theme THEME[NUM_LEVELS] = {
    { {0}, {0}, {0}, {0}, {0}, {0}, {0}, {255, 170, 70}, {110, 190, 255}, {255, 255, 255} },
    /* stormy moor: slate blue */
    { {30, 48, 62}, {22, 36, 50}, {13, 24, 36}, {66, 68, 84}, {50, 52, 68}, {96, 100, 122}, {26, 26, 40},
      {255, 170, 70}, {110, 190, 255}, {205, 225, 255} },
    /* haunted marsh: sickly green */
    { {30, 52, 42}, {21, 39, 33}, {12, 26, 22}, {62, 56, 50}, {46, 40, 40}, {86, 94, 72}, {24, 19, 26},
      {255, 175, 70}, {120, 255, 170}, {200, 255, 225} },
    /* thunder peak: violet rock */
    { {48, 40, 70}, {35, 29, 54}, {24, 18, 40}, {80, 72, 100}, {62, 55, 80}, {112, 104, 138}, {32, 25, 46},
      {255, 165, 80}, {200, 120, 255}, {235, 215, 255} },
};
/* how much of each base colour survives in the storm levels (percent); the ground colours are replaced */
static const uint8_t STORM_SCALE[32] = {
    100, 76, 70, 84, 92, 92, 75, 94, 96, 85, 60, 88, 92, 85, 62, 88,
    92, 58, 60, 70, 70, 70, 70, 70, 62, 92, 70, 85, 80, 74, 60, 78
};

static uint8_t PAL_LV[NUM_LEVELS][PAL_N][3];
static int     pal_ready;

static uint8_t clamp8(float v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5f)); }

static void palettes_init(void)
{
    if (pal_ready) return;
    pal_ready = 1;
    for (int lv = 0; lv < NUM_LEVELS; lv++) {
        const Theme *t = &THEME[lv];
        for (int c = 0; c < 32; c++) {
            float rgb[3];
            for (int k = 0; k < 3; k++) {
                rgb[k] = game_palette[c][k];
                if (lv > 0) rgb[k] = rgb[k] * STORM_SCALE[c] / 100.0f;
            }
            if (lv > 0) {
                const uint8_t *ov = NULL;
                switch (c) {
                case C_GRASS1: ov = t->g1; break;  case C_GRASS2: ov = t->g2; break;
                case C_DKGREEN: ov = t->dg; break; case C_DIRT1: ov = t->d1; break;
                case C_DIRT2: ov = t->d2; break;   case C_TAN: ov = t->tn; break;
                case C_DKBROWN: ov = t->db; break; default: break;
                }
                if (ov) for (int k = 0; k < 3; k++) rgb[k] = ov[k];
                if (c != C_BLACK) { rgb[0] *= 0.94f; rgb[2] = rgb[2] * 1.06f + 4.0f; }   /* cold tint */
            }
            for (int k = 0; k < 3; k++) {
                PAL_LV[lv][c][k] = clamp8(rgb[k]);
                PAL_LV[lv][32 + c][k] = clamp8((rgb[k] + (t->warm[k] - rgb[k]) * 0.38f) * 1.1f);
                PAL_LV[lv][64 + c][k] = clamp8((rgb[k] + (t->cool[k] - rgb[k]) * 0.38f) * 1.1f);
                PAL_LV[lv][96 + c][k] = clamp8((rgb[k] + (t->warm[k] - rgb[k]) * 0.17f) * 1.04f);
                PAL_LV[lv][128 + c][k] = clamp8((rgb[k] + (t->cool[k] - rgb[k]) * 0.17f) * 1.04f);
            }
        }
    }
}

/* 0..1: brightness of the current lightning flash (two quick strobes, then a fade) */
static float flash_level(const Game *g)
{
    if (g->level == 0 || g->strike_amp <= 0) return 0.0f;
    float s = g->strike_t, f;
    if (s < 0.05f) f = 1.0f;
    else if (s < 0.09f) f = 0.25f;
    else if (s < 0.16f) f = 0.9f;
    else f = 0.9f * expf(-(s - 0.16f) * 7.5f);
    f *= g->strike_amp;
    return f < 0.01f ? 0.0f : f;
}

void game_get_palette(const Game *g, uint8_t out[PAL_N][3])
{
    palettes_init();
    int lv = g->level < 0 || g->level >= NUM_LEVELS ? 0 : g->level;
    memcpy(out, PAL_LV[lv], sizeof PAL_LV[lv]);
    float f = flash_level(g);
    if (f > 0) {
        for (int c = 0; c < PAL_N; c++)
            for (int k = 0; k < 3; k++)
                out[c][k] = clamp8(out[c][k] + (THEME[lv].flash[k] - out[c][k]) * f * 0.62f);
    }
}

const char *game_level_name(int level) { return LEVELS[level < 0 || level >= NUM_LEVELS ? 0 : level].name; }

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
static int sound_on = 1;                    /* the pause menu can switch every sound effect off */
int game_sound_on(void) { return sound_on; }

static void sfx(Game *g, int id)
{
    if (!sound_on) return;
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

/* ---- storm weather: lightning strikes at random, thunder follows a moment later ---- */
static uint32_t wrnd(Game *g)
{
    uint32_t x = g->wx ? g->wx : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->wx = x;
    return x;
}
static float wrndf(Game *g) { return (float)(wrnd(g) >> 8) * (1.0f / 16777216.0f); }

static void update_weather(Game *g, float dt)
{
    if (g->level == 0) return;
    g->strike_t += dt;
    if (g->strike_t > 30.0f) g->strike_t = 30.0f;
    if (g->thunder_pending) {
        g->thunder_t -= dt;
        if (g->thunder_t <= 0) { g->thunder_pending = 0; sfx(g, SFX_THUNDER); }
    }
    g->bolt_cd -= dt;
    if (g->bolt_cd > 0) return;

    int bolt = wrndf(g) < 0.65f;                   /* the rest are distant sheet lightning: a flash, no bolt */
    g->strike_t = 0; g->strike_amp = bolt ? 1.0f : 0.5f;
    if (bolt) {
        g->bolt_x0 = 24 + (int)(wrnd(g) % 272);
        g->bolt_x1 = g->bolt_x0 + (int)(wrnd(g) % 81) - 40;
        if (g->bolt_x1 < 8) g->bolt_x1 = 8;
        if (g->bolt_x1 > 311) g->bolt_x1 = 311;
        g->bolt_y1 = MAP_Y + 30 + (int)(wrnd(g) % 120);
        g->bolt_seed = (int)(wrnd(g) & 0x7fff);
        if (g->state != ST_PAUSE) {                /* sparks where it lands */
            for (int i = 0; i < 10; i++) {
                float a = wrndf(g) * 2.0f * (float)M_PI, sp = 30.0f + wrndf(g) * 60.0f;
                add_part(g, (float)g->bolt_x1, (float)g->bolt_y1, cosf(a) * sp, sinf(a) * sp - 20.0f,
                         0.25f + wrndf(g) * 0.25f, (i & 1) ? C_WHITE : C_CYAN, 1 + (i & 1));
            }
            add_ring(g, (float)g->bolt_x1, (float)g->bolt_y1, 13, 0.3f, C_CYAN);
        }
    }
    g->thunder_t = 0.15f + wrndf(g) * 0.45f;               /* the rumble follows the flash by a moment */
    g->thunder_pending = 1;
    g->bolt_cd = 3.5f + wrndf(g) * 6.0f;
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
    if (decor_blocks(decor_at(tx, ty))) return 0;
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
/* every wave is a little tougher than the last: hit points compound on top of the steady climb */
static float wave_hp_scale(int wave)
{
    int k = wave > 0 ? wave - 1 : 0;
    return (1.0f + HP_GROWTH * (float)k) * powf(HP_COMPOUND, (float)k);
}

static void spawn_enemy_at(Game *g, int type, float dist)
{
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (!g->en[i].alive) {
            Enemy *e = &g->en[i];
            memset(e, 0, sizeof *e);
            e->alive = 1; e->type = (uint8_t)type; e->uid = ++g->next_uid;
            e->hp = e->maxhp = EN_DEF[type].hp * (type == EN_BOSS ? 1.0f + HP_GROWTH * (float)(g->wave > 1 ? g->wave - 1 : 0) : wave_hp_scale(g->wave)) * LV->hp_mult;   /* bosses only get the steady climb */
            e->dist = dist; e->ab_t = 1.0f + rndf(g);
            e->slow_f = 1.0f;
            if (dist > 0) { float x, y; game_path_point(dist, &x, &y); e->x = x; e->y = y; while (e->seg < path_n - 2 && dist > path_cum[e->seg + 1]) e->seg++; }
            else { e->x = path_x[0]; e->y = path_y[0]; }
            return;
        }
}
static void spawn_enemy(Game *g, int type) { spawn_enemy_at(g, type, 0); }

static int enemies_alive(const Game *g)
{
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) n += g->en[i].alive;
    return n;
}

static int best_wave[NUM_LEVELS];             /* best wave reached on each level this session */
int game_best_wave(int level) { return level < 0 || level >= NUM_LEVELS ? 0 : best_wave[level]; }

/* The hand-made tables cover the first waves; after that each wave is the last table row, bigger every time,
   with a boss every fifth wave, so the game never ends until the keep falls. */
static const char *const NEW_NAME[EN_COUNT] = {0, 0, 0, 0, "ZOMBIES", "PUMPKIN ZOMBIES", "WITCHES", "BLACK CATS", "GHOSTS"};
static const int NEW_WAVE[EN_COUNT] = {0, 0, 0, 0, 5, 10, 15, 20, 25};   /* a new kind of monster every 5 waves */

static void wave_counts(int wave, uint8_t out[EN_COUNT])
{
    memset(out, 0, EN_COUNT);
    if (wave <= LV->nwaves) memcpy(out, LV->waves[wave - 1], 4);
    else {
        const uint8_t *base = LV->waves[LV->nwaves - 1];
        int k = wave - LV->nwaves;
        for (int t = 0; t < 3; t++) {
            int n = base[t] * (100 + 14 * k) / 100;
            out[t] = (uint8_t)(n > 60 ? 60 : n);
        }
        int b = (wave % 5 == 0) ? 1 + k / 8 : 0;
        out[3] = (uint8_t)(b > 8 ? 8 : b);
    }
    /* the newer monsters join in from their wave on, and their numbers keep growing */
    for (int t = EN_ZOMBIE; t < EN_COUNT; t++) {
        if (wave < NEW_WAVE[t]) continue;
        int k = wave - NEW_WAVE[t];
        static const uint8_t first[EN_COUNT] = {0, 0, 0, 0, 3, 2, 2, 5, 2};
        static const uint8_t per5[EN_COUNT]  = {0, 0, 0, 0, 3, 2, 2, 4, 2};     /* extra monsters every 5 waves */
        int n = first[t] + per5[t] * k / 5 + k / 2;
        out[t] = (uint8_t)(n > 40 ? 40 : n);
    }
}

static void start_wave(Game *g)
{
    g->wave++;
    uint8_t w[EN_COUNT];
    wave_counts(g->wave, w);
    int n = 0;
    for (int t = 0; t < EN_COUNT; t++)
        for (int k = 0; k < w[t] && n < 512; k++) g->spawn_q[n++] = (uint8_t)t;
    for (int i = n - 1; i > 0; i--) {                    /* shuffle */
        int j = (int)(rnd(g) % (uint32_t)(i + 1));
        uint8_t tmp = g->spawn_q[i]; g->spawn_q[i] = g->spawn_q[j]; g->spawn_q[j] = tmp;
    }
    g->spawn_n = n; g->spawn_i = 0; g->spawn_t = 0.6f; g->wave_active = 1;
    char b[40];
    int debut = 0;
    for (int t = EN_ZOMBIE; t < EN_COUNT; t++) if (g->wave == NEW_WAVE[t]) debut = t;
    if (debut)     snprintf(b, sizeof b, "WAVE %d: NEW %s!", g->wave, NEW_NAME[debut]);
    else if (w[3]) snprintf(b, sizeof b, "BOSS WAVE %d!", g->wave);
    else           snprintf(b, sizeof b, "WAVE %d", g->wave);
    set_banner(g, b, debut ? C_PINK : (w[3] ? C_RED : C_YELLOW));
    sfx(g, (debut || w[3]) ? SFX_BOSS : SFX_WAVE);
}

static void kill_enemy(Game *g, Enemy *e)
{
    static const uint8_t c1[EN_COUNT] = {C_LTGREEN, C_PINK, C_LIGHT, C_ORANGE, C_LTGREEN, C_ORANGE, C_PURPLE, C_PURPLE, C_WHITE};
    static const uint8_t c2[EN_COUNT] = {C_GREEN, C_PURPLE, C_STONE, C_RED, C_DKGREEN, C_YELLOW, C_PINK, C_YELLOW, C_CYAN};
    int gold = EN_DEF[e->type].gold;
    g->gold += gold; g->kills++;
    add_text(g, e->x, e->y - 8, gold);
    burst(g, e->x, e->y, e->type == EN_BOSS ? 60 : 8, c1[e->type], c2[e->type],
          e->type == EN_BOSS ? 110 : 55, 0.5f);
    if (e->type == EN_BOSS) {
        add_ring(g, e->x, e->y, 40, 0.5f, C_CYAN);
        add_ring(g, e->x, e->y, 24, 0.35f, C_WHITE);
        g->shake_t = 0.5f;
        sfx(g, SFX_EXPLODE);
    } else {
        sfx(g, SFX_KILL);
    }
    e->alive = 0;
    if (e->type == EN_PUMPKIN) {                         /* the pumpkin bursts open: two zombies crawl out */
        burst(g, e->x, e->y, 24, C_ORANGE, C_YELLOW, 110, 0.6f);
        spawn_enemy_at(g, EN_ZOMBIE, e->dist > 6 ? e->dist - 6 : 0);
        spawn_enemy_at(g, EN_ZOMBIE, e->dist > 12 ? e->dist - 12 : 0);
    }
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
        float spd = EN_DEF[e->type].speed * (1.0f + 0.008f * (float)(g->wave > 1 ? (g->wave < 40 ? g->wave : 40) - 1 : 0));
        if (g->state == ST_PLAY) {
            if (e->type == EN_CAT) {                             /* magical blink: pops a little way ahead */
                e->ab_t -= dt;
                if (e->ab_t <= 0) {
                    e->ab_t = 1.8f + rndf(g) * 1.6f;
                    burst(g, e->x, e->y, 10, C_PURPLE, C_PINK, 70, 0.35f);
                    e->dist += 22.0f;
                    if (e->dist < path_len) { float nx, ny; game_path_point(e->dist, &nx, &ny); burst(g, nx, ny, 10, C_PURPLE, C_PINK, 70, 0.35f); }
                }
            } else if (e->type == EN_WITCH) {                    /* heals every monster close by */
                e->ab_t -= dt;
                if (e->ab_t <= 0) {
                    e->ab_t = 1.6f;
                    add_ring(g, e->x, e->y, 30, 0.35f, C_PINK);
                    for (int j = 0; j < MAX_ENEMIES; j++) {
                        Enemy *o = &g->en[j];
                        if (!o->alive || o == e || o->type == EN_BOSS) continue;
                        float dx = o->x - e->x, dy = o->y - e->y;
                        if (dx * dx + dy * dy < 30.0f * 30.0f && o->hp < o->maxhp) {
                            o->hp += o->maxhp * 0.12f; if (o->hp > o->maxhp) o->hp = o->maxhp;
                            add_part(g, o->x, o->y - 4, 0, -25, 0.4f, C_PINK, 1);
                            add_part(g, o->x + 2, o->y - 2, 0, -20, 0.35f, C_LTGREEN, 1);
                        }
                    }
                }
            }
        }
        e->dist += spd * f * dt;
        if (e->dist >= path_len) {
            e->alive = 0;
            if (g->state == ST_PLAY) {
#ifdef LEAKDBG
                fprintf(stderr, "leak wave %d type %d hp %.0f lives %d\n", g->wave, e->type, e->maxhp, g->lives);
#endif
                g->lives -= EN_DEF[e->type].leak;
                g->hurt_t = 0.45f;
                g->shake_t = EN_DEF[e->type].leak >= 10 ? 0.6f : 0.25f;
                burst(g, 304, e->y, 14, C_RED, C_ORANGE, 75, 0.5f);
                add_ring(g, 304, e->y, 16, 0.3f, C_RED);
                sfx(g, SFX_LEAK);
                if (g->lives <= 0) {
                    g->lives = 0; g->state = ST_OVER; g->state_t = 0; sfx(g, SFX_OVER);
                    if (g->wave > best_wave[g->level]) { best_wave[g->level] = g->wave; g->new_best = 1; }
                }
            }
            continue;
        }
        while (e->seg < path_n - 2 && e->dist > path_cum[e->seg + 1]) e->seg++;
        float t = (e->dist - path_cum[e->seg]) / (path_cum[e->seg + 1] - path_cum[e->seg]);
        e->x = path_x[e->seg] + (path_x[e->seg + 1] - path_x[e->seg]) * t;
        e->y = path_y[e->seg] + (path_y[e->seg + 1] - path_y[e->seg]) * t;
    }
}

static void add_arc(Game *g, float x0, float y0, float x1, float y1)
{
    for (int i = 0; i < MAX_ARC; i++)
        if (g->ar[i].life <= 0) { g->ar[i] = (Arc){x0, y0, x1, y1, 0.13f, (uint8_t)(rnd(g) & 255)}; return; }
}

/* the storm orb: a bolt of lightning that jumps from monster to monster, weaker with each jump */
static void storm_strike(Game *g, int tx, int ty, Tower *t, int ei)
{
    float x, y; tile_center(tx, ty, &x, &y); y -= 3.0f;
    int chain = STORM_CHAIN[t->level], cur = ei;
    float dmg = TW_DMG[TW_STORM][t->level], cx = x, cy = y;
    uint8_t hit[MAX_ENEMIES];
    memset(hit, 0, sizeof hit);
    for (int k = 0; k < chain && cur >= 0; k++) {
        Enemy *e = &g->en[cur];
        hit[cur] = 1;
        float ex = e->x, ey = e->y - 2.0f;
        add_arc(g, cx, cy, ex, ey);
        add_part(g, ex, ey, 0, -10, 0.15f, C_WHITE, 1);
        hurt_enemy(g, e, dmg * (1.0f - 0.2f * (float)k));
        cx = ex; cy = ey;
        int nxt = -1; float bd = 30.0f * 30.0f;
        for (int j = 0; j < MAX_ENEMIES; j++) {
            const Enemy *o = &g->en[j];
            if (!o->alive || hit[j]) continue;
            float dx = o->x - ex, dy = o->y - ey, d2 = dx * dx + dy * dy;
            if (d2 < bd) { bd = d2; nxt = j; }
        }
        cur = nxt;
    }
    t->flash = 0.07f;
    sfx(g, SFX_ZAP);
}

static void fire(Game *g, int tx, int ty, Tower *t, int ei)
{
    if (t->type == TW_STORM) { storm_strike(g, tx, ty, t, ei); return; }
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
                burst(g, p->x, p->y - 4, 5, C_LIGHT, C_GRAY, 35, 0.3f);
                sfx(g, SFX_CANNON);
            } else if (t->type == TW_FROST) {
                add_part(g, p->x, p->y - 2, 0, -12, 0.15f, C_ICE, 1);
                sfx(g, SFX_FROST);
            } else {
                add_part(g, p->x, p->y - 2, 0, -12, 0.15f, C_WHITE, 1);
                sfx(g, SFX_ARROW);
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
                if (t->type == TW_CANNON && is_flyer(e->type)) continue;   /* can't hit flyers */
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
            if (!e->alive || is_flyer(e->type)) continue;
            float dx = e->x - p->x, dy = e->y - p->y;
            if (dx * dx + dy * dy <= r * r) hurt_enemy(g, e, p->dmg);
        }
        burst(g, p->x, p->y, 12, C_ORANGE, C_YELLOW, 70, 0.4f);
        burst(g, p->x, p->y, 5, C_DKSTONE, C_GRAY, 35, 0.5f);
        add_ring(g, p->x, p->y, r + 3, 0.26f, C_ORANGE);
        if (p->level == 2 && g->shake_t < 0.1f) g->shake_t = 0.1f;
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
        burst(g, p->x, p->y, 8, C_ICE, C_CYAN, 50, 0.35f);
        add_ring(g, p->x, p->y, r, 0.28f, C_ICE);
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
            if (g->frame & 1) {
                if (p->type == TW_CANNON) add_part(g, p->x, p->y, 0, -8, 0.2f, C_GRAY, 1);
                else if (p->type == TW_FROST) add_part(g, p->x, p->y, 0, 0, 0.18f, (g->frame & 2) ? C_CYAN : C_ICE, 1);
            }
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
    for (int i = 0; i < MAX_ARC; i++) if (g->ar[i].life > 0) g->ar[i].life -= dt;
    if (g->msg_t > 0)    g->msg_t -= dt;
    if (g->banner_t > 0) g->banner_t -= dt;
    if (g->hurt_t > 0)   g->hurt_t -= dt;
    if (g->shake_t > 0)  g->shake_t -= dt;
}

static void sim_step(Game *g, float dt)
{
    if (!g->wave_active) {
        g->wave_timer -= dt;
        if (g->wave_timer <= 0) start_wave(g);
    } else if (g->spawn_i < g->spawn_n) {
        g->spawn_t -= dt;
        if (g->spawn_t <= 0) {
            int type = g->spawn_q[g->spawn_i++];
            spawn_enemy(g, type);
            static const float gap[EN_COUNT] = {0.75f, 0.45f, 1.3f, 2.5f, 0.8f, 1.2f, 1.0f, 0.4f, 1.0f};
            g->spawn_t = gap[type];
        }
    } else if (enemies_alive(g) == 0) {
        g->wave_active = 0;
        int bonus = 15 + 3 * g->wave;
        g->gold += bonus;
        char b[40];
        snprintf(b, sizeof b, "WAVE CLEAR! +%dG", bonus);
        set_banner(g, b, C_LTGREEN);
        g->wave_timer = 12.0f;
        sfx(g, SFX_CLEAR);
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
static int level_pref;                       /* level shown on the title screen / retried after a loss */

/* path, scenery, map picture and weather for one level */
static void setup_level(Game *g, int lvl)
{
    if (lvl < 0 || lvl >= NUM_LEVELS) lvl = 0;
    path_init(lvl);
    decor_init(lvl);
    wind_init();
    g->level = lvl; g->nwaves = 0;                 /* 0 = endless */
    build_bg(g);
    g->strike_t = 30.0f; g->strike_amp = 0; g->thunder_pending = 0;
    g->wx = 0x7f4a7c15u ^ ((uint32_t)lvl * 0x9e3779b9u);
    g->bolt_cd = 1.5f + (float)lvl * 0.5f;
}

void game_init(Game *g)
{
    sprites_init();
    audio_init();
    memset(g, 0, sizeof *g);
    g->rng = 0x1badf00du;
    g->state = ST_TITLE;
    g->cx = 6; g->cy = 4;
    setup_level(g, level_pref);
}

void game_start_level(Game *g, int level)
{
    uint32_t seed = g->rng ^ g->frame * 2654435761u;
    if (level < 0 || level >= NUM_LEVELS) level = 0;
    setup_level(g, level);
    level_pref = level;
    memset(g->towers, 0, sizeof g->towers);
    memset(g->en, 0, sizeof g->en);
    memset(g->pr, 0, sizeof g->pr);
    memset(g->pt, 0, sizeof g->pt);
    memset(g->ft, 0, sizeof g->ft);
    memset(g->rg, 0, sizeof g->rg);
    g->state = ST_PLAY; g->state_t = 0;
    g->lives = 20; g->gold = LEVELS[level].start_gold; g->wave = 0; g->kills = 0;
    g->cx = 6; g->cy = 4; g->sel = TW_ARROW; g->fast = 0;
    g->wave_active = 0; g->wave_timer = 18.0f;
    g->msg_t = 0; g->banner_t = 0; g->hurt_t = 0; g->shake_t = 0; g->fade = 1.0f;
    g->rng = seed ? seed : 0x1badf00du;
    set_banner(g, level ? LEVELS[level].name : "GET READY!", C_CYAN);
    sfx(g, SFX_START);
}

void game_start(Game *g) { game_start_level(g, g->level); }

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
    if (g->state != ST_TITLE) {                      /* right stick: zoom in/out, centred on the cursor */
        if ((pressed & BTN_ZIN) && g->zoom_lvl < 2) { g->zoom_lvl++; sfx(g, SFX_MOVE); }
        if ((pressed & BTN_ZOUT) && g->zoom_lvl > 0) { g->zoom_lvl--; sfx(g, SFX_MOVE); }
    }
    {
        float target = 1.0f + (float)g->zoom_lvl, k = dt * 9.0f > 1.0f ? 1.0f : dt * 9.0f;
        if (g->zoom < 1.0f) g->zoom = 1.0f;
        g->zoom += (target - g->zoom) * k;
        float fx = (float)(g->cx * TILE + 8), fy = (float)(g->cy * TILE + 8);
        if (g->zfx <= 0) { g->zfx = fx; g->zfy = fy; }
        g->zfx += (fx - g->zfx) * k; g->zfy += (fy - g->zfy) * k;
    }
    for (int i = 0; i < SFX_COUNT; i++) if (g->sfx_cd[i] > 0) g->sfx_cd[i] -= dt;
    update_weather(g, dt);
    if (g->fade > 0) g->fade -= dt * 2.2f;

    switch (g->state) {
    case ST_TITLE:
        g->title_t += dt;
        if (g->title_t > 1.1f) { g->title_t = 0; spawn_enemy(g, (int)(g->frame / 66) % 3); }
        update_enemies(g, dt);
        update_fx(g, dt);
        if (pressed & (BTN_LEFT | BTN_RIGHT)) {             /* pick a level */
            level_pref = (g->level + ((pressed & BTN_RIGHT) ? 1 : NUM_LEVELS - 1)) % NUM_LEVELS;
            setup_level(g, level_pref);
            memset(g->en, 0, sizeof g->en); memset(g->pt, 0, sizeof g->pt); memset(g->rg, 0, sizeof g->rg);
            g->title_t = 0;
            sfx(g, SFX_SELECT);
        }
        if (pressed & (BTN_A | BTN_PLUS)) game_start(g);
        return;
    case ST_PAUSE:
        if (pressed & BTN_UP)   { g->pause_sel = (g->pause_sel + 2) % 3; sfx(g, SFX_MOVE); }
        if (pressed & BTN_DOWN) { g->pause_sel = (g->pause_sel + 1) % 3; sfx(g, SFX_MOVE); }
        if (pressed & BTN_PLUS) { g->state = ST_PLAY; sfx(g, SFX_PAUSE); }
        else if (pressed & BTN_A) {
            if (g->pause_sel == 2) { level_pref = g->level; game_init(g); sfx(g, SFX_SELECT); }
            else if (g->pause_sel == 1) { sound_on = !sound_on; sfx(g, SFX_SELECT); }   /* silent when switching off */
            else { g->state = ST_PLAY; sfx(g, SFX_PAUSE); }
        }
        return;
    case ST_OVER:
    case ST_WIN:
        g->state_t += dt;
        update_fx(g, dt);
        if (g->state_t > 1.2f && (pressed & BTN_A)) {
            if (g->state == ST_WIN && g->level < NUM_LEVELS - 1) game_start_level(g, g->level + 1);
            else { level_pref = (g->state == ST_WIN) ? 0 : g->level; game_init(g); }
        }
        return;
    case ST_PLAY:
        break;
    }

    /* --- input --- */
    move_cursor(g, held, pressed);
    if (pressed & BTN_L) { g->sel = (g->sel + TW_COUNT - 1) % TW_COUNT; sfx(g, SFX_SELECT); }
    if (pressed & BTN_R) { g->sel = (g->sel + 1) % TW_COUNT;            sfx(g, SFX_SELECT); }
    if (pressed & BTN_Y) { g->fast = !g->fast; sfx(g, SFX_SELECT); }
    if (pressed & BTN_PLUS) { g->state = ST_PAUSE; g->pause_sel = 0; sfx(g, SFX_PAUSE); return; }
    if (pressed & BTN_A) {
        if (g->towers[g->cy][g->cx].used) game_upgrade(g, g->cx, g->cy);
        else game_build(g, g->cx, g->cy, g->sel);
    }
    if (pressed & BTN_B) {
        if (g->towers[g->cy][g->cx].used) game_sell(g, g->cx, g->cy);
    }
    if ((pressed & BTN_X) && !g->wave_active) {
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

/* the palette offset of whatever light the ground at this spot receives, so sprites pick up the glow too */
static int ambient_at(const Game *g, int x, int y)
{
    if (g->level == 0) return 0;
    y -= MAP_Y;
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= MAP_H * TILE) return 0;
    return g->bg[y * SCREEN_W + x] & 0xE0;
}

/* ---- the magical weapons: a floating orb above each pedestal, with orbiting motes and a gentle pulse ---- */
static void orb_px(uint8_t *fb, int x, int y, int c, int sc, int dim)
{
    if (dim) c = (c == C_WHITE || c == C_LIGHT || c == C_ICE || c == C_PINK) ? C_GRAY : C_DKGRAY;
    else if (c < 32) c += blit_add;
    if (sc == 1) px(fb, x, y, c); else rect(fb, x, y, sc, sc, c);
}

/* cx,cy: centre of the orb's resting spot in screen pixels; sc: pixel size (the title screen draws it big) */
static void draw_orb(uint8_t *fb, int type, int level, int cx, int cy, int frame, int seed, int sc, int dim)
{
    static const uint8_t pal[TW_COUNT][3] = {
        {C_DKGREEN, C_GREEN, C_LTGREEN}, {C_RED, C_ORANGE, C_YELLOW},
        {C_BLUE, C_CYAN, C_ICE}, {C_DKPURPLE, C_PURPLE, C_PINK}
    };
    float ph = (float)seed * 1.7f;
    cy += (int)lrintf(sinf((float)frame * 0.07f + ph) * 1.3f) * sc;          /* the orb bobs up and down */
    int r = level == 2 ? 4 : 3;
    const uint8_t *c = pal[type];
    int pulse = ((frame / 7 + seed) % 5) == 0;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy, in;
            if (type == TW_FROST) { in = abs(dx) + abs(dy) <= r + 1; if (!in) continue; d2 = (abs(dx) + abs(dy)) * (abs(dx) + abs(dy)); if (d2 >= (r + 1) * (r + 1)) d2 = r * r + r; }
            else if (d2 > r * r + 1) continue;
            int col = d2 >= r * r - 1 ? c[0] : (d2 >= r + 1 ? c[1] : (pulse ? C_WHITE : c[2]));
            if (dx <= -1 && dy <= -1 && d2 > 1 && d2 < r * r - 1) col = C_WHITE;           /* shine */
            orb_px(fb, cx + dx * sc, cy + dy * sc, col, sc, dim);
        }
    if (dim) return;
    uint32_t h = hash2(seed, frame / 3, type);
    if (type == TW_CANNON) {                                    /* flickering flame tips */
        orb_px(fb, cx + ((int)(h % 3) - 1) * sc, cy - (r + 1) * sc, (h & 8) ? C_YELLOW : C_ORANGE, sc, 0);
        if (h & 16) orb_px(fb, cx + ((int)((h >> 5) % 5) - 2) * sc, cy - (r + 2) * sc, C_ORANGE, sc, 0);
    } else if (type == TW_STORM) {                              /* little crackles of lightning */
        if ((h & 3) != 0) {
            int dxs = (int)((h >> 3) % 3) - 1, dys = (int)((h >> 6) % 3) - 1;
            if (dxs == 0 && dys == 0) dxs = 1;
            orb_px(fb, cx + dxs * (r + 1) * sc, cy + dys * (r + 1) * sc, (h & 4) ? C_WHITE : C_CYAN, sc, 0);
            orb_px(fb, cx + dxs * (r + 2) * sc, cy + dys * (r + 2) * sc, C_PINK, sc, 0);
        }
    } else if (type == TW_FROST && (h % 7) == 0) {              /* a falling snowflake */
        orb_px(fb, cx + ((int)((h >> 4) % 7) - 3) * sc, cy + (r + 2) * sc, C_WHITE, sc, 0);
    }
    int n = 1 + level;                                          /* orbiting motes */
    for (int k = 0; k < n; k++) {
        float a = (float)frame * 0.09f * (1.0f + 0.35f * (float)k) + (float)k * 6.2832f / (float)n + ph;
        int mx = cx + (int)lrintf(cosf(a) * (float)(r + 3)) * sc, my = cy + (int)lrintf(sinf(a) * (float)(r + 3) * 0.45f) * sc;
        orb_px(fb, mx, my, k & 1 ? c[2] : C_WHITE, sc, 0);
    }
}

static void draw_tower(uint8_t *fb, const Game *g, const Tower *t, int tx, int ty)
{
    int x = tx * TILE, y = MAP_Y + ty * TILE;
    /* ground shadow: bottom edge + right side (light comes from the top-left) */
    for (int i = 1; i < 15; i++) shadow_px(fb, x + i, y + 15);
    for (int j = 9; j < 16; j++) { shadow_px(fb, x + 14, y + j); shadow_px(fb, x + 15, y + j); }
    blit_add = ambient_at(g, x + 8, y + 12);
    blit(fb, &SPR[tower_sprite(t->type)], x, y, t->level == 0 ? M_NORMAL : (t->level == 1 ? M_TIER2 : M_TIER3));
    draw_orb(fb, t->type, t->level, x + 8, y + 5, (int)g->frame, tx * 7 + ty * 3, 1, 0);
    blit_add = 0;
    int n = t->level + 1, start = 8 - (n * 3 - 1) / 2;
    for (int i = 0; i < n; i++) {
        rect(fb, x + start + i * 3, y + 14, 2, 2, C_YELLOW);
        px(fb, x + start + i * 3, y + 15, C_ORANGE); px(fb, x + start + i * 3 + 1, y + 15, C_ORANGE);
    }
    if (t->flash > 0) {                                  /* muzzle flash */
        int mx = x + 8, my = y + 1;
        int c = t->type == TW_FROST ? C_ICE : (t->type == TW_CANNON ? C_ORANGE : (t->type == TW_STORM ? C_PINK : C_LTGREEN));
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
    case EN_ZOMBIE: spr = S_ZOMBIE; bob = (((g->frame >> 4) + e->uid) & 1) ? -1 : 0; break;     /* shambles */
    case EN_PUMPKIN: spr = S_PUMPZ; bob = (((g->frame >> 4) + e->uid) & 1) ? -1 : 0; break;
    case EN_WITCH: spr = S_WITCH; bob = (int)lrintf(sinf((float)g->frame * 0.1f + (float)e->uid) * 2.0f); break;
    case EN_CAT:   spr = S_CAT; bob = (((g->frame >> 2) + e->uid) & 1) ? -1 : 0; break;
    case EN_GHOST: spr = S_GHOST; bob = (int)lrintf(sinf((float)g->frame * 0.07f + (float)e->uid) * 2.5f); break;
    default:       spr = S_BOSS;  bob = (((g->frame >> 3) + e->uid) & 1) ? -1 : 0; break;
    }
    if (e->slow_t > 0) mode = M_FROZEN;
    if (e->hitflash > 0) mode = M_FLASH;
    else if (e->type == EN_GHOST && ((g->frame / 5 + e->uid) % 4) == 0) mode = M_GHOST;     /* flickers in and out */
    int bw = SPR[spr].w, bh = SPR[spr].h;
    int x = (int)lrintf(e->x) - bw / 2, y = (int)lrintf(e->y) - 8 - (bh - 16) + 2 * (bh > 16) + bob;
    if (is_flyer(e->type)) y -= 3;
    /* soft shadow (smaller and lighter for flyers, which hover above the ground) */
    int sy = (int)lrintf(e->y) + 5;
    shadow_ellipse(fb, (int)lrintf(e->x), sy + (is_flyer(e->type) ? 3 : 0), e->type == EN_BOSS ? 12 : (is_flyer(e->type) ? 3 : 5));
    blit_add = ambient_at(g, x + bw / 2, y + bh * 3 / 4);
    blit(fb, &SPR[spr], x, y, mode);
    blit_add = 0;
    if (e->hp < e->maxhp) {
        int w = e->type == EN_BOSS ? 26 : 10, bx = (int)lrintf(e->x) - w / 2, by = y - 2;
        int fillw = (int)(w * e->hp / e->maxhp + 0.5f);
        if (fillw < 1) fillw = 1;
        rect(fb, bx - 1, by - 1, w + 2, 3, C_BLACK);
        rect(fb, bx, by, w, 1, C_DKRED);
        float f = e->hp / e->maxhp;
        rect(fb, bx, by, fillw, 1, f > 0.5f ? C_GREEN : (f > 0.25f ? C_YELLOW : C_RED));
    }
    if (e->type == EN_BOSS && mode != M_FLASH) {                  /* the eyes burn: pulse between red and yellow */
        int ec = ((g->frame / 8) & 1) ? C_YELLOW : C_ORANGE;
        px(fb, x + 11, y + 13, ec); px(fb, x + 21, y + 13, ec);
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
        snprintf(buf, n, "A:BUILD %s %dG%s", TW_NAME[g->sel], TW_COST[g->sel],
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
    if (g->level) snprintf(b, sizeof b, "L%d WAVE %d", g->level + 1, g->wave < 1 ? 1 : g->wave);
    else          snprintf(b, sizeof b, "WAVE %d", g->wave < 1 ? 1 : g->wave);
    draw_text(fb, 84, 4, b, C_CYAN, 1);
    int ox = g->level ? 12 : 0;
    /* status */
    if (!g->wave_active) {
        snprintf(b, sizeof b, "NEXT WAVE %d  X:CALL NOW", (int)ceilf(g->wave_timer));
        draw_text(fb, 130 + ox, 4, b, C_LTGREEN, 1);
    } else if (g->wave_active) {
        int left = (g->spawn_n - g->spawn_i);
        for (int i = 0; i < MAX_ENEMIES; i++) left += g->en[i].alive;
        snprintf(b, sizeof b, "ENEMIES %d", left);
        draw_text(fb, 130 + ox, 4, b, C_ORANGE, 1);
    }
    if (g->fast) draw_text(fb, 228 + ox, 4, ">>", C_YELLOW, 1);
    /* context line */
    tower_info_text(g, b, sizeof b);
    draw_text(fb, 4, 12, b, g->msg_t > 0 ? C_ORANGE : C_LIGHT, 1);
    /* tower selector */
    for (int i = 0; i < TW_COUNT; i++) {
        int x = 251 + i * 17;
        int afford = g->gold >= TW_COST[i];
        blit(fb, &SPR[tower_sprite(i)], x, 2, afford ? M_NORMAL : M_DIM);
        draw_orb(fb, i, 0, x + 8, 7, (int)g->frame, i, 1, afford ? 0 : 1);
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

/* the haunted house: eerie windows that flicker, an attic light, and a wisp of smoke from the chimney */
static void draw_keep_flag(uint8_t *fb, const Game *g)
{
    int x = LV->wp[LV->nwp - 1][0] * TILE, y = MAP_Y + LV->wp[LV->nwp - 1][1] * TILE;
    int f = (int)(g->frame & 0xFFFFFF);
    if (g->level) {                                              /* the house keeps its own colours inside the flickering glow */
        int tx = LV->wp[LV->nwp - 1][0] * TILE, ty = LV->wp[LV->nwp - 1][1] * TILE;
        for (int j = 0; j < TILE; j++) memcpy(fb + (MAP_Y + ty + j) * SCREEN_W + tx, g->bg + (ty + j) * SCREEN_W + tx, TILE);
    }
    static const uint8_t lit[4] = {C_YELLOW, C_ORANGE, C_WHITE, C_YELLOW};
    int add = g->level ? (int)(g->bg[(LV->wp[LV->nwp - 1][1] * TILE + 12) * SCREEN_W + LV->wp[LV->nwp - 1][0] * TILE + 8] & 0xE0) : 0;
    for (int w = 0; w < 2; w++) {
        int wx = x + (w ? 10 : 4);
        uint32_t h = hash2(w, 3, f / 5);
        int c = (h % 9 == 0) ? C_DKBROWN : lit[h % 4];           /* sometimes the light blinks out */
        for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) px(fb, wx + i, y + 9 + j, c + (c == C_DKBROWN ? 0 : add));
        px(fb, wx, y + 9, (c == C_DKBROWN ? c : C_WHITE) + (c == C_DKBROWN ? 0 : add));
    }
    if (((f / 40) & 3) == 0) {                                   /* a ghostly face in the attic window */
        px(fb, x + 7, y + 4, C_WHITE); px(fb, x + 8, y + 4, C_WHITE); px(fb, x + 7, y + 5, C_CYAN); px(fb, x + 8, y + 5, C_WHITE);
    }
    for (int k = 0; k < 3; k++) {                                /* smoke */
        int t = (f / 3 + k * 14) % 42;
        int sx = x + 11 + (int)lrintf(trig_sin[(t * 24 + k * 200) & 1023] * 1.5f) + t / 14, sy = y - t / 4;
        if (sy >= MAP_Y && t < 36) px(fb, sx, sy, t < 18 ? C_LIGHT : C_GRAY);
    }
}

/* ---- storm effects: glow, lightning bolt, rain ---- */
static void gl(uint8_t *fb, int cx, int cy, int r, int ramp, int *budget)
{
    if (*budget <= 0) return;
    (*budget)--;
    glow_buf(fb + MAP_Y * SCREEN_W, MAP_H * TILE, cx, cy - MAP_Y, r, ramp);
}

/* moving light sources: shots, muzzle flashes, explosions, bosses and frozen enemies */
static void draw_glows(uint8_t *fb, const Game *g)
{
    int budget = 56;
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            const Tower *t = &g->towers[ty][tx];
            if (t->used && t->flash > 0)
                gl(fb, tx * TILE + 8, MAP_Y + ty * TILE + 4, 9, (t->type == TW_FROST || t->type == TW_STORM) ? 2 : 1, &budget);
        }
    for (int i = 0; i < MAX_PROJ; i++) {
        const Proj *p = &g->pr[i];
        if (p->alive) gl(fb, (int)p->x, (int)p->y, p->type == TW_ARROW ? 6 : (p->type == TW_CANNON ? 7 : 8), p->type == TW_FROST ? 2 : 1, &budget);
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive) continue;
        if (e->type == EN_BOSS) gl(fb, (int)e->x, (int)e->y - 6, 22, 2, &budget);
        else if (e->type == EN_WITCH || e->type == EN_CAT) gl(fb, (int)e->x, (int)e->y, 11, 2, &budget);
        else if (e->type == EN_GHOST) gl(fb, (int)e->x, (int)e->y, 12, 2, &budget);
        else if (e->type == EN_PUMPKIN) gl(fb, (int)e->x, (int)e->y, 9, 1, &budget);
        else if (e->slow_t > 0) gl(fb, (int)e->x, (int)e->y, 8, 2, &budget);
    }
    for (int i = 0; i < MAX_RING; i++) {
        const Ring *r = &g->rg[i];
        if (r->life <= 0 || !(r->col == C_ORANGE || r->col == C_YELLOW || r->col == C_RED)) continue;
        float t = 1.0f - r->life / r->max_life;
        if (t < 0.7f) gl(fb, (int)r->x, (int)r->y, (int)(r->maxr * (0.35f + 0.4f * t)) + 3, 1, &budget);
    }
}

static void bolt_line(uint8_t *fb, int x0, int y0, int x1, int y1, int c)
{
    int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    if (n < 1) n = 1;
    for (int i = 0; i <= n; i++) px(fb, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, c);
}

/* a jagged bolt from the top of the map to the strike point, with a halo; flickers twice */
static void draw_bolt(uint8_t *fb, const Game *g)
{
    if (g->strike_amp < 1.0f) return;
    float s = g->strike_t;
    if (!(s < 0.05f || (s >= 0.09f && s < 0.16f))) return;
    enum { N = 12 };
    int xs[N + 1], ys[N + 1], budget = 40;
    for (int i = 0; i <= N; i++) {
        int jit = (i == 0 || i == N) ? 0 : (int)(hash2(i, g->bolt_seed, 5) % 15) - 7;
        xs[i] = g->bolt_x0 + (g->bolt_x1 - g->bolt_x0) * i / N + jit;
        ys[i] = MAP_Y + (g->bolt_y1 - MAP_Y) * i / N;
    }
    int bx[4], by[4];                                   /* a short side branch */
    int dir = (g->bolt_seed & 1) ? 1 : -1;
    bx[0] = xs[5]; by[0] = ys[5];
    for (int i = 1; i < 4; i++) {
        bx[i] = bx[i - 1] + dir * (6 + (int)(hash2(i, g->bolt_seed, 9) % 6));
        by[i] = by[i - 1] + 9 + (int)(hash2(i, g->bolt_seed, 11) % 4);
    }
    for (int i = 0; i <= N; i++) gl(fb, xs[i], ys[i], 8, 2, &budget);
    gl(fb, xs[N], ys[N], 15, 2, &budget);
    for (int i = 0; i < N; i++) {
        bolt_line(fb, xs[i] + 1, ys[i], xs[i + 1] + 1, ys[i + 1], C_ICE);
        bolt_line(fb, xs[i], ys[i], xs[i + 1], ys[i + 1], C_WHITE);
    }
    for (int i = 0; i < 3; i++) bolt_line(fb, bx[i], by[i], bx[i + 1], by[i + 1], C_ICE);
}

/* rain is stateless: every drop's position is a function of the frame number */
static void rain_px(uint8_t *fb, int x, int y, int c)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < (unsigned)(MAP_H * TILE)) fb[(MAP_Y + y) * SCREEN_W + x] = (uint8_t)c;
}

static void draw_rain(uint8_t *fb, const Game *g)
{
    const int P = MAP_H * TILE + 16;
    long long f = (long long)g->frame;
    for (int i = 0; i < 52; i++) {
        uint32_t h = hash2(i, 5, 77);
        int layer = (i % 4 == 3) ? 2 : (i & 1);              /* mostly faint far/mid drops, a few near ones */
        int sp = 3 + layer * 2, len = 3 + layer * 2;
        int x0 = (int)(h % 360), y0 = (int)((h >> 9) % (uint32_t)P);
        int y = (int)((y0 + f * sp) % P) - 8;
        int x = (int)((((x0 - f * sp / 3) % 360) + 360) % 360) - 20;
        int c = layer == 2 ? C_LIGHT : C_GRAY;
        for (int k = 0; k < len; k++) {
            if (layer == 0 && (k & 1)) continue;             /* far drops are dotted: more transparent */
            rain_px(fb, x + k / 3, y - k, c);
        }
    }
    for (int j = 0; j < 12; j++) {                           /* faint ripples where drops land */
        uint32_t h = hash2(j, 9, 31);
        long long t = f + (long long)(h % 30);
        int ph = (int)(t % 30);
        if (ph >= 8) continue;
        uint32_t pos = hash2(j, (int)(t / 30), 33);
        int x = (int)(pos % 320), y = (int)((pos >> 9) % (MAP_H * TILE)), r = 1 + ph / 3;
        rain_px(fb, x - r, y, C_GRAY); rain_px(fb, x + r, y, C_GRAY);
        rain_px(fb, x - r / 2, y + 1, C_GRAY); rain_px(fb, x + r / 2, y + 1, C_GRAY);
    }
}

/* ---- living scenery: swaying trees, flickering flames, idle skeletons, twinkles, wind over the grass ---- */
static void sprite_live(uint8_t *fb, const Sprite *s, int x, int y, const int8_t *off, int ramp)
{
    for (int j = 0; j < s->h; j++) {
        int o = off ? off[j] : 0;
        for (int i = 0; i < s->w; i++) {
            int c = s->d[j * s->w + i];
            if (c != 255) px(fb, x + i + o, y + j, c + ramp);
        }
    }
}

/* smooth pseudo-random flicker 0..1 for a light, from a few sines at unrelated speeds */
static float flick(int f, uint32_t seed)
{
    int a = (int)(seed % 1024), b = (int)((seed >> 10) % 1024);
    float v = trig_sin[(f * 7 + a) & 1023] * 0.5f + trig_sin[(f * 17 + b) & 1023] * 0.3f + trig_sin[(f * 41 + a * 3) & 1023] * 0.2f;
    return v * 0.5f + 0.5f;
}

/* the lights' outer halos breathe and flicker: the baked glow is the steady core, this adds the live edge */
static void draw_flicker_glows(uint8_t *fb, const Game *g)
{
    int f = (int)(g->frame & 0xFFFFFF);
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            int dc = decor_map[ty][tx], r, ramp = 1, oy = 9;
            uint32_t h = hash2(tx, ty, 91);
            if (dc == D_LAMP) { r = 28; oy = 6; }
            else if (dc == D_PUMPKIN) r = 19;
            else if (dc == D_FLORA && !is_path[ty][tx]) { r = 14; ramp = 2; }
            else continue;
            float k = flick(f, h);
            if (dc == D_FLORA) k = 0.55f + 0.45f * trig_sin[(f * 4 + (int)(h % 1024)) & 1023];   /* plants pulse slowly */
            int rr = (int)((float)r * (0.8f + 0.35f * k) + 0.5f);
            gl2(fb, tx * TILE + 8, MAP_Y + ty * TILE + oy, rr, ramp);
        }
    {
        int kx = LV->wp[LV->nwp - 1][0] * TILE + 8, ky = MAP_Y + LV->wp[LV->nwp - 1][1] * TILE + 8;
        gl2(fb, kx, ky, (int)(34.0f * (0.8f + 0.35f * flick(f, 5))), 1);
        int px0 = LV->wp[0][0] * TILE + 8, py0 = MAP_Y + LV->wp[0][1] * TILE + 8;
        gl2(fb, px0, py0, (int)(30.0f * (0.85f + 0.15f * trig_sin[(f * 5) & 1023])), 2);
    }
}

static void draw_scenery(uint8_t *fb, const Game *g)
{
    int f = (int)(g->frame & 0xFFFFFF);
    /* soft ground shadows under the scenery */
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            int dc = decor_map[ty][tx];
            if (dc == D_TREE || dc == D_DEAD || dc == D_ROCK || dc == D_GRAVE || dc == D_PUMPKIN || dc == D_SKEL || dc == D_LAMP) {
                int cx = tx * TILE + (dc == D_TREE || dc == D_DEAD ? 10 : 9), cy = MAP_Y + ty * TILE + 15;
                shadow_ellipse(fb, cx, cy, dc == D_TREE || dc == D_DEAD ? 6 : 5);
            }
        }
    if (g->level > 0) draw_flicker_glows(fb, g);
    /* wind: a soft band of light drifts across the grass */
    for (int i = 0; i < wind_n; i++) {
        int x = wind_pt[i][0], y = wind_pt[i][1];
        if (((x * 2 + y - f) & 511) >= 14) continue;
        uint8_t *p = &fb[(MAP_Y + y) * SCREEN_W + x];
        if (*p == C_GRASS1) *p = (uint8_t)(g->level ? C_GRASS1 + 64 : C_LTGREEN);
    }
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            int dc = decor_map[ty][tx];
            if (dc == D_NONE) continue;
            int x0 = tx * TILE, y0 = MAP_Y + ty * TILE, ramp = decor_ramp[ty][tx] * 32;
            uint32_t h = hash2(tx, ty, 17);
            switch (dc) {
            case D_TREE: case D_DEAD: {
                int8_t off[16];
                float sw = trig_sin[(f * 5 + (int)(h % 1024)) & 1023] * (0.55f + 0.45f * trig_sin[(f * 2 + (int)(h % 997)) & 1023]);
                float amp = dc == D_TREE ? 1.6f : 2.1f;
                for (int j = 0; j < 16; j++) off[j] = j < 11 ? (int8_t)lrintf(sw * amp * (float)(11 - j) / 11.0f) : 0;
                sprite_live(fb, &SPR[dc == D_TREE ? S_TREE : S_DEAD], x0, y0, off, ramp);
                break;
            }
            case D_PUMPKIN: {
                uint32_t v = hash2(tx, ty, f / 6) % 10;
                sprite_live(fb, &SPR[S_PUMP0 + (v == 0 ? 2 : (v < 4 ? 1 : 0))], x0, y0, NULL, ramp);
                break;
            }
            case D_SKEL: {
                int lean = ((f / 50 + (int)(h & 1)) & 1);
                sprite_live(fb, &SPR[S_SKEL0 + lean], x0, y0, NULL, ramp);
                int ec = ((f / 9 + (int)(h % 11)) % 11 < 9) ? C_ORANGE : C_YELLOW;
                for (int k = 0; k < 2; k++) {                      /* glowing eyes */
                    px(fb, x0 + 6 + lean, y0 + 3 + k, ec); px(fb, x0 + 9 + lean, y0 + 3 + k, ec);
                }
                break;
            }
            case D_LAMP: {
                static const uint8_t pat[3][8] = {
                    {C_YELLOW, C_WHITE, C_WHITE, C_YELLOW, C_YELLOW, C_WHITE, C_WHITE, C_YELLOW},
                    {C_WHITE, C_WHITE, C_YELLOW, C_YELLOW, C_YELLOW, C_WHITE, C_WHITE, C_ORANGE},
                    {C_YELLOW, C_YELLOW, C_WHITE, C_WHITE, C_ORANGE, C_WHITE, C_YELLOW, C_YELLOW}
                };
                sprite_live(fb, &SPR[S_LAMP], x0, y0, NULL, ramp);
                const uint8_t *pt = pat[hash2(tx, ty, f / 4) % 3];
                for (int k = 0; k < 8; k++) px(fb, x0 + 6 + (k & 3), y0 + 3 + (k >> 2), pt[k] + ramp);
                break;
            }
            case D_FLORA: {                                         /* an occasional twinkle */
                int ph = (f + (int)(h % 110)) % 110;
                if (ph < 8) {
                    int sx = x0 + 3 + (int)((h >> 5) % 10), sy = y0 + 4 + (int)((h >> 11) % 7);
                    px(fb, sx, sy, C_WHITE);
                    if (ph > 1 && ph < 6) { px(fb, sx - 1, sy, C_WHITE); px(fb, sx + 1, sy, C_WHITE); px(fb, sx, sy - 1, C_WHITE); px(fb, sx, sy + 1, C_WHITE); }
                }
                break;
            }
            default: break;
            }
        }
}

static void draw_arcs(uint8_t *fb, const Game *g)
{
    for (int i = 0; i < MAX_ARC; i++) {
        const Arc *a = &g->ar[i];
        if (a->life <= 0) continue;
        int n = 5;
        float px0 = a->x0, py0 = a->y0, dx = a->x1 - a->x0, dy = a->y1 - a->y0, len = sqrtf(dx * dx + dy * dy) + 0.01f;
        for (int k = 1; k <= n; k++) {
            float t = (float)k / (float)n, off = k == n ? 0.0f : ((float)(hash2(a->seed, (int)(g->frame / 2), k) % 7) - 3.0f);
            float x1 = a->x0 + dx * t - dy / len * off, y1 = a->y0 + dy * t + dx / len * off;
            bolt_line(fb, (int)px0, (int)py0 + 1, (int)x1, (int)y1 + 1, C_PURPLE);
            bolt_line(fb, (int)px0, (int)py0, (int)x1, (int)y1, C_WHITE);
            px0 = x1; py0 = y1;
        }
    }
}

static void draw_world(uint8_t *fb, const Game *g)
{
    memcpy(fb + MAP_Y * SCREEN_W, g->bg, sizeof g->bg);
    draw_scenery(fb, g);
    blit_phase = (int)(g->frame >> 3);
    blit(fb, &SPR[S_PORTAL], LV->wp[0][0] * TILE, MAP_Y + LV->wp[0][1] * TILE, M_PULSE);
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
    draw_arcs(fb, g);
    if (g->level > 0) { draw_glows(fb, g); draw_bolt(fb, g); }
    for (int i = 0; i < MAX_PART; i++) {
        const Part *p = &g->pt[i];
        if (p->life > 0) rect(fb, (int)p->x, (int)p->y, p->size, p->size, p->col);
    }
    if (g->level > 0) draw_rain(fb, g);
    for (int i = 0; i < MAX_TEXT; i++) {
        const FText *t = &g->ft[i];
        if (t->life <= 0) continue;
        char b[16]; snprintf(b, sizeof b, "+%d", t->val);
        draw_text_sh(fb, (int)t->x - text_w(b, 1) / 2, (int)t->y, b, C_YELLOW, 1);
    }
}

/* zoom: magnify the map around the point the view is following (nearest-neighbour, so pixels stay crisp) */
static void zoom_map(uint8_t *fb, const Game *g)
{
    static uint8_t tmp[SCREEN_W * MAP_H * TILE];
    float z = g->zoom;
    if (z < 1.02f) return;
    memcpy(tmp, fb + MAP_Y * SCREEN_W, sizeof tmp);
    float ww = (float)SCREEN_W / z, wh = (float)(MAP_H * TILE) / z;
    float wx = g->zfx - ww * 0.5f, wy = g->zfy - wh * 0.5f;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;
    if (wx > (float)SCREEN_W - ww) wx = (float)SCREEN_W - ww;
    if (wy > (float)(MAP_H * TILE) - wh) wy = (float)(MAP_H * TILE) - wh;
    for (int y = 0; y < MAP_H * TILE; y++) {
        int sy = (int)(wy + (float)y / z);
        if (sy > MAP_H * TILE - 1) sy = MAP_H * TILE - 1;
        uint8_t *d = fb + (MAP_Y + y) * SCREEN_W;
        const uint8_t *r = tmp + sy * SCREEN_W;
        for (int x = 0; x < SCREEN_W; x++) {
            int sx = (int)(wx + (float)x / z);
            if (sx > SCREEN_W - 1) sx = SCREEN_W - 1;
            d[x] = r[sx];
        }
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
    draw_text_center(fb, 74, "ENDLESS WAVES - HOW FAR CAN YOU GO?", C_LIGHT, 1);
    /* mascots: a tower on the left, the boss on the right */
    blit_scaled(fb, &SPR[S_T_ARROW], 36, 20, 3);
    draw_orb(fb, TW_STORM, 2, 36 + 24, 20 + 15, (int)g->frame, 3, 3, 0);
    blit(fb, &SPR[S_BOSS], 248, 28 + (int)lrintf(sinf((float)g->frame * 0.06f) * 2.0f), M_NORMAL);

    panel(fb, 28, 98, 264, 80, C_GRAY);
    draw_text_center(fb, 106, "PRESS A TO START", ((g->frame >> 4) & 1) ? C_WHITE : C_YELLOW, 2);
    {
        char lb[48];
        snprintf(lb, sizeof lb, "< LEVEL %d  %s >", g->level + 1, LEVELS[g->level].name);
        draw_text_center(fb, 119, lb, g->level ? C_CYAN : C_LTGREEN, 1);
    }
    draw_text_center(fb, 128, "D-PAD MOVE   A BUILD/UPGRADE   B SELL", C_LIGHT, 1);
    draw_text_center(fb, 138, "L/R CHANGE TOWER   X CALL WAVE   Y FAST", C_LIGHT, 1);
    draw_text_center(fb, 148, "+ PAUSE   - SCREEN MODE   HOME EXIT", C_LIGHT, 1);
    if (best_wave[g->level] > 0) { char bb[32]; snprintf(bb, sizeof bb, "BEST WAVE %d", best_wave[g->level]); draw_text_center(fb, 162, bb, C_YELLOW, 1); }
    else draw_text_center(fb, 162, "CLASSIC CONTROLLER", C_CYAN, 1);
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
    if (g->state != ST_TITLE) zoom_map(fb, g);
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
        panel(fb, 70, 50, 180, 80, C_GRAY);
        draw_text_center(fb, 57, "PAUSED", C_YELLOW, 3);
        char it[24];
        for (int i = 0; i < 3; i++) {
            if (i == 0) snprintf(it, sizeof it, "RESUME");
            else if (i == 1) snprintf(it, sizeof it, "SOUND: %s", sound_on ? "ON" : "OFF");
            else snprintf(it, sizeof it, "QUIT TO TITLE");
            char line[32];
            snprintf(line, sizeof line, g->pause_sel == i ? "> %s <" : "%s", it);
            draw_text_center(fb, 83 + i * 12, line, g->pause_sel == i ? C_WHITE : C_LIGHT, 1);
        }
        draw_text_center(fb, 120, "UP/DOWN + A", C_DKGRAY, 1);
    } else if (g->state == ST_OVER || g->state == ST_WIN) {
        char b[48];
        int win = g->state == ST_WIN;
        darken(fb, 0, MAP_Y, SCREEN_W, MAP_H * TILE);
        panel(fb, 60, 50, 200, 84, win ? C_YELLOW : C_RED);
        int more = win && g->level < NUM_LEVELS - 1;
        draw_text_center(fb, 58, more ? "LEVEL CLEAR!" : (win ? "YOU WIN!" : "GAME OVER"), win ? C_YELLOW : C_RED, more ? 3 : 4);
        snprintf(b, sizeof b, "WAVE %d   KILLS %d", g->wave, g->kills);
        draw_text_center(fb, 88, b, C_WHITE, 1);
        if (g->new_best) draw_text_center(fb, 100, "NEW BEST WAVE!", ((g->frame >> 3) & 1) ? C_YELLOW : C_ORANGE, 1);
        else { snprintf(b, sizeof b, "BEST WAVE %d", best_wave[g->level]); draw_text_center(fb, 100, b, C_LIGHT, 1); }
        if (more) {
            snprintf(b, sizeof b, "NEXT: %s", LEVELS[g->level + 1].name);
            draw_text_center(fb, 110, b, C_LTGREEN, 1);
        }
        if (g->state_t > 1.2f && ((g->frame >> 4) & 1)) draw_text_center(fb, 121, "PRESS A", C_CYAN, 1);
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
