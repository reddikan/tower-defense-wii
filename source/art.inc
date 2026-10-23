/*
 * art.inc - all of the game's pictures, generated in code at start-up (included by game.c).
 *
 * Every sprite is built from lit, shaded primitives (spheres, tapered capsules, polygons) using a small
 * software rasteriser: each pixel's brightness comes from a light at the top-left, is dithered between the
 * colours of a material ramp, and gets a bounce light on its shadow side and a thin ink outline. That gives the
 * soft, rounded, cartoon-3D look at 640x360 while staying a plain palette image.
 */

typedef struct { int w, h; uint8_t d[64 * 64]; } Sprite;
#define NF 4                                   /* animation frames per monster */

enum { S_TOWER = 0,                            /* 12 pedestals: S_TOWER + type * 3 + tier */
       S_TREE = 12, S_DEAD, S_ROCK, S_GRAVE, S_LAMP, S_SKEL0, S_SKEL1, S_PUMP0, S_PUMP1, S_PUMP2,
       S_PORTAL0, S_PORTAL1, S_PORTAL2, S_PORTAL3, S_HEART, S_COIN, S_COUNT };
static Sprite SPR[S_COUNT];
static Sprite HOUSE;
static void art_house(Sprite *s);
static Sprite ESPR[EN_COUNT][NF];
static int sprites_ready;

static uint32_t hash2(int x, int y, int s)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* ---- material ramps, dark -> light ---- */
#define RAMP(name, ...) static const uint8_t name[] = {__VA_ARGS__}; enum { name##_N = sizeof name }
RAMP(RP_SLIME,  C_FOL0, C_DKGREEN, C_GREEN, C_LTGREEN, C_MINT);
RAMP(RP_BAT,    C_VIO0, C_DKPURPLE, C_PURPLE, C_VIO4);
RAMP(RP_STONE,  C_ROCK0, C_DKSTONE, C_STONE, C_ROCK4);
RAMP(RP_ZOM,    C_FOL0, C_ZOM0, C_ZOM, C_LTGREEN);
RAMP(RP_CLOTH,  C_WOOD0, C_DKBROWN, C_BROWN, C_TAN);
RAMP(RP_PANTS,  C_NAVY, C_DKBLUE, C_DKPURPLE, C_PURPLE);
RAMP(RP_PUMP,   C_WOOD0, C_DKBROWN, C_BROWN, C_ORANGE, C_EMBER);
RAMP(RP_GHOST,  C_DKSTONE, C_GHO0, C_GHO, C_WHITE);
RAMP(RP_CAT,    C_BLACK, C_DKGRAY, C_VIO0, C_DKPURPLE, C_PURPLE);
RAMP(RP_CLOAK,  C_BLACK, C_VIO0, C_DKPURPLE, C_PURPLE);
RAMP(RP_GOLD,   C_DKBROWN, C_BROWN, C_ORANGE, C_YELLOW, C_CREAM);
RAMP(RP_BONE,   C_STONE, C_LIGHT, C_CREAM, C_WHITE);
RAMP(RP_FOL,    C_FOL0, C_DKGREEN, C_GREEN, C_LTGREEN, C_FOL4);
RAMP(RP_WOOD,   C_WOOD0, C_DKBROWN, C_BROWN, C_TAN);
RAMP(RP_DEAD,   C_BLACK, C_WOOD0, C_DKBROWN, C_BROWN);
RAMP(RP_ICE,    C_NAVY, C_DKBLUE, C_BLUE, C_CYAN, C_ICE);
RAMP(RP_FIRE,   C_DKRED, C_RED, C_ORANGE, C_YELLOW, C_CREAM);
RAMP(RP_ARC,    C_FOL0, C_DKGREEN, C_GREEN, C_LTGREEN, C_FOL4);
RAMP(RP_STORM,  C_VIO0, C_DKPURPLE, C_PURPLE, C_PINK, C_VIO4);
RAMP(RP_SILVER, C_NAVY, C_DKBLUE, C_BLUE, C_SKY, C_ICE);
RAMP(RP_IRON,   C_BLACK, C_DKGRAY, C_DKSTONE, C_STONE);
RAMP(RP_SKIN,   C_DKGREEN, C_GREEN, C_LTGREEN, C_FOL4);

/* ---- the rasteriser ---- */
static Sprite *CV;
static const float LX = -0.52f, LY = -0.62f, LZ = 0.59f;       /* light comes from the top-left */

static inline float fclampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

static void cv_begin(Sprite *s, int w, int h)
{
    CV = s; s->w = w; s->h = h;
    memset(s->d, 255, sizeof s->d);
}

static inline void cput(int x, int y, int c)
{
    if ((unsigned)x < (unsigned)CV->w && (unsigned)y < (unsigned)CV->h) CV->d[y * CV->w + x] = (uint8_t)c;
}

/* pick a colour from a ramp for brightness t (0..1), dithering between neighbours */
static inline int rpick(const uint8_t *r, int n, float t, int x, int y)
{
    float v = fclampf(t, 0.0f, 1.0f) * (float)(n - 1);
    int i = (int)v;
    if (i >= n - 1) return r[n - 1];
    float f = v - (float)i;
    return (f * 16.0f > (float)BAYER4[(y & 3) * 4 + (x & 3)] + 0.5f) ? r[i + 1] : r[i];
}

static inline float light_t(float nx, float ny, float nz, float rim)
{
    float d = nx * LX + ny * LY + nz * LZ;
    float t = fclampf((d + 0.32f) / 1.32f, 0.0f, 1.0f);
    t = 0.08f + 0.92f * t;
    if (rim > 0.0f && nz < 0.5f && nx * 0.55f + ny * 0.83f > 0.2f) t += rim * (0.5f - nz) * 2.0f;   /* bounce light */
    return t;
}

/* a shaded sphere/ellipsoid */
static void s_ell(float cx, float cy, float rx, float ry, const uint8_t *rp, int n, float rim)
{
    int x0 = (int)floorf(cx - rx - 1), x1 = (int)ceilf(cx + rx + 1), y0 = (int)floorf(cy - ry - 1), y1 = (int)ceilf(cy + ry + 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = ((float)x + 0.5f - cx) / rx, dy = ((float)y + 0.5f - cy) / ry, d2 = dx * dx + dy * dy;
            if (d2 > 1.0f) continue;
            cput(x, y, rpick(rp, n, light_t(dx, dy, sqrtf(1.0f - d2), rim), x, y));
        }
}

/* a shaded tapered capsule (limbs, tails, branches) */
static void s_cap(float x0, float y0, float r0, float x1, float y1, float r1, const uint8_t *rp, int n, float rim)
{
    float vx = x1 - x0, vy = y1 - y0, L2 = vx * vx + vy * vy;
    if (L2 < 1e-4f) L2 = 1e-4f;
    float rm = r0 > r1 ? r0 : r1;
    int bx0 = (int)floorf((x0 < x1 ? x0 : x1) - rm - 1), bx1 = (int)ceilf((x0 > x1 ? x0 : x1) + rm + 1);
    int by0 = (int)floorf((y0 < y1 ? y0 : y1) - rm - 1), by1 = (int)ceilf((y0 > y1 ? y0 : y1) + rm + 1);
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++) {
            float pxc = (float)x + 0.5f, pyc = (float)y + 0.5f;
            float u = fclampf(((pxc - x0) * vx + (pyc - y0) * vy) / L2, 0.0f, 1.0f);
            float cx = x0 + vx * u, cy = y0 + vy * u, r = r0 + (r1 - r0) * u;
            float dx = (pxc - cx) / r, dy = (pyc - cy) / r, d2 = dx * dx + dy * dy;
            if (d2 > 1.0f) continue;
            cput(x, y, rpick(rp, n, light_t(dx, dy, sqrtf(1.0f - d2), rim), x, y));
        }
}

/* a polygon, shaded by a vertical gradient (t0 at the top, t1 at the bottom) and a horizontal lean (hx) */
static void s_poly(const float *p, int np, const uint8_t *rp, int n, float t0, float t1, float hx)
{
    float xmin = 1e9f, xmax = -1e9f, ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < np; i++) {
        if (p[i * 2] < xmin) xmin = p[i * 2];
        if (p[i * 2] > xmax) xmax = p[i * 2];
        if (p[i * 2 + 1] < ymin) ymin = p[i * 2 + 1];
        if (p[i * 2 + 1] > ymax) ymax = p[i * 2 + 1];
    }
    float hh = ymax - ymin + 0.001f, ww = xmax - xmin + 0.001f;
    for (int y = (int)floorf(ymin); y <= (int)ceilf(ymax); y++)
        for (int x = (int)floorf(xmin); x <= (int)ceilf(xmax); x++) {
            float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
            int in = 0;
            for (int i = 0, j = np - 1; i < np; j = i++) {
                float xi = p[i * 2], yi = p[i * 2 + 1], xj = p[j * 2], yj = p[j * 2 + 1];
                if (((yi > fy) != (yj > fy)) && (fx < (xj - xi) * (fy - yi) / (yj - yi) + xi)) in = !in;
            }
            if (!in) continue;
            float t = t0 + (t1 - t0) * (fy - ymin) / hh + hx * (0.5f - (fx - xmin) / ww);
            cput(x, y, rpick(rp, n, t, x, y));
        }
}

