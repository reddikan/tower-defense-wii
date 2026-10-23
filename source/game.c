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
enum {
    C_BLACK, C_DKGRAY, C_GRAY, C_LIGHT, C_WHITE, C_RED, C_DKRED, C_ORANGE,
    C_YELLOW, C_GREEN, C_DKGREEN, C_LTGREEN, C_CYAN, C_BLUE, C_DKBLUE,
    C_PURPLE, C_PINK, C_BROWN, C_DKBROWN, C_TAN, C_GRASS1, C_GRASS2,
    C_DIRT1, C_DIRT2, C_SKY, C_ICE, C_DKPURPLE, C_SKIN, C_STONE, C_DKSTONE,
    C_TEAL, C_CREAM,
    /* shading colours: the ends of each material ramp */
    C_FOL0, C_FOL4, C_WOOD0, C_WOOD4, C_ROCK0, C_ROCK4, C_CRIM0, C_EMBER,
    C_VIO0, C_VIO4, C_NAVY, C_ZOM, C_ZOM0, C_GHO, C_GHO0, C_MINT
};

const uint8_t game_palette[PB][3] = {
    { 14,  12,  28}, { 44,  40,  68}, {110, 110, 140}, {205, 205, 225},
    {255, 255, 255}, {225,  45,  55}, {140,  25,  45}, {255, 140,  30},
    {255, 225,  70}, { 65, 200,  75}, { 30, 115,  55}, {150, 235,  95},
    { 70, 215, 235}, { 60, 110, 235}, { 30,  50, 140}, {175,  80, 225},
    {255, 120, 185}, {160, 100,  45}, { 95,  55,  32}, {225, 185, 115},
    { 88, 172,  62}, { 72, 150,  52}, {205, 165, 105}, {180, 138,  84},
    { 84, 146, 242}, {175, 238, 255}, { 95,  45, 145}, {255, 205, 155},
    {150, 152, 172}, { 92,  92, 118}, { 40, 150, 160}, {250, 240, 200},
    { 14,  62,  38}, {200, 245, 120}, { 52,  30,  28}, {240, 205, 140},
    { 58,  58,  84}, {210, 212, 228}, { 92,  18,  40}, {255, 185, 110},
    { 52,  24,  92}, {230, 175, 255}, { 18,  28,  86}, {128, 168, 112},
    { 70, 104,  76}, {228, 238, 255}, {140, 160, 212}, {130, 255, 205}
};

/* every colour -> a darker one (used for translucent shading and shadows) */
static const uint8_t DARK[PB] = {
    0, 0, 1, 2, 3, 6, 0, 18, 7, 10, 0, 9, 30, 14, 0, 26,
    15, 18, 0, 17, 21, 10, 23, 18, 14, 12, 0, 19, 29, 1, 10, 3,
    0, 11, 0, 19, 1, 28, 0, 7, 0, 15, 0, 44, 10, 46, 29, 12
};

/* DARK for any palette index: keeps the glow ramp (0 base, PB warm-lit, 2*PB cool-lit, ...) */
static inline uint8_t dark_of(uint8_t c) { int b = c / PB; return (uint8_t)(b * PB + DARK[c - b * PB]); }

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
/* h* functions take screen pixels (640x360); the others take logic    */
/* units (320x180) and fill HSxHS blocks                               */
/* ------------------------------------------------------------------ */
#define HMAP_Y (MAP_Y * HS)
#define HMAP_H (MAP_H * TILE * HS)

static inline void hpx(uint8_t *fb, int x, int y, int c)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) fb[y * SCREEN_W + x] = (uint8_t)c;
}

static void hrect(uint8_t *fb, int x, int y, int w, int h, int c)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    if (x1 <= x) return;
    for (int j = y; j < y1; j++) memset(fb + j * SCREEN_W + x, c, (size_t)(x1 - x));
}

static void hframe(uint8_t *fb, int x, int y, int w, int h, int c)
{
    hrect(fb, x, y, w, 1, c); hrect(fb, x, y + h - 1, w, 1, c);
    hrect(fb, x, y, 1, h, c); hrect(fb, x + w - 1, y, 1, h, c);
}

static inline void px(uint8_t *fb, int x, int y, int c) { hrect(fb, x * HS, y * HS, HS, HS, c); }
static void rect(uint8_t *fb, int x, int y, int w, int h, int c) { hrect(fb, x * HS, y * HS, w * HS, h * HS, c); }

/* translucent darkening of a region (logic units) */
static void darken(uint8_t *fb, int x, int y, int w, int h)
{
    int x0 = x * HS, y0 = y * HS, x1 = (x + w) * HS, y1 = (y + h) * HS;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    for (int j = y0; j < y1; j++) {
        uint8_t *r = fb + j * SCREEN_W;
        for (int i = x0; i < x1; i++) r[i] = dark_of(r[i]);
    }
}

static const uint8_t BAYER4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

/* which colours count as "ground" (so a shadow never dirties a sprite) */
static uint8_t GROUND[PB];
static void ground_init(void)
{
    static const uint8_t g[] = {C_GRASS1, C_GRASS2, C_DKGREEN, C_DIRT1, C_DIRT2, C_DKBROWN, C_FOL0, C_LTGREEN, C_TAN, C_DKBLUE, C_NAVY};
    for (unsigned i = 0; i < sizeof g; i++) GROUND[g[i]] = 1;
}

/* soft ground shadow: a dithered ellipse (screen pixels) that only darkens ground colours */
static void hshadow_h(uint8_t *fb, int h, int cx, int cy, int rx, int ry)
{
    int colterm[128];
    if (rx > 63) rx = 63;
    for (int i = -rx; i <= rx; i++) colterm[i + rx] = i * i * 20 / (rx * rx);
    for (int y = cy - ry; y <= cy + ry; y++) {
        if ((unsigned)y >= (unsigned)h) continue;
        int rowterm = (y - cy) * (y - cy) * 20 / (ry * ry);
        uint8_t *row = fb + y * SCREEN_W;
        for (int x = cx - rx; x <= cx + rx; x++) {
            if ((unsigned)x >= SCREEN_W) continue;
            int th = 20 - rowterm - colterm[x - cx + rx];            /* 0..20: fades towards the rim */
            if (th <= 0) continue;
            uint8_t *p = &row[x];
            int b = *p / PB, base = *p - b * PB;
            if (!GROUND[base]) continue;
            int bay = BAYER4[(y & 3) * 4 + (x & 3)];
            if (th > 5 + bay / 2) *p = dark_of(dark_of(*p));
            else if (th > bay / 2) *p = dark_of(*p);
        }
    }
}
static void hshadow(uint8_t *fb, int cx, int cy, int rx, int ry) { hshadow_h(fb, SCREEN_H, cx, cy, rx, ry); }

/*
 * Glow: palette entries PB.. are "warm-lit" copies of colours 0..PB-1, 2*PB.. "cool-lit" copies, and so on, so
 * lighting a pixel is just adding PB or 2*PB to its index. A dithered falloff makes a soft halo.
 * buf is a w=SCREEN_W image with h rows; all coordinates are screen pixels.
 */
static void glow_buf(uint8_t *buf, int h, int cx, int cy, int r, int ramp)
{
    int y0 = cy - r, y1 = cy + r;
    if (y0 < 0) y0 = 0;
    if (y1 >= h) y1 = h - 1;
    int r2 = r * r, full = ramp * PB, dim = (ramp + 2) * PB;
    int inv = (64 << 16) / (r2 + 1);
    for (int y = y0; y <= y1; y++) {
        int dy = y - cy, hw = (int)sqrtf((float)(r2 - dy * dy));
        int x0 = cx - hw, x1 = cx + hw;
        if (x0 < 0) x0 = 0;
        if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
        uint8_t *row = buf + y * SCREEN_W;
        const uint8_t *bay = &BAYER4[(y & 3) * 4];
        int dy2 = dy * dy;
        for (int x = x0; x <= x1; x++) {
            uint8_t c = row[x];
            if (c >= PB) continue;
            int dx = x - cx, t = (int)(((unsigned)(dx * dx + dy2) * (unsigned)inv) >> 16), b = bay[x & 3];
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

/* glow straight onto the screen's map rows; hires coordinates */
static void gl2h(uint8_t *fb, int cx, int cy, int r, int ramp)
{
    glow_buf(fb + HMAP_Y * SCREEN_W, HMAP_H, cx, cy - HMAP_Y, r, ramp);
}
/* the same in logic units */
static void gl2(uint8_t *fb, int cx, int cy, int r, int ramp) { gl2h(fb, cx * HS, cy * HS, r * HS, ramp); }

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
    int X = x * HS, Y = y * HS, W = w * HS, H = h * HS;
    hrect(fb, X, Y, W, H, C_BLACK);
    for (int j = 2; j < H - 2; j++)                                    /* a soft vertical gradient inside */
        hrect(fb, X + 2, Y + j, W - 4, 1, (j * 3 / H) == 0 ? C_DKGRAY : (j * 3 / H == 1 ? C_BLACK : C_BLACK));
    hframe(fb, X, Y, W, H, border);
    hframe(fb, X + 1, Y + 1, W - 2, H - 2, border);
    hframe(fb, X + 5, Y + 5, W - 10, H - 10, C_DKGRAY);
    hpx(fb, X, Y, C_BLACK); hpx(fb, X + W - 1, Y, C_BLACK); hpx(fb, X, Y + H - 1, C_BLACK); hpx(fb, X + W - 1, Y + H - 1, C_BLACK);
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
    ground_init();
    trig_ready = 1;
}

/* an expanding ring: logic-unit centre and radius, drawn 2 pixels thick at full resolution */
static void ring_outline(uint8_t *fb, int cx, int cy, float r, int c)
{
    float R = r * (float)HS;
    int n = (int)(R * 6.3f) + 12;
    for (int i = 0; i < n; i++) {
        int a = i * TRIG_N / n;
        int x = cx * HS + (int)lrintf(trig_cos[a] * R), y = cy * HS + (int)lrintf(trig_sin[a] * R);
        hrect(fb, x, y, 2, 2, c);
    }
}

static void dotted_circle(uint8_t *fb, int cx, int cy, float r, int c, int phase)
{
    float R = r * (float)HS;
    int n = (int)(2.0f * (float)M_PI * R / 7.0f);
    if (n < 8) n = 8;
    for (int i = 0; i < n; i++) {
        if (((i + phase) & 1) == 0) continue;
        int a = i * TRIG_N / n;
        int x = cx * HS + (int)lrintf(trig_cos[a] * R), y = cy * HS + (int)lrintf(trig_sin[a] * R);
        hrect(fb, x, y, 3, 3, c);
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
    draw_text_sh(fb, (LOGIC_W - text_w(t, s)) / 2, y, t, col, s);
}
/* ------------------------------------------------------------------ */
/* Sprites                                                             */
/* ------------------------------------------------------------------ */
#include "art.inc"

/* ------------------------------------------------------------------ */
/* Map / path setup + background pre-render                            */
/* ------------------------------------------------------------------ */
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

/* ---- the pre-rendered map: 640 x 320, built once per level ---- */
static void bgpx(uint8_t *bg, int x, int y, int c)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < HMAP_H) bg[y * SCREEN_W + x] = (uint8_t)c;
}

static void bg_blit(uint8_t *bg, const Sprite *s, int x, int y)
{
    for (int j = 0; j < s->h; j++)
        for (int i = 0; i < s->w; i++) {
            int c = s->d[j * s->w + i];
            if (c != 255) bgpx(bg, x + i, y + j, c);
        }
}

static void bg_shadow(uint8_t *bg, int cx, int cy, int rx, int ry) { hshadow_h(bg, HMAP_H, cx, cy, rx, ry); }

/* smooth value noise */
static float vnoise(float x, float y, int seed)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    fx = fx * fx * (3.0f - 2.0f * fx); fy = fy * fy * (3.0f - 2.0f * fy);
    float a = (float)(hash2(xi, yi, seed) & 1023), b = (float)(hash2(xi + 1, yi, seed) & 1023);
    float c = (float)(hash2(xi, yi + 1, seed) & 1023), d = (float)(hash2(xi + 1, yi + 1, seed) & 1023);
    return ((a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy) * (1.0f / 1023.0f);
}
static float fbm2(float x, float y, int seed) { return vnoise(x, y, seed) * 0.6f + vnoise(x * 2.3f, y * 2.3f, seed + 1) * 0.4f; }

RAMP(RP_GRASS, C_DKGREEN, C_GRASS2, C_GRASS1, C_LTGREEN);
RAMP(RP_DIRT,  C_DKBROWN, C_DIRT2, C_DIRT1, C_TAN);
RAMP(RP_WATER, C_NAVY, C_DKBLUE, C_BLUE, C_SKY);

/* distance (pixels) from a point to a tile rectangle (0 inside) */
static float rect_dist(int x, int y, int tx, int ty)
{
    float dx = 0, dy = 0;
    int x0 = tx * TILE * HS, y0 = ty * TILE * HS, x1 = x0 + TILE * HS - 1, y1 = y0 + TILE * HS - 1;
    if (x < x0) dx = (float)(x0 - x); else if (x > x1) dx = (float)(x - x1);
    if (y < y0) dy = (float)(y0 - y); else if (y > y1) dy = (float)(y - y1);
    return sqrtf(dx * dx + dy * dy);
}

/* signed distance to the road: negative on it, positive off it */
static float path_sdf(int x, int y)
{
    int tx = x / (TILE * HS), ty = y / (TILE * HS);
    int inside = is_path[ty][tx];
    float best = 1e9f;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            int nx = tx + i, ny = ty + j;
            nx = nx < 0 ? 0 : (nx >= MAP_W ? MAP_W - 1 : nx);
            ny = ny < 0 ? 0 : (ny >= MAP_H ? MAP_H - 1 : ny);
            if (is_path[ny][nx] == inside) continue;
            float d = rect_dist(x, y, tx + i, ty + j);
            if (d < best) best = d;
        }
    if (best > 1e8f) best = 24.0f;
    return inside ? -best : best;
}

static void cv_puddle(float cx, float cy, float rx, float ry, int seed)
{
    for (int y = (int)(cy - ry - 1); y <= (int)(cy + ry + 1); y++)
        for (int x = (int)(cx - rx - 1); x <= (int)(cx + rx + 1); x++) {
            float dx = ((float)x + 0.5f - cx) / rx, dy = ((float)y + 0.5f - cy) / ry, d2 = dx * dx + dy * dy;
            if (d2 > 1.0f) continue;
            float t = 0.22f + 0.28f * (1.0f - dy) * 0.5f + 0.1f * sinf(dx * 7.0f + (float)seed);
            if (d2 > 0.8f) t = 0.0f;                                                              /* muddy rim */
            if (dy < -0.25f && dy > -0.55f && dx > -0.7f && dx < 0.1f) t = 0.95f;                  /* sky reflection */
            cput(x, y, rpick(RP_WATER, RP_WATER_N, t, x, y));
        }
}

/* skull and crossed bones lying in the grass */
static void cv_bones(uint32_t h)
{
    float sh = (float)(h % 5) - 2.0f;
    s_cap(8.0f + sh, 24.0f, 1.1f, 25.0f + sh, 29.0f, 1.1f, RP_BONE, RP_BONE_N, 0.3f);
    s_cap(25.0f + sh, 24.0f, 1.1f, 8.0f + sh, 29.0f, 1.1f, RP_BONE, RP_BONE_N, 0.3f);
    s_ell(7.0f + sh, 23.5f, 1.5f, 1.4f, RP_BONE, RP_BONE_N, 0.3f); s_ell(26.0f + sh, 23.5f, 1.5f, 1.4f, RP_BONE, RP_BONE_N, 0.3f);
    s_ell(7.0f + sh, 29.5f, 1.5f, 1.4f, RP_BONE, RP_BONE_N, 0.3f); s_ell(26.0f + sh, 29.5f, 1.5f, 1.4f, RP_BONE, RP_BONE_N, 0.3f);
    s_ell(16.5f + sh, 20.0f, 5.6f, 5.0f, RP_BONE, RP_BONE_N, 0.3f);
    s_ell(16.5f + sh, 24.0f, 3.4f, 2.0f, RP_BONE, RP_BONE_N, 0.1f);
    s_fell(14.2f + sh, 20.4f, 1.6f, 1.9f, C_BLACK); s_fell(18.8f + sh, 20.4f, 1.6f, 1.9f, C_BLACK);
    float ns[6] = {15.8f + sh, 23.0f, 17.2f + sh, 23.0f, 16.5f + sh, 21.4f};
    s_fpoly(ns, 3, C_BLACK);
    s_outline(C_BLACK);
}

/* glowing plants: blue flowers (moor), toadstools (marsh), crystals (peak) */
static void cv_flora(int level, int tx, int ty)
{
    for (int k = 0; k < 3; k++) {
        uint32_t h = hash2(tx, ty, 300 + k);
        float ox = 6.0f + (float)(h % 20), oy = 10.0f + (float)((h >> 8) % 16);
        if (level == 1) {                                   /* flower */
            s_cap(ox, oy + 5.0f, 0.5f, ox, oy, 0.5f, RP_FOL, RP_FOL_N - 1, 0.0f);
            s_fell(ox - 3.5f, oy + 3.0f, 2.2f, 0.9f, C_DKGREEN);
            for (int p = 0; p < 5; p++) {
                float a = (float)p * 1.2566f - 1.57f;
                s_ell(ox + cosf(a) * 2.3f, oy - 1.0f + sinf(a) * 2.3f, 1.6f, 1.6f, RP_ICE, RP_ICE_N, 0.0f);
            }
            s_fell(ox, oy - 1.0f, 1.2f, 1.2f, C_WHITE);
        } else if (level == 2) {                            /* toadstool */
            s_cap(ox, oy + 4.5f, 1.4f, ox, oy, 1.1f, RP_BONE, RP_BONE_N, 0.2f);
            s_ell(ox, oy - 0.5f, 4.4f, 3.0f, RP_ARC, RP_ARC_N, 0.4f);
            s_fell(ox - 1.6f, oy - 1.6f, 0.9f, 0.7f, C_WHITE); s_fell(ox + 1.7f, oy - 0.6f, 0.7f, 0.6f, C_WHITE);
        } else {                                            /* crystal cluster */
            float c1[6] = {ox - 1.6f, oy + 3.0f, ox + 1.6f, oy + 3.0f, ox, oy - 5.0f};
            float c2[6] = {ox - 4.2f, oy + 3.0f, ox - 1.6f, oy + 3.0f, ox - 3.4f, oy - 1.5f};
            float c3[6] = {ox + 1.8f, oy + 3.0f, ox + 4.2f, oy + 3.0f, ox + 3.6f, oy - 2.0f};
            s_poly(c2, 3, RP_STORM, RP_STORM_N, 0.7f, 0.3f, 0.5f); s_poly(c3, 3, RP_STORM, RP_STORM_N, 0.7f, 0.3f, 0.5f);
            s_poly(c1, 3, RP_STORM, RP_STORM_N, 1.0f, 0.4f, 0.6f);
            s_fline(ox - 0.3f, oy - 3.0f, ox - 0.3f, oy + 1.0f, 0.3f, C_VIO4);
        }
    }
    s_outline(C_BLACK);
}