static void s_fell(float cx, float cy, float rx, float ry, int c)
{
    for (int y = (int)floorf(cy - ry - 1); y <= (int)ceilf(cy + ry + 1); y++)
        for (int x = (int)floorf(cx - rx - 1); x <= (int)ceilf(cx + rx + 1); x++) {
            float dx = ((float)x + 0.5f - cx) / rx, dy = ((float)y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) cput(x, y, c);
        }
}

/* a flat-coloured line of the given radius */
static void s_fline(float x0, float y0, float x1, float y1, float r, int c)
{
    float vx = x1 - x0, vy = y1 - y0, L2 = vx * vx + vy * vy;
    if (L2 < 1e-4f) L2 = 1e-4f;
    for (int y = (int)floorf((y0 < y1 ? y0 : y1) - r - 1); y <= (int)ceilf((y0 > y1 ? y0 : y1) + r + 1); y++)
        for (int x = (int)floorf((x0 < x1 ? x0 : x1) - r - 1); x <= (int)ceilf((x0 > x1 ? x0 : x1) + r + 1); x++) {
            float pxc = (float)x + 0.5f, pyc = (float)y + 0.5f;
            float u = fclampf(((pxc - x0) * vx + (pyc - y0) * vy) / L2, 0.0f, 1.0f);
            float dx = pxc - (x0 + vx * u), dy = pyc - (y0 + vy * u);
            if (dx * dx + dy * dy <= r * r) cput(x, y, c);
        }
}

/* a flat polygon */
static void s_fpoly(const float *p, int np, int c)
{
    static const uint8_t one[1] = {0};
    uint8_t col[2] = {(uint8_t)c, (uint8_t)c};
    (void)one;
    s_poly(p, np, col, 2, 0.0f, 0.0f, 0.0f);
}

/* an eye: white, with a pupil that looks to one side, and a catch-light */
static void s_eye(float cx, float cy, float rx, float ry, float look, int pupil)
{
    s_fell(cx, cy, rx, ry, C_WHITE);
    s_fell(cx + look, cy + 0.3f, rx * 0.58f, ry * 0.66f, pupil);
    s_fell(cx + look - rx * 0.2f, cy - ry * 0.2f, rx * 0.2f, ry * 0.2f, C_WHITE);
}

/* a thin ink outline round everything drawn so far */
static void s_outline(int col)
{
    static uint8_t t[64 * 64];
    int w = CV->w, h = CV->h;
    memcpy(t, CV->d, (size_t)(w * h));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (t[y * w + x] != 255) continue;
            if ((x > 0 && t[y * w + x - 1] != 255) || (x < w - 1 && t[y * w + x + 1] != 255) ||
                (y > 0 && t[(y - 1) * w + x] != 255) || (y < h - 1 && t[(y + 1) * w + x] != 255))
                CV->d[y * w + x] = (uint8_t)col;
        }
}

/* the same polygon mirrored about x = 16 (or any axis) */
static void mirror_pts(const float *in, float *out, int np, float axis)
{
    for (int i = 0; i < np; i++) { out[i * 2] = 2.0f * axis - in[i * 2]; out[i * 2 + 1] = in[i * 2 + 1]; }
}

/* ======================================================================== */
/* monsters                                                                 */
/* ======================================================================== */
static void art_slime(Sprite *s, int f)
{
    cv_begin(s, 32, 32);
    float ph = (float)f * (float)M_PI * 0.5f, sq = sinf(ph) * 1.7f;
    float rx = 11.5f + sq, ry = 8.6f - sq * 0.8f, cx = 16.0f, cy = 27.0f - ry;
    s_ell(cx - rx * 0.65f, 26.2f, 3.6f, 2.6f, RP_SLIME, RP_SLIME_N, 0.35f);          /* low blobs at the edges */
    s_ell(cx + rx * 0.7f, 26.0f, 3.2f, 2.4f, RP_SLIME, RP_SLIME_N, 0.35f);
    s_ell(cx, cy, rx, ry, RP_SLIME, RP_SLIME_N, 0.45f);
    s_fell(cx - 4.5f, cy - ry * 0.55f, 3.4f, 1.6f, C_MINT);                           /* gloss */
    s_fell(cx - 7.0f, cy - ry * 0.1f, 1.0f, 0.8f, C_WHITE);
    s_fell(cx + 5.5f, cy + 2.2f, 1.3f, 1.3f, C_LTGREEN);                              /* bubbles */
    s_fell(cx + 7.4f, cy - 0.3f, 0.9f, 0.9f, C_LTGREEN);
    s_fell(cx - 1.0f, cy + 3.7f, 0.9f, 0.9f, C_GREEN);
    s_eye(cx - 3.6f, cy - 0.8f, 2.5f, 3.0f, 0.5f, C_BLACK);
    s_eye(cx + 3.6f, cy - 0.8f, 2.5f, 3.0f, 0.5f, C_BLACK);
    s_fline(cx - 2.0f, cy + 4.0f, cx + 2.0f, cy + 4.0f, 0.55f, C_FOL0);               /* little smile */
    s_fline(cx - 2.8f, cy + 3.3f, cx - 2.0f, cy + 4.0f, 0.5f, C_FOL0);
    s_fline(cx + 2.8f, cy + 3.3f, cx + 2.0f, cy + 4.0f, 0.5f, C_FOL0);
    s_outline(C_FOL0);
}

static void art_bat(Sprite *s, int f)
{
    cv_begin(s, 32, 32);
    float a = sinf((float)f * (float)M_PI * 0.5f);                       /* wing flap: -1 up .. +1 down */
    float ty = 11.0f + 8.0f * a;
    float wl[12] = {12.5f, 13.0f, 1.5f, ty, 4.5f, ty + 7.0f, 7.5f, ty + 5.0f, 10.0f, ty + 9.5f, 12.5f, 20.0f}, wr[12];
    mirror_pts(wl, wr, 6, 16.0f);
    s_poly(wl, 6, RP_BAT, RP_BAT_N, 0.85f, 0.25f, 0.5f);
    s_poly(wr, 6, RP_BAT, RP_BAT_N, 0.85f, 0.25f, -0.5f);
    for (int k = 0; k < 3; k++) {                                          /* wing bones */
        float bx = wl[4 + k * 2 - (k == 0 ? 2 : 0)], by = wl[5 + k * 2 - (k == 0 ? 2 : 0)];
        if (k == 0) { bx = wl[2]; by = wl[3]; }
        s_fline(12.5f, 13.5f, bx, by, 0.45f, C_VIO0);
        s_fline(19.5f, 13.5f, 32.0f - bx, by, 0.45f, C_VIO0);
    }
    s_ell(16.0f, 17.5f, 4.6f, 6.4f, RP_BAT, RP_BAT_N, 0.5f);               /* body */
    s_ell(16.0f, 18.5f, 2.6f, 3.6f, RP_BAT, RP_BAT_N - 1, 0.0f);            /* lighter belly */
    float ear[6] = {12.6f, 8.0f, 12.2f, 2.6f, 15.0f, 6.0f}, ear2[6];
    mirror_pts(ear, ear2, 3, 16.0f);
    s_poly(ear, 3, RP_BAT, RP_BAT_N, 0.3f, 0.7f, 0);
    s_poly(ear2, 3, RP_BAT, RP_BAT_N, 0.3f, 0.7f, 0);
    s_ell(16.0f, 10.2f, 4.4f, 3.9f, RP_BAT, RP_BAT_N, 0.5f);               /* head */
    s_fell(14.3f, 10.0f, 1.2f, 1.4f, C_YELLOW); s_fell(17.7f, 10.0f, 1.2f, 1.4f, C_YELLOW);
    s_fell(14.4f, 10.2f, 0.6f, 0.9f, C_RED); s_fell(17.6f, 10.2f, 0.6f, 0.9f, C_RED);
    float fg[6] = {14.8f, 12.4f, 15.6f, 12.4f, 15.2f, 14.2f}, fg2[6];
    mirror_pts(fg, fg2, 3, 16.0f);
    s_fpoly(fg, 3, C_WHITE); s_fpoly(fg2, 3, C_WHITE);
    s_outline(C_BLACK);
}