/* wildflowers for the meadow */
static void cv_meadow_flowers(int tx, int ty)
{
    static const uint8_t cols[3] = {C_PINK, C_YELLOW, C_WHITE};
    for (int k = 0; k < 3; k++) {
        uint32_t h = hash2(tx, ty, 300 + k);
        float ox = 5.0f + (float)(h % 22), oy = 8.0f + (float)((h >> 8) % 18);
        int c = cols[(h >> 16) % 3];
        s_cap(ox, oy + 5.5f, 0.5f, ox, oy, 0.5f, RP_FOL, RP_FOL_N - 1, 0.0f);
        s_fell(ox - 2.8f, oy + 3.2f, 2.0f, 0.8f, C_GREEN);
        for (int p = 0; p < 5; p++) {
            float a = (float)p * 1.2566f - 1.57f;
            s_fell(ox + cosf(a) * 2.2f, oy + sinf(a) * 2.2f, 1.5f, 1.5f, c);
            s_fell(ox + cosf(a) * 2.2f - 0.4f, oy + sinf(a) * 2.2f - 0.4f, 0.6f, 0.6f, C_WHITE);
        }
        s_fell(ox, oy, 1.1f, 1.1f, C_ORANGE);
    }
}

/* the haunted house at the exit (its windows flicker at runtime) */
static void bg_keep(uint8_t *bg)
{
    int x = LV->wp[LV->nwp - 1][0] * TILE * HS, y = LV->wp[LV->nwp - 1][1] * TILE * HS;
    bg_blit(bg, &HOUSE, x - HOUSE_OX, y - HOUSE_OY);
}

static void build_bg(Game *g)
{
    uint8_t *bg = g->bg;
    int lv = g->level, storm = lv > 0, sd = lv * 1000;
    static Sprite tmp;
    for (int y = 0; y < HMAP_H; y++)
        for (int x = 0; x < SCREEN_W; x++) {
            float sdf = path_sdf(x, y) + (fbm2((float)x * 0.07f, (float)y * 0.07f, sd + 5) - 0.5f) * 5.0f;
            float n1 = fbm2((float)x * 0.035f, (float)y * 0.035f, sd + 11), n2 = vnoise((float)x * 0.35f, (float)y * 0.35f, sd + 13);
            int c;
            if (sdf < 0.0f) {                                                              /* road */
                float depth = -sdf, t = 0.40f + 0.34f * n1 + 0.14f * (n2 - 0.5f);
                if (storm) {                                                               /* wet flagstones */
                    int cell = (x + ((y / 11) & 1) * 6) / 12, row = y / 11;
                    int ex = (x + ((y / 11) & 1) * 6) % 12, ey = y % 11;
                    float v = (float)(hash2(cell, row, sd + 21) & 255) * (1.0f / 255.0f);
                    t = 0.38f + 0.3f * v + 0.18f * n1;
                    if (ex == 0 || ey == 0) t -= 0.28f;
                    else if (ex == 1 || ey == 1) t += 0.08f;
                }
                if (depth < 4.0f) t -= (4.0f - depth) * 0.1f;                              /* dark worn edge */
                c = rpick(RP_DIRT, RP_DIRT_N, t, x, y);
            } else {                                                                      /* grass */
                float t = 0.30f + 0.38f * n1 + 0.2f * (n2 - 0.5f) + 0.1f * (fbm2((float)x * 0.012f, (float)y * 0.012f, sd + 17) - 0.5f);
                if (sdf < 6.0f) t -= (6.0f - sdf) * 0.045f;                               /* occlusion beside the road */
                if (storm) t *= 0.82f;
                c = rpick(RP_GRASS, RP_GRASS_N, t, x, y);
            }
            bg[y * SCREEN_W + x] = (uint8_t)c;
        }

    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            int x0 = tx * TILE * HS, y0 = ty * TILE * HS, dc = decor_at(tx, ty);
            cv_begin(&tmp, 32, 32);
            if (is_path[ty][tx]) {
                int np = storm ? 2 : 6;
                for (int k = 0; k < np; k++) {                                              /* pebbles */
                    uint32_t h = hash2(tx, ty, sd + 50 + k);
                    float ox = 4.0f + (float)(h % 24), oy = 4.0f + (float)((h >> 8) % 24), r = 1.0f + (float)((h >> 16) % 3) * 0.5f;
                    s_ell(ox, oy, r * 1.3f, r, RP_STONE, RP_STONE_N, 0.2f);
                }
                if (storm) {
                    uint32_t hp = hash2(tx, ty, sd + 90);
                    if (hp % 100 < 30) cv_puddle(10.0f + (float)((hp >> 8) % 12), 12.0f + (float)((hp >> 16) % 8), 6.5f, 3.6f, (int)hp);
                } else {
                    for (int k = 0; k < 2; k++) {                                           /* tufts poking through */
                        uint32_t h = hash2(tx, ty, 60 + k);
                        float ox = 6.0f + (float)(h % 20), oy = 8.0f + (float)((h >> 8) % 18);
                        for (int b = -1; b <= 1; b++) s_fline(ox + (float)b * 1.2f, oy, ox + (float)b * 1.8f, oy - 2.6f, 0.4f, b ? C_GRASS2 : C_GRASS1);
                    }
                }
                bg_blit(bg, &tmp, x0, y0);
                continue;
            }
            /* grass: blades, and whatever stands here */
            int nb = storm ? 9 : 13;
            for (int k = 0; k < nb; k++) {
                uint32_t h = hash2(tx, ty, sd + 100 + k);
                float ox = 2.0f + (float)(h % 28), oy = 5.0f + (float)((h >> 8) % 26), len = 2.6f + (float)((h >> 16) % 3);
                float lean = ((float)((h >> 20) % 5) - 2.0f) * 0.5f;
                int bc = ((h >> 24) & 3) == 0 ? (storm ? C_GRASS1 : C_LTGREEN) : ((h >> 24) & 3) == 1 ? C_DKGREEN : C_GRASS2;
                s_fline(ox, oy, ox + lean, oy - len, 0.42f, bc);
                s_fline(ox + 1.0f, oy + 0.2f, ox + 1.0f + lean * 0.6f, oy - len * 0.7f, 0.35f, C_DKGREEN);
            }
            if (storm) {
                uint32_t hg = hash2(tx, ty, sd + 250);
                if (hg % 100 < 25) s_fell(4.0f + (float)(hg >> 8) * 0.0f + (float)((hg >> 8) % 24), 6.0f + (float)((hg >> 16) % 22), 0.9f, 0.6f, C_SKY);   /* wet gleam */
                if (dc == D_NONE && (hash2(tx, ty, sd + 91) % 100) < 7)
                    cv_puddle(9.0f + (float)(hash2(tx, ty, sd + 92) % 12), 14.0f + (float)(hash2(tx, ty, sd + 93) % 8), 7.0f, 3.8f, (int)hg);
            }
            bg_blit(bg, &tmp, x0, y0);

            switch (dc) {
            case D_TREE: bg_shadow(bg, x0 + 24, y0 + 30, 17, 5); break;                      /* the tree itself is drawn live (it sways) */
            case D_DEAD: bg_shadow(bg, x0 + 23, y0 + 30, 15, 4); break;
            case D_PUMPKIN: bg_shadow(bg, x0 + 20, y0 + 28, 11, 3); break;
            case D_SKEL: bg_shadow(bg, x0 + 19, y0 + 29, 12, 3); break;
            case D_LAMP: bg_shadow(bg, x0 + 19, y0 + 31, 10, 3); break;
            case D_ROCK: bg_shadow(bg, x0 + 19, y0 + 28, 13, 4); bg_blit(bg, &SPR[S_ROCK], x0, y0); break;
            case D_GRAVE: bg_shadow(bg, x0 + 19, y0 + 28, 13, 4); bg_blit(bg, &SPR[S_GRAVE], x0, y0); break;
            case D_BONES: cv_begin(&tmp, 32, 32); cv_bones(hash2(tx, ty, 700)); bg_blit(bg, &tmp, x0, y0); break;
            case D_FLORA:
                cv_begin(&tmp, 32, 32);
                if (storm) cv_flora(lv, tx, ty); else cv_meadow_flowers(tx, ty);
                bg_blit(bg, &tmp, x0, y0);
                break;
            default: break;
            }
        }

    bg_shadow(bg, LV->wp[LV->nwp - 1][0] * TILE * HS + 20, LV->wp[LV->nwp - 1][1] * TILE * HS + 30, 22, 5);
    bg_keep(bg);

    if (storm) {
        /* light sources are baked in: lamps and the house glow warm, plants and the portal glow cool */
        for (int ty = 0; ty < MAP_H; ty++)
            for (int tx = 0; tx < MAP_W; tx++) {
                int dc = decor_at(tx, ty), x0 = tx * TILE * HS, y0 = ty * TILE * HS;
                if (dc == D_LAMP)  glow_buf(bg, HMAP_H, x0 + 16, y0 + 12, 32, 1);
                if (dc == D_PUMPKIN) glow_buf(bg, HMAP_H, x0 + 16, y0 + 20, 22, 1);
                if (dc == D_FLORA && !is_path[ty][tx]) glow_buf(bg, HMAP_H, x0 + 16, y0 + 18, 18, 2);
            }
        glow_buf(bg, HMAP_H, LV->wp[LV->nwp - 1][0] * TILE * HS + 16, LV->wp[LV->nwp - 1][1] * TILE * HS + 16, 44, 1);
        glow_buf(bg, HMAP_H, LV->wp[0][0] * TILE * HS + 16, LV->wp[0][1] * TILE * HS + 16, 40, 2);
        bg_keep(bg);                                      /* the house keeps its own colours inside the glow */

        /* remember whether each live-drawn scenery tile sits in a glow, so its sprite can be lit the same way */
        for (int ty = 0; ty < MAP_H; ty++)
            for (int tx = 0; tx < MAP_W; tx++) {
                int v = bg[(ty * TILE * HS + 24) * SCREEN_W + tx * TILE * HS + 16];
                decor_ramp[ty][tx] = (uint8_t)(v / PB);   /* 0 unlit, 1 warm, 2 cool, 3 faint warm, 4 faint cool */
            }
    }

    /* vignette: the edges of the map fall into darkness (soft in the meadow, deep in the storm) */
    for (int y = 0; y < HMAP_H; y++)
        for (int x = 0; x < SCREEN_W; x++) {
            float nx = (float)(x - SCREEN_W / 2) / (float)(SCREEN_W / 2), ny = (float)(y - HMAP_H / 2) / (float)(HMAP_H / 2);
            float v = storm ? nx * nx * 0.8f + ny * ny * 0.9f - 0.55f : nx * nx * 0.7f + ny * ny * 0.8f - 0.75f;
            if (v <= 0) continue;
            uint8_t *p = &bg[y * SCREEN_W + x];
            int th = (int)(v * (storm ? 20.0f : 16.0f));
            if (BAYER4[(y & 3) * 4 + (x & 3)] < th) *p = dark_of(*p);
            if (storm && v > 0.5f && BAYER4[((y + 2) & 3) * 4 + ((x + 1) & 3)] < (int)((v - 0.5f) * 20.0f)) *p = dark_of(*p);
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
static const uint8_t STORM_SCALE[PB] = {
    100, 76, 70, 84, 92, 92, 75, 94, 96, 85, 60, 88, 92, 85, 62, 88,
    92, 58, 60, 70, 70, 70, 70, 70, 62, 92, 70, 85, 80, 74, 60, 78,
    60, 80, 60, 70, 70, 80, 70, 88, 70, 88, 70, 80, 70, 84, 76, 88
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
        for (int c = 0; c < PB; c++) {
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
                PAL_LV[lv][PB + c][k] = clamp8((rgb[k] + (t->warm[k] - rgb[k]) * 0.38f) * 1.1f);
                PAL_LV[lv][2 * PB + c][k] = clamp8((rgb[k] + (t->cool[k] - rgb[k]) * 0.38f) * 1.1f);
                PAL_LV[lv][3 * PB + c][k] = clamp8((rgb[k] + (t->warm[k] - rgb[k]) * 0.17f) * 1.04f);
                PAL_LV[lv][4 * PB + c][k] = clamp8((rgb[k] + (t->cool[k] - rgb[k]) * 0.17f) * 1.04f);
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
    int keep_lite = g->lite;
    memset(g, 0, sizeof *g);
    g->lite = keep_lite;
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
    if (g->state != ST_TITLE) {                      /* R / L triggers: zoom in/out, centred on the cursor */
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
static int tower_sprite(int type, int level) { return S_TOWER + type * 3 + level; }

/* the palette offset of whatever light the ground at this spot receives (screen pixels), so sprites pick up the glow too */
static int ambient_at(const Game *g, int x, int y)
{
    if (g->level == 0) return 0;
    y -= HMAP_Y;
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= HMAP_H) return 0;
    return g->bg[y * SCREEN_W + x] / PB * PB;
}

/* a thick line between screen pixels */
static void hline(uint8_t *fb, int x0, int y0, int x1, int y1, int c, int th)
{
    int dx = abs(x1 - x0), dy = abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    for (int guard = 0; guard < 4096; guard++) {
        hrect(fb, x0 - th / 2, y0 - th / 2, th, th, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

/* a flat triangle */
static void ftri(uint8_t *fb, int x0, int y0, int x1, int y1, int x2, int y2, int c)
{
    int ymin = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2), ymax = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    for (int y = ymin; y <= ymax; y++) {
        int xa = 100000, xb = -100000;
        int ex[3][4] = {{x0, y0, x1, y1}, {x1, y1, x2, y2}, {x2, y2, x0, y0}};
        for (int e = 0; e < 3; e++) {
            int ax = ex[e][0], ay = ex[e][1], bx = ex[e][2], by = ex[e][3];
            if ((ay > y) == (by > y)) continue;
            int xx = ax + (bx - ax) * (y - ay) / (by - ay);
            if (xx < xa) xa = xx;
            if (xx > xb) xb = xx;
        }
        if (xb >= xa) hrect(fb, xa, y, xb - xa + 1, 1, c);
    }
}

/* ---- the magical weapons: a glowing orb floating above each pedestal, with orbiting motes and a gentle pulse ---- */
/* cx,cy: centre of the orb's resting spot in screen pixels; k: size multiplier (the title screen draws it big) */
static void draw_orb(uint8_t *fb, int type, int level, int cx, int cy, int frame, int seed, int k, int dim)
{
    static const uint8_t *const RPS[TW_COUNT] = {RP_ARC, RP_FIRE, RP_ICE, RP_STORM};
    static const int RPN[TW_COUNT] = {RP_ARC_N, RP_FIRE_N, RP_ICE_N, RP_STORM_N};
    const uint8_t *rp = RPS[type];
    int rn = RPN[type];
    float ph = (float)seed * 1.7f;
    cy += (int)lrintf(sinf((float)frame * 0.07f + ph) * 2.2f * (float)k);               /* the orb bobs up and down */
    int R = (level == 2 ? 8 : 6) * k;
    int pulse = ((frame / 7 + seed) % 5) == 0;
    float pf = 0.5f + 0.5f * sinf((float)frame * 0.12f + ph);
    uint32_t h = hash2(seed, frame / 3, type);
    if (!dim) {                                                  /* things that flicker out of the top of the orb */
        if (type == TW_CANNON) {                                 /* flame tongues */
            for (int i = 0; i < 3; i++) {
                uint32_t hh = hash2(seed + i, frame / 2, 5);
                int bx = cx + (i - 1) * R / 2, ht = R + R / 2 + (int)(hh % (uint32_t)(R + 1));
                int lean = ((int)((hh >> 8) % 5) - 2) * k;
                ftri(fb, bx - R / 3, cy - R / 3, bx + R / 3, cy - R / 3, bx + lean, cy - ht, C_ORANGE + blit_add);
                ftri(fb, bx - R / 5, cy - R / 3, bx + R / 5, cy - R / 3, bx + lean / 2, cy - ht * 2 / 3, C_YELLOW + blit_add);
            }
        }
    }
    static uint8_t SPH[4][33 * 33];                              /* per-pixel code of a sphere of radius R: 0 outside, 1 shine, 2 rim, else brightness+3 */
    static int sph_ready[4];
    int si = R <= 6 ? 0 : (R <= 8 ? 1 : (R <= 12 ? 2 : 3));
    int sR = si == 0 ? 6 : (si == 1 ? 8 : (si == 2 ? 12 : 16));
    if (R > 16) R = 16;
    if (!sph_ready[si]) {
        for (int y = -sR; y <= sR; y++)
            for (int x = -sR; x <= sR; x++) {
                float dx = (float)x / (float)sR, dy = (float)y / (float)sR, d2 = dx * dx + dy * dy;
                int code = 0;
                if (d2 <= 1.0f) {
                    float nz = sqrtf(1.0f - d2), sx = dx + 0.38f, sy = dy + 0.42f;
                    if (sx * sx + sy * sy < 0.045f) code = 1;
                    else if (d2 > 0.84f) code = 2;
                    else code = 3 + (int)(250.0f * (0.16f + 0.74f * powf(nz, 1.3f)) / 255.0f * 1.0f);
                }
                SPH[si][(y + sR) * 33 + x + sR] = (uint8_t)code;
            }
        sph_ready[si] = 1;
    }
    int tadd = (pulse ? 38 : 0) + (int)(pf * 18.0f);
    for (int y = -R; y <= R; y++)
        for (int x = -R; x <= R; x++) {
            int c;
            if (type == TW_FROST) {                              /* a faceted crystal */
                float ax = (float)abs(x) / (float)R, ay = (float)abs(y) / (float)(R * 1.15f);
                float d = ax + ay * 0.85f;
                if (d > 1.0f) continue;
                int fx = x < 0, fy = y < 0;
                int f = fx && fy ? 4 : (fy ? 3 : (fx ? 2 : 1));
                if (d > 0.86f) c = RPS[type][0];
                else if (abs(x) <= k) c = pulse ? C_WHITE : C_ICE;
                else c = rp[f];
                if (pulse && f >= 3) c = C_WHITE;
            } else {
                int code = SPH[si][(y + sR) * 33 + x + sR];
                if (!code) continue;
                if (code == 1) c = C_WHITE;
                else if (code == 2) c = rp[0];
                else {
                    int t = code - 3 + tadd;
                    t = t < 0 ? 0 : (t > 255 ? 255 : t);
                    int v = t * (rn - 1), i2 = v >> 8;
                    c = i2 >= rn - 1 ? rp[rn - 1] : (((v & 255) >> 4) > BAYER4[((cy + y) & 3) * 4 + ((cx + x) & 3)] ? rp[i2 + 1] : rp[i2]);
                }
            }
            if (dim) c = DIM_MAP[c]; else c += blit_add;
            hpx(fb, cx + x, cy + y, c);
        }
    if (dim) return;
    if (type == TW_STORM) {                                      /* crackles of lightning */
        for (int a = 0; a < 3; a++) {
            uint32_t hh = hash2(seed + a, frame / 2, 9);
            if ((hh & 3) == 0) continue;
            float an = (float)(hh % 628) * 0.01f, len = (float)R * (1.0f + (float)((hh >> 10) % 8) * 0.12f);
            int x1 = cx + (int)(cosf(an) * len), y1 = cy + (int)(sinf(an) * len);
            int xm = cx + (int)(cosf(an + 0.4f) * len * 0.6f), ym = cy + (int)(sinf(an + 0.4f) * len * 0.6f);
            hline(fb, cx + (int)(cosf(an) * (float)R * 0.7f), cy + (int)(sinf(an) * (float)R * 0.7f), xm, ym, C_PINK + blit_add, 1);
            hline(fb, xm, ym, x1, y1, C_WHITE, 1);
        }
    } else if (type == TW_FROST && (h % 7) == 0) {               /* a falling snowflake */
        hrect(fb, cx + ((int)((h >> 4) % 13) - 6) * k, cy + R + 3 * k, 2, 2, C_WHITE);
    } else if (type == TW_ARROW && (h % 5) == 0) {               /* a drifting leaf */
        hrect(fb, cx + ((int)((h >> 4) % 15) - 7) * k, cy + R + (int)((h >> 9) % 5) * k, 3, 2, C_LTGREEN + blit_add);
    }
    int n = 1 + level;                                           /* orbiting motes */
    for (int m = 0; m < n; m++) {
        float a = (float)frame * 0.09f * (1.0f + 0.35f * (float)m) + (float)m * 6.2832f / (float)n + ph;
        int mx = cx + (int)lrintf(cosf(a) * (float)(R + 5 * k)), my = cy + (int)lrintf(sinf(a) * (float)(R + 5 * k) * 0.45f);
        int mc = (m & 1) ? rp[rn - 1] : C_WHITE;
        hrect(fb, mx - k, my - k, 2 * k + 1, 2 * k + 1, mc + blit_add);
        if (k == 1) hpx(fb, mx - 2, my, rp[rn - 2] + blit_add), hpx(fb, mx + 2, my, rp[rn - 2] + blit_add);
    }
}

/* the ground shadow under every tower */
static void shadow_towers(uint8_t *fb, const Game *g)
{
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++)
            if (g->towers[ty][tx].used) hshadow(fb, tx * TILE * HS + 19, HMAP_Y + ty * TILE * HS + 28, 15, 5);
}

static void draw_tower(uint8_t *fb, const Game *g, const Tower *t, int tx, int ty)
{
    int x = tx * TILE * HS, y = HMAP_Y + ty * TILE * HS;
    blit_add = ambient_at(g, x + 16, y + 24);
    blit(fb, &SPR[tower_sprite(t->type, t->level)], x, y, M_NORMAL);
    draw_orb(fb, t->type, t->level, x + 16, y + 9, (int)g->frame, tx * 7 + ty * 3, 1, 0);
    blit_add = 0;
    int n = t->level + 1, start = 16 - (n * 6 - 2) / 2;
    for (int i = 0; i < n; i++) {                                 /* level pips */
        int px0 = x + start + i * 6, py0 = y + 29;
        hrect(fb, px0, py0, 4, 3, C_ORANGE); hrect(fb, px0, py0, 4, 2, C_YELLOW); hpx(fb, px0, py0, C_CREAM);
    }
    if (t->flash > 0) {                                           /* muzzle flash: a bright starburst on the orb */
        int mx = x + 16, my = y + 9;
        int c = t->type == TW_FROST ? C_ICE : (t->type == TW_CANNON ? C_YELLOW : (t->type == TW_STORM ? C_PINK : C_LTGREEN));
        hline(fb, mx - 10, my, mx + 10, my, c, 2); hline(fb, mx, my - 10, mx, my + 10, c, 2);
        hline(fb, mx - 6, my - 6, mx + 6, my + 6, c, 1); hline(fb, mx - 6, my + 6, mx + 6, my - 6, c, 1);
        hrect(fb, mx - 3, my - 3, 7, 7, C_WHITE);
    }
    if (t->level == 2) {                                          /* level-3 sparkle */
        uint32_t h = hash2(tx, ty, (int)(g->frame / 48));
        int ph = (int)(g->frame % 48);
        if (ph < 8) {
            int sx = x + 6 + (int)(h % 20), sy = y + 2 + (int)((h >> 8) % 12);
            hrect(fb, sx, sy, 2, 2, C_WHITE);
            if (ph > 1 && ph < 6) { hline(fb, sx - 4, sy, sx + 5, sy, C_YELLOW, 1); hline(fb, sx, sy - 4, sx, sy + 5, C_YELLOW, 1); }
        }
    }
}

static int enemy_frame(const Game *g, const Enemy *e)
{
    int speed = (e->type == EN_BAT || e->type == EN_CAT || e->type == EN_WITCH) ? 2 : 3;
    return (int)(((g->frame >> speed) + e->uid) & 3);
}

static void enemy_origin(const Game *g, const Enemy *e, int *hx, int *hy)
{
    const Sprite *s = &ESPR[e->type][0];
    int bob = 0;
    switch (e->type) {
    case EN_BAT:   bob = (int)lrintf(sinf((float)g->frame * 0.15f + (float)e->uid) * 3.0f); break;
    case EN_WITCH: bob = (int)lrintf(sinf((float)g->frame * 0.1f + (float)e->uid) * 4.0f); break;
    case EN_GHOST: bob = (int)lrintf(sinf((float)g->frame * 0.07f + (float)e->uid) * 5.0f); break;
    default: break;
    }
    *hx = (int)lrintf(e->x * (float)HS) - s->w / 2;
    *hy = (int)lrintf(e->y * (float)HS) - (e->type == EN_BOSS ? 40 : 17) + bob - (is_flyer(e->type) ? 6 : 0);
}

static void shadow_enemies(uint8_t *fb, const Game *g)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive) continue;
        int fl = is_flyer(e->type);
        int cx = (int)lrintf(e->x * (float)HS) + 2, cy = (int)lrintf(e->y * (float)HS) + (e->type == EN_BOSS ? 20 : 10) + (fl ? 8 : 0);
        hshadow(fb, cx, cy, e->type == EN_BOSS ? 26 : (fl ? 7 : 11), e->type == EN_BOSS ? 7 : (fl ? 2 : 4));
    }
}

static void draw_enemy(uint8_t *fb, const Game *g, const Enemy *e)
{
    int mode = M_NORMAL, fr = enemy_frame(g, e);
    if (e->slow_t > 0) mode = M_FROZEN;
    if (e->hitflash > 0) mode = M_FLASH;
    else if (e->type == EN_GHOST && ((g->frame / 5 + e->uid) % 4) == 0) mode = M_GHOST;     /* flickers in and out */
    const Sprite *s = &ESPR[e->type][fr];
    int x, y;
    enemy_origin(g, e, &x, &y);
    blit_add = ambient_at(g, x + s->w / 2, y + s->h * 3 / 4);
    blit(fb, s, x, y, mode);
    blit_add = 0;
    if (e->type == EN_BOSS && mode != M_FLASH) {                  /* the eyes burn: pulse between red and yellow */
        float bob = sinf((float)fr * (float)M_PI * 0.5f) * 0.9f;
        int ec = ((g->frame / 8) & 1) ? C_YELLOW : C_ORANGE, ey = y + (int)(26.0f + bob);
        hrect(fb, x + 22, ey - 1, 3, 3, ec); hrect(fb, x + 40, ey - 1, 3, 3, ec);
        hpx(fb, x + 23, ey - 1, C_WHITE); hpx(fb, x + 41, ey - 1, C_WHITE);
    }
    if (e->hp < e->maxhp) {                                       /* health bar */
        int w = e->type == EN_BOSS ? 52 : 20, bx = (int)lrintf(e->x * (float)HS) - w / 2, by = y - (e->type == EN_BOSS ? 3 : 2);
        int fillw = (int)((float)w * e->hp / e->maxhp + 0.5f);
        if (fillw < 1) fillw = 1;
        float f = e->hp / e->maxhp;
        hrect(fb, bx - 1, by - 1, w + 2, 6, C_BLACK);
        hrect(fb, bx, by, w, 4, C_DKRED);
        int c1 = f > 0.5f ? C_GREEN : (f > 0.25f ? C_YELLOW : C_RED), c2 = f > 0.5f ? C_LTGREEN : (f > 0.25f ? C_CREAM : C_ORANGE);
        hrect(fb, bx, by, fillw, 4, c1); hrect(fb, bx, by, fillw, 1, c2);
    }
}

static void draw_projectile(uint8_t *fb, const Proj *p)
{
    int x = (int)lrintf(p->x * (float)HS), y = (int)lrintf(p->y * (float)HS);
    if (p->type == TW_ARROW) {
        float len = sqrtf(p->vx * p->vx + p->vy * p->vy);
        if (len < 1) len = 1;
        float ux = p->vx / len, uy = p->vy / len;
        int tx0 = x - (int)(ux * 12.0f), ty0 = y - (int)(uy * 12.0f);
        hline(fb, tx0, ty0, x, y, C_TAN, 2);                                           /* shaft */
        hline(fb, tx0, ty0, tx0 + (int)(ux * 3.0f), ty0 + (int)(uy * 3.0f), C_LTGREEN, 2);   /* fletching */
        hrect(fb, x - 1, y - 1, 3, 3, C_WHITE);                                         /* head */
        hpx(fb, x + (int)(ux * 2.0f), y + (int)(uy * 2.0f), C_LIGHT);
    } else if (p->type == TW_CANNON) {
        for (int j = -4; j <= 4; j++)
            for (int i = -4; i <= 4; i++) {
                float dx = (float)i / 4.4f, dy = (float)j / 4.4f, d2 = dx * dx + dy * dy;
                if (d2 > 1.0f) continue;
                int c = rpick(RP_IRON, RP_IRON_N, light_t(dx, dy, sqrtf(1.0f - d2), 0.4f), x + i, y + j);
                if ((dx + 0.4f) * (dx + 0.4f) + (dy + 0.45f) * (dy + 0.45f) < 0.06f) c = C_GRAY;
                hpx(fb, x + i, y + j, c);
            }
    } else {
        for (int j = -4; j <= 4; j++)
            for (int i = -4; i <= 4; i++) {
                int d = abs(i) + abs(j);
                if (d > 4) continue;
                hpx(fb, x + i, y + j, d <= 1 ? C_WHITE : (d <= 2 ? C_ICE : (d <= 3 ? C_CYAN : C_BLUE)));
            }
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
    hrect(fb, 0, 0, SCREEN_W, 36, C_BLACK);
    hrect(fb, 0, 0, SCREEN_W, 2, C_DKGRAY);
    for (int j = 2; j < 14; j++) hrect(fb, 0, j, SCREEN_W, 1, j < 6 ? C_DKGRAY : C_BLACK);
    hrect(fb, 0, 36, SCREEN_W, 2, C_DKGRAY);
    hrect(fb, 0, 38, SCREEN_W, 2, C_GRAY);
    /* lives (heart blinks when low) */
    if (g->lives > 5 || (g->frame & 16)) blit(fb, &SPR[S_HEART], 8, 5, M_NORMAL);
    snprintf(b, sizeof b, "%d", g->lives);
    draw_text(fb, 14, 4, b, (g->hurt_t > 0 || g->lives <= 5) ? C_RED : C_WHITE, 1);
    /* gold */
    blit(fb, &SPR[S_COIN], 74, 5, M_NORMAL);
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
        int x = (251 + i * 17) * HS - 1;
        int afford = g->gold >= TW_COST[i];
        blit(fb, &SPR[tower_sprite(i, 0)], x, 3, afford ? M_NORMAL : M_DIM);
        draw_orb(fb, i, 0, x + 16, 12, (int)g->frame, i, 1, afford ? 0 : 1);
        if (i == g->sel) {
            hframe(fb, x - 1, 1, 34, 36, C_YELLOW); hframe(fb, x, 2, 32, 34, C_YELLOW);
            if (g->frame & 16) hframe(fb, x - 3, 0, 38, 38, C_ORANGE);
        }
    }
}

static void draw_cursor(uint8_t *fb, const Game *g)
{
    int x = g->cx * TILE * HS, y = HMAP_Y + g->cy * TILE * HS;
    const Tower *t = &g->towers[g->cy][g->cx];
    int buildable = game_is_buildable(g, g->cx, g->cy);
    int col = t->used ? C_YELLOW : (buildable && g->gold >= TW_COST[g->sel] ? C_LTGREEN : C_RED);
    if (buildable) blit(fb, &SPR[tower_sprite(g->sel, 0)], x, y, M_GHOST);   /* placement preview */
    int o = ((g->frame >> 4) & 1) ? 2 : 0, L = 11;
    int x0 = x - o, y0 = y - o, x1 = x + TILE * HS - 1 + o, y1 = y + TILE * HS - 1 + o;
    for (int th = 0; th < 3; th++) {
        int c = th == 2 ? C_WHITE : col;
        hrect(fb, x0, y0 + th, L, 1, c); hrect(fb, x0 + th, y0, 1, L, c);
        hrect(fb, x1 - L + 1, y0 + th, L, 1, c); hrect(fb, x1 - th, y0, 1, L, c);
        hrect(fb, x0, y1 - th, L, 1, c); hrect(fb, x0 + th, y1 - L + 1, 1, L, c);
        hrect(fb, x1 - L + 1, y1 - th, L, 1, c); hrect(fb, x1 - th, y1 - L + 1, 1, L, c);
    }
    /* range preview */
    int type = t->used ? t->type : g->sel, lvl = t->used ? t->level : 0;
    if (t->used || buildable)
        dotted_circle(fb, g->cx * TILE + 8, MAP_Y + g->cy * TILE + 8, TW_RANGE[type][lvl], t->used ? C_YELLOW : C_WHITE, (int)(g->frame >> 3));
}

/* the haunted house: eerie windows that flicker, an attic light, and a wisp of smoke from the chimney */
static void draw_keep_flag(uint8_t *fb, const Game *g)
{
    int tx = LV->wp[LV->nwp - 1][0] * TILE * HS, ty = LV->wp[LV->nwp - 1][1] * TILE * HS;
    int x = tx, y = HMAP_Y + ty;
    int f = (int)(g->frame & 0xFFFFFF);
    if (g->level) {                                              /* the house keeps its own colours inside the flickering glow */
        for (int j = 0; j < HOUSE_H; j++) {
            int yy = ty - HOUSE_OY + j;
            if (yy < 0 || yy >= HMAP_H) continue;
            for (int i = 0; i < HOUSE_W; i++) {
                int xx = tx - HOUSE_OX + i;
                if (xx < 0 || xx >= SCREEN_W) continue;
                if (HOUSE.d[j * HOUSE_W + i] != 255) fb[(HMAP_Y + yy) * SCREEN_W + xx] = g->bg[yy * SCREEN_W + xx];
            }
        }
    }
    static const uint8_t lit[4] = {C_YELLOW, C_ORANGE, C_CREAM, C_YELLOW};
    for (int w = 0; w < 2; w++) {
        int wx = x + (w ? HOUSE_WIN2_X : HOUSE_WIN1_X), wy = y + HOUSE_WIN_Y;
        uint32_t h = hash2(w, 3, f / 5);
        int c = (h % 9 == 0) ? C_DKBROWN : lit[h % 4];           /* sometimes the light blinks out */
        hrect(fb, wx, wy, HOUSE_WIN_W + 2, HOUSE_WIN_H + 1, c);
        if (c != C_DKBROWN) {
            hrect(fb, wx + 1, wy + 1, HOUSE_WIN_W, HOUSE_WIN_H - 1, c == C_ORANGE ? C_YELLOW : C_CREAM);
            hrect(fb, wx + 1, wy + 1, 2, 2, C_WHITE);
        }
        hrect(fb, wx + 3, wy, 1, HOUSE_WIN_H + 1, C_WOOD0); hrect(fb, wx, wy + 3, HOUSE_WIN_W + 2, 1, C_WOOD0);
    }
    if (((f / 40) & 3) == 0) {                                   /* a ghostly face in the attic window */
        int ax = x + 15, ay = y + 3;
        hrect(fb, ax - 1, ay - 1, 4, 5, C_WHITE);
        hrect(fb, ax - 1, ay + 1, 1, 1, C_CYAN); hrect(fb, ax + 2, ay + 1, 1, 1, C_CYAN);
    }
    for (int k = 0; k < 4; k++) {                                /* smoke */
        int t = (f / 3 + k * 11) % 44;
        int sx = x + 25 + (int)lrintf(trig_sin[(t * 24 + k * 200) & 1023] * 2.5f) + t / 5, sy = y - 4 - t * 4 / 5, r = 1 + t / 11;
        if (sy < HMAP_Y || t >= 40) continue;
        int c = t < 18 ? C_LIGHT : C_GRAY;
        for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r && ((i + j + t) & 1) == 0) hpx(fb, sx + i, sy + j, c);
    }
}