static void art_golem(Sprite *s, int f)
{
    cv_begin(s, 32, 32);
    float ph = (float)f * (float)M_PI * 0.5f, sw = sinf(ph) * 2.2f, bob = -fabsf(sinf(ph)) * 0.9f;
    s_cap(12.5f, 21.0f + bob, 3.6f, 12.5f - sw * 0.4f, 27.5f - (sw > 0 ? sw * 0.6f : 0), 3.8f, RP_STONE, RP_STONE_N, 0.3f);   /* legs */
    s_cap(19.5f, 21.0f + bob, 3.6f, 19.5f + sw * 0.4f, 27.5f - (sw < 0 ? -sw * 0.6f : 0), 3.8f, RP_STONE, RP_STONE_N, 0.3f);
    s_cap(8.0f, 13.5f + bob, 3.8f, 5.0f, 20.0f + bob + sw, 3.6f, RP_STONE, RP_STONE_N, 0.3f);                                  /* arms */
    s_cap(24.0f, 13.5f + bob, 3.8f, 27.0f, 20.0f + bob - sw, 3.6f, RP_STONE, RP_STONE_N, 0.3f);
    s_ell(4.6f, 22.5f + bob + sw, 3.8f, 3.5f, RP_STONE, RP_STONE_N, 0.3f);                                                    /* fists */
    s_ell(27.4f, 22.5f + bob - sw, 3.8f, 3.5f, RP_STONE, RP_STONE_N, 0.3f);
    s_ell(16.0f, 16.6f + bob, 9.6f, 8.4f, RP_STONE, RP_STONE_N, 0.4f);                                                        /* torso */
    s_ell(8.6f, 12.0f + bob, 4.4f, 3.6f, RP_STONE, RP_STONE_N, 0.3f);                                                         /* shoulders */
    s_ell(23.4f, 12.0f + bob, 4.4f, 3.6f, RP_STONE, RP_STONE_N, 0.3f);
    s_fell(9.4f, 10.4f + bob, 2.4f, 1.1f, C_FOL0); s_fell(8.8f, 10.0f + bob, 1.7f, 0.7f, C_GREEN);                           /* moss */
    s_fell(23.0f, 10.8f + bob, 1.8f, 0.9f, C_DKGREEN); s_fell(22.6f, 10.5f + bob, 1.2f, 0.6f, C_GREEN);
    float rn[8] = {16.0f, 12.0f + bob, 18.6f, 16.0f + bob, 16.0f, 20.0f + bob, 13.4f, 16.0f + bob};
    s_fpoly(rn, 4, C_DKBLUE);                                                                                                 /* glowing rune */
    float rn2[8] = {16.0f, 13.6f + bob, 17.4f, 16.0f + bob, 16.0f, 18.4f + bob, 14.6f, 16.0f + bob};
    s_fpoly(rn2, 4, C_CYAN);
    s_fell(16.0f, 16.0f + bob, 0.9f, 1.2f, C_WHITE);
    s_fline(11.5f, 18.5f + bob, 13.5f, 22.0f + bob, 0.4f, C_ROCK0);                                                           /* cracks */
    s_fline(13.5f, 22.0f + bob, 12.5f, 24.0f + bob, 0.4f, C_ROCK0);
    s_fline(21.0f, 19.0f + bob, 19.5f, 22.5f + bob, 0.4f, C_ROCK0);
    s_ell(16.0f, 6.8f + bob, 4.6f, 4.4f, RP_STONE, RP_STONE_N, 0.4f);                                                         /* head */
    float br[8] = {11.8f, 5.2f + bob, 20.2f, 5.2f + bob, 20.0f, 7.0f + bob, 12.0f, 6.2f + bob};
    s_fpoly(br, 4, C_ROCK0);                                                                                                  /* heavy brow */
    s_fell(13.8f, 7.9f + bob, 1.5f, 1.0f, C_YELLOW); s_fell(18.2f, 7.9f + bob, 1.5f, 1.0f, C_YELLOW);
    s_fell(13.8f, 8.0f + bob, 0.6f, 0.6f, C_WHITE); s_fell(18.2f, 8.0f + bob, 0.6f, 0.6f, C_WHITE);
    s_fline(14.0f, 10.2f + bob, 18.0f, 10.2f + bob, 0.45f, C_ROCK0);
    s_outline(C_BLACK);
}

/* shared by zombie and pumpkin zombie: shambling body (head drawn by the caller) */
static void zombie_body(float sw, float bob, int shirt_dark)
{
    float lift1 = sw > 0 ? sw : 0, lift2 = sw < 0 ? -sw : 0;
    s_cap(13.0f, 20.0f + bob, 2.7f, 12.6f, 27.0f - lift1, 2.8f, RP_PANTS, RP_PANTS_N, 0.3f);
    s_cap(19.0f, 20.0f + bob, 2.7f, 19.4f, 27.0f - lift2, 2.8f, RP_PANTS, RP_PANTS_N, 0.3f);
    s_ell(12.2f, 27.6f - lift1, 3.4f, 1.8f, RP_WOOD, RP_WOOD_N, 0.2f);
    s_ell(19.8f, 27.6f - lift2, 3.4f, 1.8f, RP_WOOD, RP_WOOD_N, 0.2f);
    s_cap(10.0f, 15.0f + bob, 2.2f, 6.0f - sw * 0.3f, 22.0f + bob - sw, 2.0f, RP_ZOM, RP_ZOM_N, 0.3f);              /* reaching arms */
    s_cap(22.0f, 15.0f + bob, 2.2f, 26.0f + sw * 0.3f, 22.0f + bob + sw, 2.0f, RP_ZOM, RP_ZOM_N, 0.3f);
    s_ell(5.6f, 22.8f + bob - sw, 2.1f, 2.0f, RP_ZOM, RP_ZOM_N, 0.3f);
    s_ell(26.4f, 22.8f + bob + sw, 2.1f, 2.0f, RP_ZOM, RP_ZOM_N, 0.3f);
    float torso[16] = {9.5f, 14.0f + bob, 22.5f, 14.0f + bob, 23.0f, 21.0f + bob, 21.0f, 22.0f + bob,
                       19.0f, 21.0f + bob, 16.0f, 22.5f + bob, 13.0f, 21.5f + bob, 9.0f, 21.5f + bob};
    s_poly(torso, 8, RP_CLOTH, shirt_dark ? RP_CLOTH_N - 1 : RP_CLOTH_N, 0.75f, 0.3f, 0.5f);
    s_fell(15.0f, 17.5f + bob, 1.1f, 1.8f, C_ZOM0);                                                                   /* torn shirt */
    s_fline(11.0f, 15.5f + bob, 13.0f, 17.0f + bob, 0.4f, C_WOOD0);
    s_fline(20.0f, 17.0f + bob, 18.5f, 19.5f + bob, 0.4f, C_WOOD0);
}

static void art_zombie(Sprite *s, int f)
{
    cv_begin(s, 32, 32);
    float ph = (float)f * (float)M_PI * 0.5f, sw = sinf(ph) * 1.8f, bob = -fabsf(sinf(ph)) * 0.7f;
    zombie_body(sw, bob, 0);
    s_ell(16.0f, 8.4f + bob, 5.0f, 5.4f, RP_ZOM, RP_ZOM_N, 0.4f);                                        /* head */
    float hair[12] = {11.3f, 6.0f + bob, 12.5f, 2.5f + bob, 14.0f, 4.5f + bob, 16.0f, 2.2f + bob, 18.5f, 4.0f + bob, 20.5f, 6.0f + bob};
    s_fpoly(hair, 6, C_WOOD0);
    s_fell(13.6f, 8.6f + bob, 1.7f, 2.0f, C_BLACK); s_fell(18.4f, 8.6f + bob, 1.7f, 2.0f, C_BLACK);                  /* sunken eyes */
    s_fell(13.7f, 8.9f + bob, 0.7f, 0.8f, C_RED); s_fell(18.5f, 8.9f + bob, 0.7f, 0.8f, C_RED);
    s_fell(16.0f, 11.8f + bob, 3.0f, 1.2f, C_BLACK);                                                                 /* gaping mouth */
    for (int k = 0; k < 3; k++) s_fell(14.2f + (float)k * 1.8f, 11.0f + bob, 0.45f, 0.55f, C_CREAM);
    s_fline(12.0f, 6.2f + bob, 15.0f, 6.8f + bob, 0.35f, C_ZOM0);                                                   /* stitches */
    for (int k = 0; k < 3; k++) s_fline(12.5f + (float)k * 1.0f, 5.6f + bob, 12.5f + (float)k * 1.0f, 7.2f + bob, 0.25f, C_BLACK);
    s_outline(C_FOL0);
}

static void art_pumpz(Sprite *s, int f)
{
    cv_begin(s, 32, 32);
    float ph = (float)f * (float)M_PI * 0.5f, sw = sinf(ph) * 1.6f, bob = -fabsf(sinf(ph)) * 0.7f;
    zombie_body(sw, bob, 1);
    s_cap(16.0f, 3.8f + bob, 0.9f, 17.2f, 1.6f + bob, 0.8f, RP_WOOD, RP_WOOD_N, 0.0f);                              /* stem */
    s_ell(9.6f, 8.8f + bob, 4.6f, 5.4f, RP_PUMP, RP_PUMP_N, 0.4f);                                                   /* lobes */
    s_ell(22.4f, 8.8f + bob, 4.6f, 5.4f, RP_PUMP, RP_PUMP_N, 0.4f);
    s_ell(16.0f, 8.4f + bob, 6.6f, 6.0f, RP_PUMP, RP_PUMP_N, 0.4f);
    s_fline(11.6f, 4.0f + bob, 11.0f, 12.0f + bob, 0.4f, C_DKBROWN); s_fline(20.4f, 4.0f + bob, 21.0f, 12.0f + bob, 0.4f, C_DKBROWN);
    int fc = (f & 1) ? C_ORANGE : C_YELLOW;
    float e1[6] = {11.0f, 9.2f + bob, 14.2f, 9.2f + bob, 12.6f, 6.4f + bob}, e2[6] = {17.8f, 9.2f + bob, 21.0f, 9.2f + bob, 19.4f, 6.4f + bob};
    s_fpoly(e1, 3, fc); s_fpoly(e2, 3, fc);
    float nz[6] = {15.2f, 10.4f + bob, 16.8f, 10.4f + bob, 16.0f, 9.0f + bob};
    s_fpoly(nz, 3, fc);
    float mo[16] = {11.0f, 11.6f + bob, 13.0f, 12.8f + bob, 14.6f, 11.6f + bob, 16.0f, 12.8f + bob,
                    17.4f, 11.6f + bob, 19.0f, 12.8f + bob, 21.0f, 11.6f + bob, 16.0f, 13.8f + bob};
    s_fpoly(mo, 8, fc);
    s_fell(12.0f, 5.6f + bob, 1.4f, 0.8f, C_EMBER);
    s_outline(C_BLACK);
}