/* ---- storm effects: glow, lightning bolt, rain ---- */
static void gl(uint8_t *fb, int cx, int cy, int r, int ramp, int *budget)
{
    if (*budget <= 0) return;
    (*budget)--;
    gl2(fb, cx, cy, r, ramp);
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

/* a jagged bolt from the top of the map to the strike point, with a halo; flickers twice */
static void draw_bolt(uint8_t *fb, const Game *g)
{
    if (g->strike_amp < 1.0f) return;
    float s = g->strike_t;
    if (!(s < 0.05f || (s >= 0.09f && s < 0.16f))) return;
    enum { N = 14 };
    int xs[N + 1], ys[N + 1], budget = 40;
    for (int i = 0; i <= N; i++) {
        int jit = (i == 0 || i == N) ? 0 : (int)(hash2(i, g->bolt_seed, 5) % 15) - 7;
        xs[i] = (g->bolt_x0 + (g->bolt_x1 - g->bolt_x0) * i / N + jit) * HS;
        ys[i] = (MAP_Y + (g->bolt_y1 - MAP_Y) * i / N) * HS;
    }
    int bx[5], by[5];                                   /* a short side branch */
    int dir = (g->bolt_seed & 1) ? 1 : -1;
    bx[0] = xs[5]; by[0] = ys[5];
    for (int i = 1; i < 5; i++) {
        bx[i] = bx[i - 1] + dir * (8 + (int)(hash2(i, g->bolt_seed, 9) % 10));
        by[i] = by[i - 1] + 14 + (int)(hash2(i, g->bolt_seed, 11) % 8);
    }
    for (int i = 0; i <= N; i += 2) gl2h(fb, xs[i], ys[i], 16, 2);
    gl2h(fb, xs[N], ys[N], 30, 2);
    (void)budget;
    for (int i = 0; i < N; i++) {
        hline(fb, xs[i], ys[i], xs[i + 1], ys[i + 1], C_CYAN, 5);
        hline(fb, xs[i], ys[i], xs[i + 1], ys[i + 1], C_ICE, 3);
        hline(fb, xs[i], ys[i], xs[i + 1], ys[i + 1], C_WHITE, 1);
    }
    for (int i = 0; i < 4; i++) { hline(fb, bx[i], by[i], bx[i + 1], by[i + 1], C_ICE, 2); hline(fb, bx[i], by[i], bx[i + 1], by[i + 1], C_WHITE, 1); }
}

/* rain is stateless: every drop's position is a function of the frame number */
static void rain_px(uint8_t *fb, int x, int y, int c)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < (unsigned)HMAP_H) fb[(HMAP_Y + y) * SCREEN_W + x] = (uint8_t)c;
}

static void draw_rain(uint8_t *fb, const Game *g)
{
    const int P = HMAP_H + 32;
    long long f = (long long)g->frame;
    for (int i = 0; i < 64; i++) {
        uint32_t h = hash2(i, 5, 77);
        int layer = (i % 4 == 3) ? 2 : (i & 1);              /* mostly faint far/mid drops, a few near ones */
        int sp = 6 + layer * 4, len = 6 + layer * 4;
        int x0 = (int)(h % 720), y0 = (int)((h >> 9) % (uint32_t)P);
        int y = (int)((y0 + f * sp) % P) - 16;
        int x = (int)((((x0 - f * sp / 3) % 720) + 720) % 720) - 40;
        int c = layer == 2 ? C_LIGHT : C_GRAY;
        for (int k = 0; k < len; k++) {
            if (layer == 0 && (k & 1)) continue;             /* far drops are dotted: more transparent */
            rain_px(fb, x + k / 3, y - k, c);
        }
    }
    for (int j = 0; j < (g->lite ? 0 : 14); j++) {            /* faint ripples where drops land */
        uint32_t h = hash2(j, 9, 31);
        long long t = f + (long long)(h % 30);
        int ph = (int)(t % 30);
        if (ph >= 10) continue;
        uint32_t pos = hash2(j, (int)(t / 30), 33);
        int x = (int)(pos % 640), y = (int)((pos >> 9) % HMAP_H), r = 2 + ph;
        rain_px(fb, x - r, y, C_GRAY); rain_px(fb, x + r, y, C_GRAY);
        rain_px(fb, x - r + 1, y + 1, C_GRAY); rain_px(fb, x + r - 1, y + 1, C_GRAY);
        rain_px(fb, x - r / 2, y + 2, C_GRAY); rain_px(fb, x + r / 2, y + 2, C_GRAY);
    }
}