static void art_witch(Sprite *s, int f)
{
    cv_begin(s, 32, 32);
    float ph = (float)f * (float)M_PI * 0.5f, fl = sinf(ph), bob = fl * 0.9f;
    float tail[10] = {10.0f, 18.5f + bob, 2.0f, 16.0f + bob * 2.0f + fl * 1.2f, 3.0f, 20.5f + bob + fl, 7.0f, 21.5f + bob, 11.0f, 21.0f + bob};
    s_fpoly(tail, 5, C_ORANGE);                                                                                      /* broom straw */
    for (int k = 0; k < 4; k++) s_fline(10.0f, 19.5f + bob, 2.5f, 16.5f + (float)k * 1.3f + bob + fl * 0.8f, 0.4f, C_BROWN);
    s_cap(9.0f, 19.6f + bob, 1.0f, 28.0f, 22.8f + bob, 1.0f, RP_WOOD, RP_WOOD_N, 0.2f);                              /* broom handle */
    s_cap(15.5f, 21.0f + bob, 1.7f, 14.5f, 26.5f + bob + fl * 0.6f, 1.5f, RP_CLOAK, RP_CLOAK_N, 0.3f);                /* dangling leg */
    s_ell(14.2f, 27.3f + bob + fl * 0.6f, 2.6f, 1.4f, RP_CLOAK, RP_CLOAK_N, 0.3f);
    float cl[14] = {14.0f, 13.5f + bob, 21.0f, 13.5f + bob, 22.5f, 20.5f + bob, 17.0f, 22.5f + bob,
                    10.5f + fl * 0.6f, 24.0f + bob, 9.0f, 18.5f + bob, 12.0f, 15.0f + bob};
    s_poly(cl, 7, RP_CLOAK, RP_CLOAK_N, 0.85f, 0.2f, 0.6f);
    s_cap(20.5f, 15.5f + bob, 1.6f, 25.5f, 19.5f + bob, 1.4f, RP_CLOAK, RP_CLOAK_N, 0.3f);                           /* arm to the broom */
    s_ell(26.0f, 20.0f + bob, 1.5f, 1.5f, RP_SKIN, RP_SKIN_N, 0.3f);
    float hr[8] = {13.0f, 8.0f + bob, 9.5f, 12.5f + bob + fl, 12.5f, 14.5f + bob, 15.0f, 11.0f + bob};
    s_fpoly(hr, 4, C_ORANGE);                                                                                        /* streaming hair */
    s_ell(17.4f, 10.0f + bob, 4.2f, 4.4f, RP_SKIN, RP_SKIN_N, 0.4f);                                                 /* face */
    float ns[6] = {21.0f, 10.0f + bob, 24.2f, 12.6f + bob, 20.6f, 12.0f + bob};
    s_poly(ns, 3, RP_SKIN, RP_SKIN_N, 0.7f, 0.5f, 0);
    s_fell(19.4f, 9.6f + bob, 1.3f, 1.2f, C_WHITE); s_fell(19.8f, 9.7f + bob, 0.6f, 0.7f, C_BLACK);
    s_fline(16.5f, 13.2f + bob, 19.5f, 13.4f + bob, 0.35f, C_DKGREEN);
    s_ell(17.0f, 6.8f + bob, 7.4f, 2.0f, RP_CLOAK, RP_CLOAK_N, 0.3f);                                                /* hat brim */
    float hat[10] = {12.5f, 6.4f + bob, 21.5f, 6.4f + bob, 19.0f, 3.0f + bob, 14.0f + fl * 0.5f, 0.8f + bob, 12.0f, 3.2f + bob};
    s_poly(hat, 5, RP_CLOAK, RP_CLOAK_N, 0.9f, 0.25f, 0.5f);
    s_fell(17.0f, 5.9f + bob, 4.6f, 0.9f, C_YELLOW);
    s_fell(17.0f, 5.9f + bob, 1.1f, 0.9f, C_DKBROWN);
    s_outline(C_BLACK);
}

static void art_cat(Sprite *s, int f)
{
    cv_begin(s, 32, 32);
    float ph = (float)f * (float)M_PI * 0.5f, sw = sinf(ph) * 2.2f, bob = -fabsf(sinf(ph)) * 0.8f;
    s_cap(6.5f, 19.0f + bob, 1.5f, 3.0f, 13.0f + bob - sw * 0.3f, 1.3f, RP_CAT, RP_CAT_N, 0.6f);                      /* tail */
    s_cap(3.0f, 13.0f + bob - sw * 0.3f, 1.3f, 5.5f, 7.0f + bob, 1.1f, RP_CAT, RP_CAT_N, 0.6f);
    s_cap(10.0f, 22.0f + bob, 1.9f, 10.0f - sw, 27.5f, 1.8f, RP_CAT, RP_CAT_N, 0.6f);                                 /* legs */
    s_cap(13.5f, 22.0f + bob, 1.9f, 13.5f + sw, 27.5f, 1.8f, RP_CAT, RP_CAT_N, 0.6f);
    s_cap(20.0f, 22.0f + bob, 1.9f, 20.0f + sw, 27.5f, 1.8f, RP_CAT, RP_CAT_N, 0.6f);
    s_cap(23.0f, 22.0f + bob, 1.9f, 23.0f - sw, 27.5f, 1.8f, RP_CAT, RP_CAT_N, 0.6f);
    s_ell(14.5f, 19.0f + bob, 9.0f, 5.2f, RP_CAT, RP_CAT_N, 0.7f);                                                    /* body */
    float e1[6] = {20.5f, 11.5f + bob, 21.0f, 6.0f + bob, 24.2f, 10.0f + bob}, e2[6] = {24.5f, 10.0f + bob, 27.0f, 6.2f + bob, 28.2f, 12.0f + bob};
    s_poly(e1, 3, RP_CAT, RP_CAT_N, 0.4f, 0.8f, 0); s_poly(e2, 3, RP_CAT, RP_CAT_N, 0.4f, 0.8f, 0);
    s_ell(24.4f, 14.8f + bob, 5.0f, 4.5f, RP_CAT, RP_CAT_N, 0.7f);                                                    /* head */
    s_fell(26.6f, 13.6f + bob, 1.7f, 1.9f, C_YELLOW); s_fell(22.4f, 13.6f + bob, 1.5f, 1.8f, C_YELLOW);               /* glowing eyes */
    s_fell(26.9f, 13.7f + bob, 0.45f, 1.5f, C_BLACK); s_fell(22.6f, 13.7f + bob, 0.45f, 1.4f, C_BLACK);
    s_fell(28.4f, 16.0f + bob, 0.7f, 0.5f, C_PINK);
    s_fline(27.0f, 17.4f + bob, 31.0f, 16.4f + bob, 0.2f, C_LIGHT); s_fline(27.0f, 18.0f + bob, 31.0f, 18.4f + bob, 0.2f, C_LIGHT);
    float sp = (f & 1) ? 1.0f : 0.6f;                                                                                 /* magic sparkle */
    s_fell(10.0f, 5.0f, sp, sp, C_VIO4); s_fline(10.0f, 5.0f - sp * 2.0f, 10.0f, 5.0f + sp * 2.0f, 0.3f, C_PINK); s_fline(10.0f - sp * 2.0f, 5.0f, 10.0f + sp * 2.0f, 5.0f, 0.3f, C_PINK);
    s_outline(C_VIO0);
}

static void ghost_blob(Sprite *s, float cx, float cy, float rad, float bottom, float hwmax, float ph, float freq, int tatter)
{
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            float fx = (float)x + 0.5f - cx, fy = (float)y + 0.5f;
            float hw, bot = bottom + sinf(fx * freq + ph) * (tatter ? 2.6f : 1.6f);
            if (fy < cy) { float t = (cy - fy) / rad; if (t > 1.0f) continue; hw = rad * sqrtf(1.0f - t * t); }
            else hw = rad + (hwmax - rad) * (fy - cy) / (bottom - cy);
            if (fabsf(fx) > hw || fy > bot) continue;
            float nx = fx / (hwmax * 1.05f), ny = (fy - (cy + bottom) * 0.5f) / ((bottom - cy + rad) * 0.6f);
            float d2 = nx * nx + ny * ny * 0.5f, nz = sqrtf(fclampf(1.0f - d2, 0.02f, 1.0f));
            cput(x, y, rpick(RP_GHOST, RP_GHOST_N, light_t(nx, ny, nz, 0.4f) + 0.08f, x, y));
        }
}

static void art_ghost(Sprite *s, int f)
{
    cv_begin(s, 32, 32);
    float ph = (float)f * (float)M_PI * 0.5f, bob = sinf(ph) * 0.6f;
    s_cap(9.5f, 17.0f, 1.9f, 4.5f, 20.0f + sinf(ph) * 2.0f, 1.1f, RP_GHOST, RP_GHOST_N, 0.4f);
    s_cap(22.5f, 17.0f, 1.9f, 27.5f, 20.0f - sinf(ph) * 2.0f, 1.1f, RP_GHOST, RP_GHOST_N, 0.4f);
    ghost_blob(s, 16.0f, 11.0f + bob, 7.6f, 27.0f + bob, 9.4f, ph, 0.9f, 0);
    s_fell(13.4f, 11.4f + bob, 1.9f, 2.7f, C_BLACK); s_fell(18.6f, 11.4f + bob, 1.9f, 2.7f, C_BLACK);                  /* hollow eyes */
    s_fell(13.4f, 11.8f + bob, 0.7f, 1.0f, C_CYAN); s_fell(18.6f, 11.8f + bob, 0.7f, 1.0f, C_CYAN);
    s_fell(16.0f, 17.0f + bob, 1.7f, 2.4f, C_BLACK);                                                                 /* wailing mouth */
    s_fell(11.0f, 8.0f + bob, 2.2f, 1.1f, C_WHITE);                                                                  /* gloss */
    s_outline(C_GHO0);
}

static void art_boss(Sprite *s, int f)
{
    cv_begin(s, 64, 64);
    float ph = (float)f * (float)M_PI * 0.5f, bob = sinf(ph) * 0.9f;
    for (int side = 0; side < 2; side++) {                                  /* long arms with claws */
        float sg = side ? -1.0f : 1.0f, sx = 32.0f - sg * 17.0f, ex = 32.0f - sg * 27.0f, ey = 47.0f + sinf(ph + (float)side * 3.1f) * 2.4f;
        s_cap(sx, 34.0f + bob, 5.0f, ex, ey, 3.2f, RP_GHOST, RP_GHOST_N, 0.4f);
        for (int k = 0; k < 3; k++) s_cap(ex, ey, 1.6f, ex - sg * (2.0f + (float)k * 1.4f), ey + 6.0f + (float)(k == 1) * 1.5f, 0.5f, RP_BONE, RP_BONE_N, 0.2f);
    }
    ghost_blob(s, 32.0f, 26.0f + bob, 18.0f, 58.0f + bob, 25.0f, ph, 0.42f, 1);
    s_fell(22.0f, 15.0f + bob, 5.0f, 2.2f, C_WHITE);                         /* gloss */
    for (int e = 0; e < 2; e++) {                                            /* hollow eyes, burning pupils */
        float ex = e ? 41.0f : 23.0f;
        s_fell(ex, 25.0f + bob, 5.4f, 6.6f, C_BLACK);
        s_fell(ex, 26.0f + bob, 2.8f, 3.4f, C_DKRED);
        s_fell(ex, 26.0f + bob, 1.8f, 2.4f, C_RED);
    }
    float mo[24] = {22.0f, 37.0f + bob, 25.0f, 34.5f + bob, 32.0f, 36.0f + bob, 39.0f, 34.5f + bob, 42.0f, 37.0f + bob, 40.0f, 43.0f + bob,
                    36.0f, 46.0f + bob, 32.0f, 44.0f + bob, 28.0f, 46.0f + bob, 24.0f, 43.0f + bob, 22.0f, 40.0f + bob, 22.0f, 38.0f + bob};
    s_fpoly(mo, 12, C_BLACK);
    for (int k = 0; k < 5; k++) {                                            /* fangs */
        float tx = 25.0f + (float)k * 3.6f;
        float up[6] = {tx - 1.2f, 36.0f + bob, tx + 1.2f, 36.0f + bob, tx, 39.5f + bob};
        s_fpoly(up, 3, C_WHITE);
        if (k > 0 && k < 4) { float dn[6] = {tx - 1.2f, 45.5f + bob, tx + 1.2f, 45.5f + bob, tx, 42.0f + bob}; s_fpoly(dn, 3, C_WHITE); }
    }
    /* the crown */
    float band[8] = {21.0f, 11.0f + bob, 43.0f, 11.0f + bob, 44.0f, 16.0f + bob, 20.0f, 16.0f + bob};
    float spike[5][6];
    for (int k = 0; k < 5; k++) {
        float bx = 22.0f + (float)k * 5.0f, h = (k == 2) ? 9.5f : ((k & 1) ? 6.0f : 7.5f);
        spike[k][0] = bx - 2.6f; spike[k][1] = 12.0f + bob; spike[k][2] = bx + 2.6f; spike[k][3] = 12.0f + bob;
        spike[k][4] = bx; spike[k][5] = 12.0f - h + bob;
        s_poly(spike[k], 3, RP_GOLD, RP_GOLD_N, 0.9f, 0.4f, 0.5f);
        s_fell(bx, 12.0f - h + bob, 1.0f, 1.0f, C_CREAM);
    }
    s_poly(band, 4, RP_GOLD, RP_GOLD_N, 0.95f, 0.3f, 0.4f);
    for (int k = 0; k < 3; k++) { s_fell(26.0f + (float)k * 6.0f, 13.5f + bob, 1.5f, 1.5f, k == 1 ? C_CYAN : C_RED); s_fell(25.6f + (float)k * 6.0f, 13.0f + bob, 0.5f, 0.5f, C_WHITE); }
    s_outline(C_GHO0);
    s_outline(C_BLACK);
}

/* ======================================================================== */
/* towers: a runed stone pedestal; the orb above it is animated live        */
/* ======================================================================== */
static void art_tower(Sprite *s, int type, int tier)
{
    static const uint8_t accent[TW_COUNT][2] = {{C_GREEN, C_LTGREEN}, {C_ORANGE, C_YELLOW}, {C_CYAN, C_ICE}, {C_PURPLE, C_PINK}};
    const uint8_t *trim = tier == 0 ? RP_STONE : (tier == 1 ? RP_SILVER : RP_GOLD);
    int trim_n = tier == 0 ? RP_STONE_N : (tier == 1 ? RP_SILVER_N : RP_GOLD_N);
    cv_begin(s, 32, 32);
    float wid = tier == 2 ? 1.5f : 0.0f;
    s_ell(16.0f, 28.0f, 12.0f + wid, 3.4f, RP_STONE, RP_STONE_N, 0.2f);                           /* base slab */
    s_ell(16.0f, 27.0f, 10.0f + wid, 2.8f, RP_STONE, RP_STONE_N, 0.3f);
    float col[8] = {10.6f, 26.0f, 21.4f, 26.0f, 19.6f, 15.5f, 12.4f, 15.5f};                       /* tapered column */
    s_poly(col, 4, RP_STONE, RP_STONE_N, 0.8f, 0.22f, 1.0f);
    float rn[8] = {16.0f, 17.6f, 18.2f, 21.0f, 16.0f, 24.4f, 13.8f, 21.0f};                       /* glowing rune */
    s_fpoly(rn, 4, C_ROCK0);
    float rn2[8] = {16.0f, 18.8f, 17.4f, 21.0f, 16.0f, 23.2f, 14.6f, 21.0f};
    s_fpoly(rn2, 4, accent[type][0]);
    s_fell(16.0f, 21.0f, 0.8f, 1.2f, accent[type][1]);
    s_fline(12.6f, 18.4f, 14.0f, 19.8f, 0.3f, accent[type][0]);                                      /* runic scratches */
    s_fline(19.4f, 18.4f, 18.0f, 19.8f, 0.3f, accent[type][0]);
    s_fline(12.2f, 24.0f, 14.0f, 22.8f, 0.3f, accent[type][0]);
    s_fline(19.8f, 24.0f, 18.0f, 22.8f, 0.3f, accent[type][0]);
    s_ell(16.0f, 15.6f, 7.2f, 2.4f, trim, trim_n, 0.3f);                                              /* capital ring */
    s_ell(16.0f, 14.8f, 5.0f, 1.6f, RP_STONE, RP_STONE_N, 0.0f);
    s_cap(10.0f, 15.0f, 1.1f, 8.2f, 9.5f, 0.6f, trim, trim_n, 0.3f);                                 /* prongs cradling the orb */
    s_cap(22.0f, 15.0f, 1.1f, 23.8f, 9.5f, 0.6f, trim, trim_n, 0.3f);
    s_cap(16.0f, 16.0f, 1.0f, 16.0f, 12.0f, 0.8f, trim, trim_n, 0.3f);
    if (tier >= 1) {                                                                                  /* braces */
        s_cap(11.2f, 26.0f, 1.3f, 11.6f, 17.0f, 1.1f, trim, trim_n, 0.3f);
        s_cap(20.8f, 26.0f, 1.3f, 20.4f, 17.0f, 1.1f, trim, trim_n, 0.3f);
        s_ell(16.0f, 25.8f, 7.6f, 1.2f, trim, trim_n, 0.2f);
    }
    if (tier >= 2) {                                                                                  /* floating gems */
        const uint8_t *g = type == TW_CANNON ? RP_FIRE : (type == TW_FROST ? RP_ICE : (type == TW_STORM ? RP_STORM : RP_ARC));
        int gn = type == TW_CANNON ? RP_FIRE_N : (type == TW_FROST ? RP_ICE_N : (type == TW_STORM ? RP_STORM_N : RP_ARC_N));
        s_ell(4.6f, 19.0f, 2.0f, 2.8f, g, gn, 0.4f); s_ell(27.4f, 19.0f, 2.0f, 2.8f, g, gn, 0.4f);
        s_fell(4.0f, 18.0f, 0.5f, 0.7f, C_WHITE); s_fell(26.8f, 18.0f, 0.5f, 0.7f, C_WHITE);
        s_cap(7.0f, 20.5f, 0.5f, 10.6f, 23.0f, 0.5f, trim, trim_n, 0.0f);
        s_cap(25.0f, 20.5f, 0.5f, 21.4f, 23.0f, 0.5f, trim, trim_n, 0.0f);
    }
    s_outline(C_BLACK);
}