/* ---- living scenery: swaying trees, flickering flames, idle skeletons, twinkles, wind over the grass ---- */
static void sprite_live(uint8_t *fb, const Sprite *s, int x, int y, const int8_t *off, int ramp)
{
    for (int j = 0; j < s->h; j++) {
        int o = off ? off[j] : 0;
        for (int i = 0; i < s->w; i++) {
            int c = s->d[j * s->w + i];
            if (c != 255) hpx(fb, x + i + o, y + j, c + ramp);
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
            int dc = decor_map[ty][tx], r, ramp = 1, oy = 12;
            uint32_t h = hash2(tx, ty, 91);
            if (dc == D_LAMP) { r = 56; oy = 12; }
            else if (dc == D_PUMPKIN) { r = 38; oy = 20; }
            else if (dc == D_FLORA && !is_path[ty][tx]) { r = 28; ramp = 2; oy = 18; }
            else continue;
            float k = flick(f, h);
            if (dc == D_FLORA) k = 0.55f + 0.45f * trig_sin[(f * 4 + (int)(h % 1024)) & 1023];   /* plants pulse slowly */
            int rr = (int)((float)r * (0.8f + 0.35f * k) + 0.5f);
            gl2h(fb, tx * TILE * HS + 16, HMAP_Y + ty * TILE * HS + oy, rr, ramp);
        }
    {
        int kx = LV->wp[LV->nwp - 1][0] * TILE * HS + 16, ky = HMAP_Y + LV->wp[LV->nwp - 1][1] * TILE * HS + 16;
        gl2h(fb, kx, ky, (int)(68.0f * (0.8f + 0.35f * flick(f, 5))), 1);
        int px0 = LV->wp[0][0] * TILE * HS + 16, py0 = HMAP_Y + LV->wp[0][1] * TILE * HS + 16;
        gl2h(fb, px0, py0, (int)(60.0f * (0.85f + 0.15f * trig_sin[(f * 5) & 1023])), 2);
    }
}

static uint8_t lighten_ground(uint8_t c)
{
    int b = c / PB, base = c - b * PB;
    switch (base) {
    case C_DKGREEN: base = C_GRASS2; break;
    case C_GRASS2:  base = C_GRASS1; break;
    case C_GRASS1:  base = C_LTGREEN; break;
    default: return c;
    }
    return (uint8_t)(b * PB + base);
}

static void draw_scenery(uint8_t *fb, const Game *g)
{
    int f = (int)(g->frame & 0xFFFFFF);
    if (g->level > 0 && !g->lite) draw_flicker_glows(fb, g);
    /* wind: a soft band of light drifts across the grass */
    for (int i = 0; i < wind_n; i++) {
        int x = wind_pt[i][0] * HS, y = wind_pt[i][1] * HS;
        if (((wind_pt[i][0] * 2 + wind_pt[i][1] - f) & 511) >= 14) continue;
        for (int d = 0; d < 3; d++) {
            uint8_t *p = &fb[(HMAP_Y + y + d) * SCREEN_W + x];
            *p = lighten_ground(*p);
            p[1] = lighten_ground(p[1]);
        }
    }
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            int dc = decor_map[ty][tx];
            if (dc == D_NONE) continue;
            int x0 = tx * TILE * HS, y0 = HMAP_Y + ty * TILE * HS, ramp = decor_ramp[ty][tx] * PB;
            uint32_t h = hash2(tx, ty, 17);
            switch (dc) {
            case D_TREE: case D_DEAD: {
                int8_t off[52];
                float sw = trig_sin[(f * 5 + (int)(h % 1024)) & 1023] * (0.55f + 0.45f * trig_sin[(f * 2 + (int)(h % 997)) & 1023]);
                float amp = dc == D_TREE ? 3.2f : 4.2f;
                for (int j = 0; j < 52; j++) off[j] = j < 40 ? (int8_t)lrintf(sw * amp * (float)(40 - j) / 40.0f) : 0;
                sprite_live(fb, &SPR[dc == D_TREE ? S_TREE : S_DEAD], x0 - 4, y0 - 20, off, ramp);
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
                    int ex = x0 + (k ? 18 : 13) + lean;
                    hrect(fb, ex, y0 + 9, 2, 2, ec); hpx(fb, ex, y0 + 9, C_YELLOW);
                }
                break;
            }
            case D_LAMP: {
                sprite_live(fb, &SPR[S_LAMP], x0, y0, NULL, ramp);
                float k = flick(f, h);
                int fh = (int)(4.0f + 3.0f * k), fw = (int)(2.0f + 1.5f * k);
                for (int j = -fh; j <= fh; j++)                        /* the flame */
                    for (int i = -fw - 1; i <= fw + 1; i++) {
                        float ex = (float)i / (float)(fw + 1), ey = (float)j / (float)fh;
                        float d = ex * ex + ey * ey * (j < 0 ? 0.8f : 1.2f);
                        if (d > 1.0f) continue;
                        hpx(fb, x0 + 16 + i, y0 + 12 + j, d < 0.25f ? C_WHITE : (d < 0.6f ? C_YELLOW : C_ORANGE));
                    }
                break;
            }
            case D_FLORA: {                                         /* an occasional twinkle */
                int ph = (f + (int)(h % 110)) % 110;
                if (ph < 8) {
                    int sx = x0 + 6 + (int)((h >> 5) % 20), sy = y0 + 8 + (int)((h >> 11) % 14);
                    hrect(fb, sx, sy, 2, 2, C_WHITE);
                    if (ph > 1 && ph < 6) { hline(fb, sx - 3, sy, sx + 4, sy, C_WHITE, 1); hline(fb, sx, sy - 3, sx, sy + 4, C_WHITE, 1); }
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
        int n = 6;
        float px0 = a->x0 * (float)HS, py0 = a->y0 * (float)HS, dx = (a->x1 - a->x0) * (float)HS, dy = (a->y1 - a->y0) * (float)HS, len = sqrtf(dx * dx + dy * dy) + 0.01f;
        for (int k = 1; k <= n; k++) {
            float t = (float)k / (float)n, off = k == n ? 0.0f : (((float)(hash2(a->seed, (int)(g->frame / 2), k) % 11) - 5.0f) * 1.2f);
            float x1 = a->x0 * (float)HS + dx * t - dy / len * off, y1 = a->y0 * (float)HS + dy * t + dx / len * off;
            hline(fb, (int)px0, (int)py0, (int)x1, (int)y1, C_PURPLE, 4);
            hline(fb, (int)px0, (int)py0, (int)x1, (int)y1, C_PINK, 2);
            hline(fb, (int)px0, (int)py0, (int)x1, (int)y1, C_WHITE, 1);
            px0 = x1; py0 = y1;
        }
    }
}

static void draw_world(uint8_t *fb, const Game *g)
{
    memcpy(fb + HMAP_Y * SCREEN_W, g->bg, sizeof g->bg);
    shadow_towers(fb, g);
    if (!g->lite) shadow_enemies(fb, g);
    draw_scenery(fb, g);
    blit(fb, &SPR[S_PORTAL0 + ((g->frame >> 3) & 3)], LV->wp[0][0] * TILE * HS, HMAP_Y + LV->wp[0][1] * TILE * HS, M_NORMAL);
    draw_keep_flag(fb, g);
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++)
            if (g->towers[ty][tx].used) draw_tower(fb, g, &g->towers[ty][tx], tx, ty);
    for (int i = 0; i < MAX_ENEMIES; i++) if (g->en[i].alive) draw_enemy(fb, g, &g->en[i]);
    int halos = g->lite ? 0 : 40;
    for (int ty = 0; ty < MAP_H && halos > 0; ty++)            /* every orb spills a soft light of its own colour */
        for (int tx = 0; tx < MAP_W; tx++) {
            const Tower *t = &g->towers[ty][tx];
            if (!t->used || halos <= 0) continue;
            halos--;
            float pulse = 0.85f + 0.15f * trig_sin[((int)g->frame * 6 + tx * 97 + ty * 53) & 1023];
            gl2h(fb, tx * TILE * HS + 16, HMAP_Y + ty * TILE * HS + 12, (int)((t->level == 2 ? 22.0f : 17.0f) * pulse), t->type == TW_CANNON ? 1 : 2);
        }
    for (int i = 0; i < MAX_RING; i++) {
        const Ring *r = &g->rg[i];
        if (r->life <= 0) continue;
        float t = 1.0f - r->life / r->max_life;
        ring_outline(fb, (int)r->x, (int)r->y, r->maxr * (0.25f + 0.75f * sqrtf(t)), t > 0.6f ? DARK[r->col] : r->col);
    }
    for (int i = 0; i < MAX_PROJ; i++) if (g->pr[i].alive) draw_projectile(fb, &g->pr[i]);
    draw_arcs(fb, g);
    if (g->level > 0) { if (!g->lite) draw_glows(fb, g); draw_bolt(fb, g); }
    for (int i = 0; i < MAX_PART; i++) {
        const Part *p = &g->pt[i];
        if (p->life > 0) { int sz = p->size * HS; if (sz > 2) sz--; hrect(fb, (int)(p->x * (float)HS), (int)(p->y * (float)HS), sz, sz, p->col); }
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
    static uint8_t tmp[SCREEN_W * HMAP_H];
    static int xmap[SCREEN_W];
    float z = g->zoom;
    if (z < 1.02f) return;
    memcpy(tmp, fb + HMAP_Y * SCREEN_W, sizeof tmp);
    float ww = (float)SCREEN_W / z, wh = (float)HMAP_H / z;
    float wx = g->zfx * (float)HS - ww * 0.5f, wy = (g->zfy - (float)MAP_Y) * (float)HS - wh * 0.5f;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;
    if (wx > (float)SCREEN_W - ww) wx = (float)SCREEN_W - ww;
    if (wy > (float)HMAP_H - wh) wy = (float)HMAP_H - wh;
    for (int x = 0; x < SCREEN_W; x++) { int sx = (int)(wx + (float)x / z); xmap[x] = sx > SCREEN_W - 1 ? SCREEN_W - 1 : sx; }
    for (int y = 0; y < HMAP_H; y++) {
        int sy = (int)(wy + (float)y / z);
        if (sy > HMAP_H - 1) sy = HMAP_H - 1;
        uint8_t *d = fb + (HMAP_Y + y) * SCREEN_W;
        const uint8_t *r = tmp + sy * SCREEN_W;
        for (int x = 0; x < SCREEN_W; x++) d[x] = r[xmap[x]];
    }
}

static void shake_map(uint8_t *fb, int dx, int dy)
{
    static uint8_t tmp[SCREEN_W * HMAP_H];
    memcpy(tmp, fb + HMAP_Y * SCREEN_W, sizeof tmp);
    for (int y = 0; y < HMAP_H; y++) {
        int sy = y - dy; if (sy < 0) sy = 0; if (sy >= HMAP_H) sy = HMAP_H - 1;
        for (int x = 0; x < SCREEN_W; x++) {
            int sx = x - dx; if (sx < 0) sx = 0; if (sx >= SCREEN_W) sx = SCREEN_W - 1;
            fb[(HMAP_Y + y) * SCREEN_W + x] = tmp[sy * SCREEN_W + sx];
        }
    }
}

static void draw_title(uint8_t *fb, const Game *g)
{
    draw_world(fb, g);
    hrect(fb, 0, 0, SCREEN_W, HMAP_Y, C_BLACK);

    /* logo */
    panel(fb, 28, 4, 264, 84, C_YELLOW);
    static const uint8_t cols1[5] = {C_RED, C_ORANGE, C_YELLOW, C_LTGREEN, C_CYAN};
    static const uint8_t cols2[7] = {C_BLUE, C_PURPLE, C_PINK, C_RED, C_ORANGE, C_YELLOW, C_LTGREEN};
    const char *w1 = "TOWER", *w2 = "DEFENSE";
    int x1 = (LOGIC_W - text_w(w1, 5)) / 2, x2 = (LOGIC_W - text_w(w2, 5)) / 2;
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
    blit_scaled(fb, &SPR[tower_sprite(TW_STORM, 2)], 68, 40, 2);
    draw_orb(fb, TW_STORM, 2, 68 + 32, 40 + 18, (int)g->frame, 3, 2, 0);
    blit(fb, &ESPR[EN_BOSS][(g->frame >> 3) & 3], 496, 54 + (int)lrintf(sinf((float)g->frame * 0.06f) * 4.0f), M_NORMAL);

    panel(fb, 28, 98, 264, 80, C_GRAY);
    draw_text_center(fb, 106, "PRESS A TO START", ((g->frame >> 4) & 1) ? C_WHITE : C_YELLOW, 2);
    {
        char lb[48];
        snprintf(lb, sizeof lb, "< LEVEL %d  %s >", g->level + 1, LEVELS[g->level].name);
        draw_text_center(fb, 119, lb, g->level ? C_CYAN : C_LTGREEN, 1);
    }
    draw_text_center(fb, 128, "D-PAD MOVE   A BUILD/UPGRADE   B SELL", C_LIGHT, 1);
    draw_text_center(fb, 138, "ZL/ZR TOWER   L/R ZOOM   X WAVE   Y FAST", C_LIGHT, 1);
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
        int amp = g->shake_t > 0.25f ? 4 : 2;
        int dx = (int)(hash2((int)g->frame, 1, 7) % (uint32_t)(2 * amp + 1)) - amp;
        int dy = (int)(hash2((int)g->frame, 2, 9) % (uint32_t)(2 * amp + 1)) - amp;
        shake_map(fb, dx, dy);
    }
    if (g->hurt_t > 0 && (g->frame & 2)) { hframe(fb, 0, HMAP_Y, SCREEN_W, HMAP_H, C_RED); hframe(fb, 1, HMAP_Y + 1, SCREEN_W - 2, HMAP_H - 2, C_RED); hframe(fb, 2, HMAP_Y + 2, SCREEN_W - 4, HMAP_H - 4, C_RED); }
    draw_hud(fb, g);

    if (g->banner_t > 0 && g->state == ST_PLAY) {
        int y = 78 - (g->banner_t > 1.7f ? (int)((g->banner_t - 1.7f) * 40) : 0);
        darken(fb, 0, y - 5, LOGIC_W, 27);
        draw_text_center(fb, y, g->banner, g->banner_col, 3);
    }
    if (g->state == ST_PAUSE) {
        darken(fb, 0, MAP_Y, LOGIC_W, MAP_H * TILE);
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
        darken(fb, 0, MAP_Y, LOGIC_W, MAP_H * TILE);
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
    for (int t = 0; t < EN_COUNT; t++)
        for (int f = 0; f < NF; f++) {
            const Sprite *s = &ESPR[t][f];
            int x = 6 + (t == EN_BOSS ? 0 : f * 36), y = t == EN_BOSS ? 250 : 4 + (t > EN_BOSS ? t - 1 : t) * 34;
            if (t == EN_BOSS) x = 6 + f * 68;
            blit(fb, s, x, y, M_NORMAL);
            if (t != EN_BOSS) blit(fb, s, x + 150, y, M_FROZEN);
        }
    for (int i = 0; i < S_COUNT; i++) {
        int x = 330 + (i % 8) * 38, y = 4 + (i / 8) * 40;
        if (i == S_TREE || i == S_DEAD) { blit(fb, &SPR[i], x, y, M_NORMAL); continue; }
        blit(fb, &SPR[i], x, y, M_NORMAL);
    }
    blit(fb, &HOUSE, 330, 150, M_NORMAL);
}