/* ======================================================================== */
/* scenery                                                                  */
/* ======================================================================== */
static void art_tree(Sprite *s)
{
    cv_begin(s, 40, 52);
    s_cap(20.0f, 50.0f, 4.2f, 20.5f, 29.0f, 2.7f, RP_WOOD, RP_WOOD_N, 0.3f);
    s_cap(16.5f, 50.0f, 1.8f, 12.0f, 49.0f, 0.8f, RP_WOOD, RP_WOOD_N, 0.1f);                         /* roots */
    s_cap(23.5f, 50.0f, 1.8f, 28.0f, 49.0f, 0.8f, RP_WOOD, RP_WOOD_N, 0.1f);
    for (int k = 0; k < 4; k++) s_fline(18.5f + (float)(k & 1) * 3.0f, 33.0f + (float)k * 4.0f, 18.5f + (float)(k & 1) * 3.0f, 36.0f + (float)k * 4.0f, 0.35f, C_WOOD0);
    static const float cl[8][3] = {{20, 24, 12.5f}, {10.5f, 27, 8.2f}, {29.5f, 27, 8.2f}, {20, 14, 10.5f}, {11.5f, 19, 8.0f}, {28.5f, 19, 8.0f}, {20, 30, 9.0f}, {20, 7, 6.2f}};
    static const int order[8] = {6, 1, 2, 0, 4, 5, 3, 7};
    for (int k = 0; k < 8; k++) {
        const float *c = cl[order[k]];
        s_ell(c[0], c[1], c[2], c[2] * 0.88f, RP_FOL, RP_FOL_N, 0.35f);
    }
    for (int k = 0; k < 26; k++) {                                                                   /* leaf clumps */
        uint32_t h = hash2(k, 7, 3);
        float lx = 7.0f + (float)(h % 27), ly = 4.0f + (float)((h >> 8) % 28);
        float dx = (lx - 20.0f) / 14.0f, dy = (ly - 20.0f) / 16.0f;
        if (dx * dx + dy * dy > 0.85f) continue;
        s_fell(lx, ly, 1.7f, 1.3f, (h >> 16) & 1 ? C_FOL4 : C_FOL0);
    }
    for (int k = 0; k < 4; k++) { uint32_t h = hash2(k, 11, 3); s_fell(11.0f + (float)(h % 18), 15.0f + (float)((h >> 8) % 16), 1.1f, 1.1f, C_RED); }
    s_outline(C_FOL0);
}

static void art_dead(Sprite *s)
{
    cv_begin(s, 40, 52);
    s_cap(20.0f, 50.0f, 4.0f, 19.0f, 30.0f, 2.4f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_cap(19.0f, 30.0f, 2.4f, 21.5f, 18.0f, 1.6f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_cap(21.5f, 18.0f, 1.6f, 19.0f, 5.5f, 0.8f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_cap(19.5f, 36.0f, 1.8f, 8.5f, 26.0f, 0.9f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_cap(8.5f, 26.0f, 0.9f, 4.0f, 17.0f, 0.5f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_cap(10.5f, 28.0f, 0.8f, 11.5f, 20.0f, 0.4f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_cap(20.0f, 31.0f, 1.7f, 33.0f, 22.0f, 0.9f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_cap(33.0f, 22.0f, 0.9f, 36.5f, 12.0f, 0.5f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_cap(31.0f, 24.0f, 0.8f, 28.5f, 15.0f, 0.4f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_cap(21.0f, 22.0f, 1.2f, 29.0f, 8.0f, 0.5f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_cap(19.5f, 14.0f, 1.0f, 11.0f, 7.0f, 0.4f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_fell(18.5f, 40.0f, 1.3f, 1.8f, C_BLACK);                                                       /* knot hole */
    s_cap(16.0f, 50.0f, 1.8f, 11.0f, 49.5f, 0.7f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_cap(24.0f, 50.0f, 1.8f, 29.0f, 49.5f, 0.7f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_outline(C_VIO0);
}

static void art_rock(Sprite *s)
{
    cv_begin(s, 32, 32);
    s_ell(20.5f, 24.0f, 7.5f, 5.2f, RP_STONE, RP_STONE_N, 0.4f);
    s_ell(14.0f, 20.5f, 10.5f, 8.4f, RP_STONE, RP_STONE_N, 0.4f);
    s_ell(10.0f, 25.0f, 5.5f, 3.8f, RP_STONE, RP_STONE_N, 0.4f);
    s_ell(17.0f, 17.5f, 5.5f, 3.6f, RP_STONE, RP_STONE_N, 0.2f);
    s_fell(11.0f, 13.6f, 3.4f, 1.4f, C_DKGREEN); s_fell(10.3f, 13.2f, 2.4f, 1.0f, C_GREEN);          /* moss */
    s_fell(21.5f, 21.4f, 2.0f, 1.0f, C_DKGREEN);
    s_fline(16.5f, 18.0f, 18.0f, 22.5f, 0.35f, C_ROCK0); s_fline(18.0f, 22.5f, 16.8f, 25.0f, 0.35f, C_ROCK0);
    s_fline(9.0f, 21.0f, 12.0f, 23.5f, 0.35f, C_ROCK0);
    s_fell(26.5f, 27.0f, 1.6f, 1.1f, C_STONE); s_fell(5.5f, 28.0f, 1.3f, 0.9f, C_DKSTONE);
    s_outline(C_BLACK);
}

static void art_grave(Sprite *s)
{
    cv_begin(s, 32, 32);
    s_ell(16.0f, 27.0f, 11.0f, 3.4f, RP_WOOD, RP_WOOD_N, 0.3f);                                       /* mound */
    float st[8] = {9.0f, 26.5f, 23.0f, 26.5f, 23.0f, 10.0f, 9.0f, 10.0f};
    s_poly(st, 4, RP_STONE, RP_STONE_N, 0.8f, 0.25f, 0.8f);
    s_ell(16.0f, 10.0f, 7.0f, 5.6f, RP_STONE, RP_STONE_N, 0.3f);
    s_fline(16.0f, 9.0f, 16.0f, 17.0f, 0.7f, C_ROCK0);                                                /* engraved cross */
    s_fline(13.0f, 12.0f, 19.0f, 12.0f, 0.7f, C_ROCK0);
    s_fline(16.0f, 9.0f, 16.0f, 17.0f, 0.25f, C_STONE);
    s_fell(11.0f, 25.0f, 2.8f, 1.3f, C_DKGREEN); s_fell(10.4f, 24.6f, 1.8f, 0.8f, C_GREEN);
    s_fell(21.5f, 22.0f, 1.2f, 2.0f, C_DKGREEN);
    s_fline(14.0f, 20.0f, 15.5f, 23.0f, 0.3f, C_ROCK0);
    s_outline(C_BLACK);
}

#define LAMP_X0 24
#define LAMP_Y0 8
#define LAMP_W  16
#define LAMP_H  10
static void art_lamp(Sprite *s)
{
    cv_begin(s, 32, 32);
    s_ell(16.0f, 29.5f, 4.0f, 1.8f, RP_IRON, RP_IRON_N, 0.2f);
    s_cap(16.0f, 29.0f, 1.3f, 16.0f, 17.0f, 1.0f, RP_IRON, RP_IRON_N, 0.4f);
    float lan[8] = {11.0f, 6.0f, 21.0f, 6.0f, 20.0f, 17.0f, 12.0f, 17.0f};
    s_poly(lan, 4, RP_IRON, RP_IRON_N, 0.8f, 0.3f, 0.5f);
    float gl[8] = {12.0f, 7.5f, 20.0f, 7.5f, 19.4f, 15.8f, 12.6f, 15.8f};
    s_fpoly(gl, 4, C_YELLOW);
    s_fline(16.0f, 7.5f, 16.0f, 15.8f, 0.4f, C_DKGRAY);                                               /* frame bars */
    float roof[6] = {9.5f, 6.5f, 22.5f, 6.5f, 16.0f, 1.0f};
    s_poly(roof, 3, RP_IRON, RP_IRON_N, 0.9f, 0.3f, 0.5f);
    s_fell(16.0f, 1.2f, 0.9f, 0.9f, C_DKSTONE);
    s_outline(C_BLACK);
}

static void art_skel(Sprite *s, int lean)
{
    cv_begin(s, 32, 32);
    float lx = lean ? 1.0f : 0.0f;
    s_ell(16.0f, 28.0f, 9.0f, 2.2f, RP_WOOD, RP_WOOD_N, 0.1f);
    s_cap(12.5f, 26.0f, 1.5f, 9.0f, 21.0f, 1.2f, RP_BONE, RP_BONE_N, 0.2f);                           /* legs folded up */
    s_cap(19.5f, 26.0f, 1.5f, 23.0f, 21.0f, 1.2f, RP_BONE, RP_BONE_N, 0.2f);
    s_ell(7.5f, 21.0f, 2.2f, 1.6f, RP_BONE, RP_BONE_N, 0.2f); s_ell(24.5f, 21.0f, 2.2f, 1.6f, RP_BONE, RP_BONE_N, 0.2f);
    s_cap(16.0f, 17.0f, 1.4f, 16.0f, 26.0f, 1.4f, RP_BONE, RP_BONE_N, 0.2f);                          /* spine */
    s_ell(16.0f, 22.5f, 4.4f, 2.0f, RP_BONE, RP_BONE_N, 0.2f);                                        /* pelvis */
    for (int k = 0; k < 4; k++) {                                                                      /* ribs */
        float y = 17.0f + (float)k * 2.0f, w = 5.2f - (float)k * 0.5f;
        s_fline(16.0f - w, y + 0.5f, 16.0f + w, y + 0.5f, 0.75f, C_LIGHT);
        s_fline(16.0f - w, y + 1.1f, 16.0f + w, y + 1.1f, 0.35f, C_STONE);
    }
    s_cap(11.0f + lx, 14.5f, 1.2f, 7.0f, 20.0f, 1.0f, RP_BONE, RP_BONE_N, 0.2f);                       /* arms resting on knees */
    s_cap(21.0f + lx, 14.5f, 1.2f, 25.0f, 20.0f, 1.0f, RP_BONE, RP_BONE_N, 0.2f);
    s_ell(16.0f + lx, 9.5f, 5.2f, 4.8f, RP_BONE, RP_BONE_N, 0.3f);                                    /* skull */
    s_ell(16.0f + lx, 13.6f, 3.4f, 1.9f, RP_BONE, RP_BONE_N, 0.1f);                                   /* jaw */
    s_fell(13.8f + lx, 9.8f, 1.8f, 2.0f, C_BLACK); s_fell(18.2f + lx, 9.8f, 1.8f, 2.0f, C_BLACK);     /* sockets (eyes glow live) */
    float ns[6] = {15.4f + lx, 12.0f, 16.6f + lx, 12.0f, 16.0f + lx, 10.6f};
    s_fpoly(ns, 3, C_BLACK);
    for (int k = 0; k < 4; k++) s_fline(14.2f + lx + (float)k * 1.2f, 13.4f, 14.2f + lx + (float)k * 1.2f, 14.4f, 0.2f, C_ROCK0);
    s_outline(C_BLACK);
}

static void art_pumpkin(Sprite *s, int face)
{
    static const uint8_t faces[3] = {C_YELLOW, C_ORANGE, C_CREAM};
    int fc = faces[face];
    cv_begin(s, 32, 32);
    s_ell(10.5f, 20.0f, 6.2f, 7.6f, RP_PUMP, RP_PUMP_N, 0.4f);
    s_ell(21.5f, 20.0f, 6.2f, 7.6f, RP_PUMP, RP_PUMP_N, 0.4f);
    s_ell(16.0f, 19.6f, 8.2f, 8.4f, RP_PUMP, RP_PUMP_N, 0.4f);
    s_fline(11.3f, 13.0f, 10.4f, 26.0f, 0.4f, C_DKBROWN); s_fline(20.7f, 13.0f, 21.6f, 26.0f, 0.4f, C_DKBROWN);
    s_cap(16.0f, 12.0f, 1.3f, 17.4f, 8.5f, 1.0f, RP_WOOD, RP_WOOD_N, 0.1f);                           /* stem */
    float lf[8] = {17.0f, 10.0f, 22.0f, 7.5f, 24.0f, 10.5f, 19.0f, 11.5f};
    s_poly(lf, 4, RP_FOL, RP_FOL_N, 0.8f, 0.3f, 0.3f);
    s_fline(18.0f, 8.0f, 20.5f, 5.0f, 0.3f, C_GREEN);                                                 /* tendril */
    float e1[6] = {10.0f, 19.0f, 14.0f, 19.0f, 12.0f, 15.2f}, e2[6] = {18.0f, 19.0f, 22.0f, 19.0f, 20.0f, 15.2f};
    float nz[6] = {15.0f, 21.0f, 17.0f, 21.0f, 16.0f, 19.2f};
    float mo[20] = {9.5f, 23.0f, 12.0f, 24.8f, 13.6f, 23.0f, 16.0f, 25.2f, 18.4f, 23.0f, 20.0f, 24.8f, 22.5f, 23.0f, 21.0f, 26.6f, 16.0f, 27.8f, 11.0f, 26.6f};
    s_fpoly(e1, 3, fc); s_fpoly(e2, 3, fc); s_fpoly(nz, 3, fc); s_fpoly(mo, 10, fc);
    s_fell(10.0f, 14.5f, 1.6f, 1.0f, C_EMBER);
    s_outline(C_BLACK);
}

static void art_portal(Sprite *s, int f)
{
    cv_begin(s, 32, 32);
    float ph = (float)f * (float)M_PI * 0.5f;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            float dx = ((float)x + 0.5f - 16.0f) / 12.0f, dy = ((float)y + 0.5f - 16.0f) / 14.5f, r = sqrtf(dx * dx + dy * dy);
            if (r > 1.0f) continue;
            float a = atan2f(dy, dx);
            float v = 0.5f + 0.5f * sinf(a * 3.0f + r * 9.0f - ph * 2.0f);
            float t = (1.0f - r) * 0.55f + v * 0.5f;
            if (r > 0.86f) t = 1.0f;                                                                   /* bright rim */
            if (r < 0.2f) t *= 0.4f;                                                                   /* dark eye */
            cput(x, y, rpick(RP_STORM, RP_STORM_N, t, x, y));
        }
    for (int k = 0; k < 5; k++) {                                                                      /* sparks */
        float a = ph + (float)k * 1.26f;
        s_fell(16.0f + cosf(a) * 13.5f, 16.0f + sinf(a) * 15.5f, 0.8f, 0.8f, k & 1 ? C_WHITE : C_PINK);
    }
    s_outline(C_VIO0);
}

static void art_heart(Sprite *s)
{
    cv_begin(s, 16, 14);
    s_ell(5.0f, 5.2f, 3.8f, 3.6f, RP_FIRE, RP_FIRE_N - 2, 0.3f);
    s_ell(11.0f, 5.2f, 3.8f, 3.6f, RP_FIRE, RP_FIRE_N - 2, 0.3f);
    float tri[6] = {1.4f, 6.6f, 14.6f, 6.6f, 8.0f, 12.8f};
    s_poly(tri, 3, RP_FIRE, RP_FIRE_N - 2, 0.6f, 0.2f, 0.4f);
    s_fell(4.0f, 3.6f, 1.2f, 0.8f, C_EMBER);
    s_outline(C_BLACK);
}

static void art_coin(Sprite *s)
{
    cv_begin(s, 14, 14);
    s_ell(7.0f, 7.0f, 5.4f, 5.4f, RP_GOLD, RP_GOLD_N, 0.3f);
    s_fell(7.0f, 7.0f, 3.3f, 3.3f, C_BROWN);
    s_ell(7.0f, 7.0f, 3.0f, 3.0f, RP_GOLD, RP_GOLD_N - 1, 0.0f);
    s_fline(7.0f, 5.0f, 7.0f, 9.0f, 0.5f, C_BROWN);
    s_fell(5.0f, 4.6f, 1.1f, 0.8f, C_CREAM);
    s_outline(C_BLACK);
}

/* ---- blit: sprites are placed at screen pixels ---- */
enum { M_NORMAL, M_FLASH, M_FROZEN, M_DIM, M_GHOST };
static int blit_add;                         /* ambient light: palette offset added to every sprite pixel (0, PB, 2*PB, ...) */
static uint8_t FROZEN_MAP[PB], DIM_MAP[PB];

static void sprites_init(void)
{
    if (sprites_ready) return;
    trig_init();
    for (int c = 0; c < PB; c++) {                           /* frozen / dimmed versions: remap by brightness */
        float lum = (0.3f * (float)game_palette[c][0] + 0.59f * (float)game_palette[c][1] + 0.11f * (float)game_palette[c][2]) / 255.0f;
        int k = (int)(lum * 4.99f);
        static const uint8_t ice[5] = {C_NAVY, C_DKBLUE, C_BLUE, C_CYAN, C_ICE};
        FROZEN_MAP[c] = ice[k];
        DIM_MAP[c] = k < 2 ? C_BLACK : (k < 3 ? C_DKGRAY : C_GRAY);
    }
    for (int f = 0; f < NF; f++) {
        art_slime(&ESPR[EN_SLIME][f], f);   art_bat(&ESPR[EN_BAT][f], f);      art_golem(&ESPR[EN_GOLEM][f], f);
        art_zombie(&ESPR[EN_ZOMBIE][f], f); art_pumpz(&ESPR[EN_PUMPKIN][f], f); art_witch(&ESPR[EN_WITCH][f], f);
        art_cat(&ESPR[EN_CAT][f], f);       art_ghost(&ESPR[EN_GHOST][f], f);   art_boss(&ESPR[EN_BOSS][f], f);
        art_portal(&SPR[S_PORTAL0 + f], f);
    }
    for (int t = 0; t < TW_COUNT; t++) for (int k = 0; k < 3; k++) art_tower(&SPR[S_TOWER + t * 3 + k], t, k);
    art_tree(&SPR[S_TREE]); art_dead(&SPR[S_DEAD]); art_rock(&SPR[S_ROCK]); art_grave(&SPR[S_GRAVE]); art_lamp(&SPR[S_LAMP]);
    art_skel(&SPR[S_SKEL0], 0); art_skel(&SPR[S_SKEL1], 1);
    for (int i = 0; i < 3; i++) art_pumpkin(&SPR[S_PUMP0 + i], i);
    art_heart(&SPR[S_HEART]); art_coin(&SPR[S_COIN]);
    art_house(&HOUSE);
    sprites_ready = 1;
}

static void blit(uint8_t *fb, const Sprite *s, int x, int y, int mode)
{
    uint8_t map[PB];
    for (int c = 0; c < PB; c++) map[c] = (uint8_t)c;
    if (mode == M_FROZEN) memcpy(map, FROZEN_MAP, PB);
    else if (mode == M_DIM) memcpy(map, DIM_MAP, PB);
    else if (mode == M_FLASH) memset(map, C_WHITE, PB);
    int add = (mode == M_FLASH) ? 0 : blit_add;
    int j0 = y < 0 ? -y : 0, j1 = (y + s->h > SCREEN_H) ? SCREEN_H - y : s->h;
    int i0 = x < 0 ? -x : 0, i1 = (x + s->w > SCREEN_W) ? SCREEN_W - x : s->w;
    for (int j = j0; j < j1; j++) {
        uint8_t *d = fb + (y + j) * SCREEN_W + x;
        const uint8_t *r = s->d + j * s->w;
        for (int i = i0; i < i1; i++) {
            uint8_t c = r[i];
            if (c == 255) continue;
            if (mode == M_GHOST && ((i + j) & 1)) continue;
            d[i] = (uint8_t)(map[c] + add);
        }
    }
}

/* a sprite magnified k times (title screen) */
static void blit_scaled(uint8_t *fb, const Sprite *s, int x, int y, int k)
{
    for (int j = 0; j < s->h; j++)
        for (int i = 0; i < s->w; i++) {
            int c = s->d[j * s->w + i];
            if (c != 255) hrect(fb, x + i * k, y + j * k, k, k, c);
        }
}

/* ---- the haunted house at the end of the road: drawn on a canvas whose tile origin is (HOUSE_OX, HOUSE_OY) ---- */
#define HOUSE_OX 8
#define HOUSE_OY 18
#define HOUSE_W  48
#define HOUSE_H  52
static void art_house(Sprite *s)
{
    cv_begin(s, HOUSE_W, HOUSE_H);
    float X = (float)HOUSE_OX, Y = (float)HOUSE_OY;
    s_ell(X + 16.0f, Y + 31.0f, 17.0f, 2.6f, RP_IRON, RP_IRON_N, 0.0f);                              /* foundation */
    float wall[8] = {X + 3.0f, Y + 14.0f, X + 30.0f, Y + 14.0f, X + 30.0f, Y + 31.0f, X + 3.0f, Y + 31.0f};
    s_poly(wall, 4, RP_STONE, RP_STONE_N, 0.62f, 0.18f, 0.5f);
    for (int k = 0; k < 6; k++) s_fline(X + 3.0f, Y + 17.0f + (float)k * 3.0f, X + 30.0f, Y + 17.0f + (float)k * 3.0f, 0.3f, C_ROCK0);
    for (int k = 0; k < 5; k++) { uint32_t h = hash2(k, 5, 9); s_fline(X + 5.0f + (float)(h % 24), Y + 14.5f + (float)k * 3.0f, X + 5.0f + (float)(h % 24), Y + 17.0f + (float)k * 3.0f, 0.3f, C_ROCK0); }
    float side[8] = {X + 24.0f, Y + 14.0f, X + 30.0f, Y + 14.0f, X + 30.0f, Y + 31.0f, X + 24.0f, Y + 31.0f};
    s_poly(side, 4, RP_IRON, RP_IRON_N, 0.55f, 0.2f, 0.0f);                                          /* shaded side */
    float ch[8] = {X + 22.5f, Y - 3.0f, X + 28.0f, Y - 3.0f, X + 28.0f, Y + 10.0f, X + 22.5f, Y + 8.0f};
    s_poly(ch, 4, RP_STONE, RP_STONE_N, 0.5f, 0.15f, 0.7f);                                          /* chimney */
    s_ell(X + 25.3f, Y - 3.0f, 3.4f, 1.3f, RP_STONE, RP_STONE_N, 0.2f);
    s_fell(X + 25.3f, Y - 3.2f, 2.1f, 0.7f, C_BLACK);
    float roof[6] = {X - 3.5f, Y + 17.0f, X + 15.0f, Y - 12.0f, X + 33.5f, Y + 17.0f};
    s_poly(roof, 3, RP_CLOAK, RP_CLOAK_N, 0.85f, 0.18f, 0.7f);
    for (int k = 0; k < 8; k++) {                                                                      /* shingles */
        float y = Y - 7.0f + (float)k * 3.2f, half = (y - (Y - 12.0f)) / 29.0f * 18.5f;
        s_fline(X + 15.0f - half, y, X + 15.0f + half, y, 0.32f, C_BLACK);
        for (int j = 0; j * 4 < (int)(half * 2.0f); j++) s_fline(X + 15.0f - half + (float)(j * 4 + (k & 1) * 2), y, X + 15.0f - half + (float)(j * 4 + (k & 1) * 2), y + 3.0f, 0.25f, C_VIO0);
    }
    s_fline(X - 3.5f, Y + 17.0f, X + 15.0f, Y - 12.0f, 0.5f, C_DKPURPLE);                           /* lit left rake */
    s_fline(X - 4.5f, Y + 17.5f, X + 34.5f, Y + 17.5f, 0.7f, C_BLACK);                              /* eaves */
    s_ell(X + 15.0f, Y + 3.5f, 2.6f, 3.2f, RP_IRON, RP_IRON_N, 0.0f);                                 /* attic window */
    s_fell(X + 15.0f, Y + 3.5f, 1.8f, 2.4f, C_DKBLUE);
    for (int w = 0; w < 2; w++) {                                                                      /* windows */
        float wx = X + (w ? 21.0f : 6.0f);
        float fr[8] = {wx - 1.0f, Y + 18.6f, wx + 7.0f, Y + 18.6f, wx + 7.0f, Y + 27.0f, wx - 1.0f, Y + 27.0f};
        s_poly(fr, 4, RP_WOOD, RP_WOOD_N, 0.6f, 0.2f, 0.4f);
        float gl[8] = {wx, Y + 19.6f, wx + 6.0f, Y + 19.6f, wx + 6.0f, Y + 26.0f, wx, Y + 26.0f};
        s_fpoly(gl, 4, C_DKBLUE);
        s_fline(wx + 3.0f, Y + 19.6f, wx + 3.0f, Y + 26.0f, 0.35f, C_WOOD0);
        s_fline(wx, Y + 22.8f, wx + 6.0f, Y + 22.8f, 0.35f, C_WOOD0);
        float sh[8] = {wx - 3.0f, Y + 18.6f, wx - 1.0f, Y + 18.6f, wx - 1.0f, Y + 27.0f, wx - 3.4f, Y + 26.0f};
        float sh2[8] = {wx + 7.0f, Y + 18.6f, wx + 9.0f, Y + 18.6f, wx + 9.4f, Y + 26.0f, wx + 7.0f, Y + 27.0f};
        if (w == 0) s_poly(sh, 4, RP_WOOD, RP_WOOD_N, 0.5f, 0.2f, 0.3f); else s_poly(sh2, 4, RP_WOOD, RP_WOOD_N, 0.5f, 0.2f, 0.3f);
    }
    float dr[8] = {X + 12.5f, Y + 21.0f, X + 19.5f, Y + 21.0f, X + 19.5f, Y + 31.0f, X + 12.5f, Y + 31.0f};
    s_poly(dr, 4, RP_WOOD, RP_WOOD_N, 0.45f, 0.15f, 0.5f);                                           /* boarded door */
    s_ell(X + 16.0f, Y + 21.0f, 3.5f, 1.4f, RP_WOOD, RP_WOOD_N, 0.0f);
    s_fline(X + 12.0f, Y + 24.5f, X + 20.0f, Y + 25.5f, 0.7f, C_DKBROWN);
    s_fline(X + 12.0f, Y + 28.0f, X + 20.0f, Y + 27.2f, 0.7f, C_DKBROWN);
    s_fell(X + 18.2f, Y + 27.0f, 0.55f, 0.55f, C_YELLOW);
    s_ell(X + 16.0f, Y + 31.6f, 6.5f, 1.4f, RP_STONE, RP_STONE_N, 0.2f);                              /* step */
    s_cap(X + 0.5f, Y + 31.0f, 1.0f, X + 2.0f, Y + 25.0f, 0.5f, RP_DEAD, RP_DEAD_N, 0.5f);            /* dead bush */
    s_cap(X + 1.5f, Y + 28.0f, 0.5f, X - 2.0f, Y + 24.0f, 0.3f, RP_DEAD, RP_DEAD_N, 0.5f);
    s_cap(X + 1.5f, Y + 28.0f, 0.5f, X + 4.0f, Y + 22.5f, 0.3f, RP_DEAD, RP_DEAD_N, 0.5f);
    for (int k = 0; k < 3; k++) s_fline(X + 22.0f + (float)k * 3.0f, Y + 31.0f, X + 22.0f + (float)k * 3.0f, Y + 27.5f, 0.45f, C_DKSTONE);   /* fence */
    s_fline(X + 21.0f, Y + 28.5f, X + 29.5f, Y + 28.5f, 0.3f, C_DKSTONE);
    s_fline(X + 6.2f, Y + 19.6f, X + 9.5f, Y + 22.0f, 0.2f, C_LIGHT);                               /* cobweb */
    s_fline(X + 6.2f, Y + 19.6f, X + 8.0f, Y + 23.0f, 0.2f, C_LIGHT);
    s_outline(C_BLACK);
}
#define HOUSE_WIN1_X 7
#define HOUSE_WIN2_X 22
#define HOUSE_WIN_Y  20
#define HOUSE_WIN_W  4
#define HOUSE_WIN_H  5
