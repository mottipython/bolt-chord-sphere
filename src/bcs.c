/*
 * bolt-chord-sphere: screensaver of the three things an avout owns, from "Anathem".
 *
 * A fraa in the cloister shows what a bolt, a chord and a sphere are good for: the
 * sphere as pillow, seat, lamp, bowl, shield and pill, the bolt thin and thick, and
 * the chord lifting a stone.
 *
 *   bolt-chord-sphere            full screen on every display; any key or mouse movement quits
 *   bolt-chord-sphere --window   in a window; Esc or Q quits
 *   BoltChordSphere.scr /s       Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures (drawn with Gemini, prepared by tools/make_sprites.py) are built
 * into the program as PNG and JPEG files and decoded at start-up. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* else Windows builds need libwinpthread-1.dll */
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(__APPLE__)
/* Mach-O: its own section names, and C names carry a leading underscore */
#define ART_SECTION ".section __DATA,__const\n"
#define ART_SYM "_art_"
#elif defined(_WIN32)
#define ART_SECTION ".section .rdata,\"dr\"\n"
#define ART_SYM "art_"
#else
#define ART_SECTION ".section .rodata\n"
#define ART_SYM "art_"
#endif
#define ART(name, file) \
    __asm__(ART_SECTION ".globl " ART_SYM #name "\n" ART_SYM #name ":\n.incbin \"assets/art/" file "\"\n" \
            ".globl " ART_SYM #name "_end\n" ART_SYM #name "_end:\n.text\n"); \
    extern const unsigned char art_##name[], art_##name##_end[];
ART(cloister, "cloister.jpg")
ART(banner, "banner.png")
ART(glow, "glow.jpg")
ART(sphere, "sphere.png")
ART(sphere_lit, "sphere_lit.png")
ART(bowl, "bowl.png")
ART(block, "block.png")
ART(apple, "apple.png")
ART(pillow, "pillow.png")
ART(sleep, "sleep.png")
ART(twist, "twist.png")
ART(sit, "sit.png")
ART(eat, "eat.png")
ART(hot, "hot.png")
ART(cold, "cold.png")
ART(watch, "watch.png")
ART(shield, "shield.png")
ART(walk, "walk.png")

enum { SP_CLOISTER, SP_BANNER, SP_GLOW, SP_SPHERE, SP_SPHERE_LIT, SP_BOWL, SP_BLOCK, SP_APPLE, SP_PILLOW, SP_SLEEP, SP_TWIST, SP_SIT, SP_EAT, SP_HOT, SP_COLD, SP_WATCH, SP_SHIELD, SP_WALK, NSPRITE };
static struct { const unsigned char *file, *end; int w, h; unsigned char *px; } sprite[NSPRITE] = {
    {art_cloister, art_cloister_end},
    {art_banner, art_banner_end},
    {art_glow, art_glow_end},
    {art_sphere, art_sphere_end},
    {art_sphere_lit, art_sphere_lit_end},
    {art_bowl, art_bowl_end},
    {art_block, art_block_end},
    {art_apple, art_apple_end},
    {art_pillow, art_pillow_end},
    {art_sleep, art_sleep_end},
    {art_twist, art_twist_end},
    {art_sit, art_sit_end},
    {art_eat, art_eat_end},
    {art_hot, art_hot_end},
    {art_cold, art_cold_end},
    {art_watch, art_watch_end},
    {art_shield, art_shield_end},
    {art_walk, art_walk_end},
};
static SDL_Texture **cur_tex; /* the pictures, for the renderer being drawn to */

static void load_sprites(void)
{
    for (int i = 0; i < NSPRITE; i++) {
        int n;
        sprite[i].px = stbi_load_from_memory(sprite[i].file, (int)(sprite[i].end - sprite[i].file), &sprite[i].w, &sprite[i].h, &n, 4);
        if (!sprite[i].px) { sprite[i].w = sprite[i].h = 1; }
    }
}
static void make_textures(SDL_Renderer *r, SDL_Texture **tex)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    for (int i = 0; i < NSPRITE; i++) {
        tex[i] = NULL;
        if (!sprite[i].px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sprite[i].w, sprite[i].h);
        if (!tex[i]) continue;
        SDL_UpdateTexture(tex[i], NULL, sprite[i].px, sprite[i].w * 4);
        /* the glow is light painted on black, and is added to what is behind it */
        SDL_SetTextureBlendMode(tex[i], i == SP_GLOW ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    }
}

/* ---- options ---- */
static int opt_window = 0;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {16, 13, 10}, INK = {46, 32, 22}, CHORD = {150, 44, 30}, PAPER = {253, 246, 226};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad4(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                  SDL_Color c0, SDL_Color c1, SDL_Color c2, SDL_Color c3)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c0); vert(x1, y1, c1); vert(x2, y2, c2); vert(x3, y3, c3);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    quad4(x0, y0, x1, y1, x2, y2, x3, y3, c, c, c, c);
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
__attribute__((unused)) static void ring(double cx, double cy, double r, double w, SDL_Color c)
{
    for (int i = 0; i < 64; i++) {
        double a = i * M_PI / 32, b = (i + 1) * M_PI / 32;
        line(cx + cos(a) * r, cy + sin(a) * r, cx + cos(b) * r, cy + sin(b) * r, w, c);
    }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

static SDL_Renderer *ren; /* where the current frame is being drawn */

/* a picture stretched over a rectangle; sx0..sx1 picks a slice of it across (0..1) */
static void pic_part(int which, double x, double y, double w, double h, double sx0, double sx1, double tilt, int flip)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    SDL_Rect src = {(int)(sx0 * sprite[which].w), 0, (int)((sx1 - sx0) * sprite[which].w), sprite[which].h};
    SDL_FRect dst = {(float)x, (float)y, (float)w, (float)h};
    SDL_FPoint pivot = {(float)(w / 2), (float)h};
    SDL_RenderCopyExF(ren, cur_tex[which], &src, &dst, tilt, &pivot, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
/* a picture `tall` high, standing with the point (ax, ay) of it (0..1 across and down) at (x, y) */
__attribute__((unused)) static void pic(int which, double x, double y, double tall, double ax, double ay)
{
    double w = tall * sprite[which].w / sprite[which].h;
    pic_part(which, x - w * ax, y - tall * ay, w, tall, 0, 1, 0, 0);
}

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26", "+03 43|21 25", "=02 42|04 44",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}
/* ---- the scenes ----
 *
 * An avout owns three things: a bolt, a chord and a sphere. Eight short scenes show
 * what they are good for. The fraa is a set of drawings, one for each thing he does;
 * the sphere, the bowl, the block and the apple are separate drawings that the
 * program moves, swells, squashes and lights. */
static double T, anim_t;
static uint64_t seed_state;
#define SCENE 13.0
#define NSCENE 8
#define ROUND (SCENE * NSCENE)
#define NFLAKE 90
static struct { double x, y, v, sway; } flake[NFLAKE];

static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double frand(void) { return (next_rand() >> 11) * (1.0 / 9007199254740992.0); }
static double ramp(double t, double a, double b) { return t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a); }
static double ease(double x) { return x * x * (3 - 2 * x); }
static double go(double t, double a, double b) { return ease(ramp(t, a, b)); }
/* 0 to 1 with a little spring at the end, for things that swell or pop */
static double spring(double t, double a, double b)
{
    double x = ramp(t, a, b);
    return x >= 1 ? 1 : 1 - cos(x * M_PI * 2.5) * exp(-x * 4) * (1 - x) - (1 - x) * (1 - x) * 0;
}

static void new_round(void)
{
    T = 0;
    for (int i = 0; i < NFLAKE; i++) { flake[i].x = frand(); flake[i].y = frand(); flake[i].v = 0.05 + 0.06 * frand(); flake[i].sway = frand() * 6.28; }
}

static void update(double dt)
{
    anim_t += dt;
    T += dt * opt_speed;
    if (T > ROUND) new_round();
    for (int i = 0; i < NFLAKE; i++) { flake[i].y += flake[i].v * dt * opt_speed; if (flake[i].y > 1) { flake[i].y -= 1; flake[i].x = frand(); } }
}

/* ---- drawing ---- */
static double unit;
static int sw, sh;
#define GROUND 0.86 /* where feet go, as a fraction of the height of the picture */
static const char *say1, *say2;

/* the fraa standing on the ground at x (a fraction of the width). All the drawings
 * of him are to one scale, so he is the same size whatever he is doing. */
#define FRAA 0.44 /* how tall he stands, as a fraction of the height of the picture */
static double tall_of(int which) { return FRAA * sprite[which].h / 520.0; }
static void stand(int which, double x, double scale, double lift, int flip)
{
    double hh = sh * tall_of(which) * scale, ww = hh * sprite[which].w / sprite[which].h;
    disc(sw * x, sh * GROUND, ww * 0.42, col(INK, 0.28), col(INK, 0));
    pic_part(which, sw * x - ww / 2, sh * (GROUND - lift) - hh, ww, hh, 0, 1, 0, flip);
}
/* a thing `d` of the screen height across, its centre at (x, y), squashed by `squash` */
static void ball(int which, double x, double y, double d, double squash, double turn)
{
    double ww = sh * d * (2 - squash), hh = sh * d * squash * sprite[which].h / sprite[which].w;
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    SDL_FRect dst = {(float)(sw * x - ww / 2), (float)(sh * y - hh / 2), (float)ww, (float)hh};
    SDL_RenderCopyExF(ren, cur_tex[which], NULL, &dst, turn, NULL, SDL_FLIP_NONE);
}
static void shadow(double x, double d, double strength)
{
    disc(sw * x, sh * GROUND, sh * d * 0.55, col(INK, 0.3 * strength), col(INK, 0));
}
static void glow(double x, double y, double d, double alpha)
{
    flush(ren);
    if (!cur_tex || !cur_tex[SP_GLOW] || alpha <= 0) return;
    SDL_FRect dst = {(float)(sw * x - sh * d / 2), (float)(sh * y - sh * d / 2), (float)(sh * d), (float)(sh * d)};
    SDL_SetTextureAlphaMod(cur_tex[SP_GLOW], (Uint8)(fmin(alpha, 1) * 255));
    SDL_RenderCopyF(ren, cur_tex[SP_GLOW], NULL, &dst);
    SDL_SetTextureAlphaMod(cur_tex[SP_GLOW], 255);
}
static void tint(Uint8 r, Uint8 g, Uint8 b, double alpha)
{
    if (alpha > 0) { rect(0, 0, sw, sh, (SDL_Color){r, g, b, (Uint8)(fmin(alpha, 1) * 255)}); flush(ren); }
}
static double ASP; /* height over width of the screen, to turn heights into widths */

static void scene_pillow(double t)
{
    double breathe = sin(anim_t * 1.4) * 0.012, tall = tall_of(SP_SLEEP), wide = tall * sprite[SP_SLEEP].w / sprite[SP_SLEEP].h * ASP;
    double hx = 0.5 - wide * 0.36, d = 0.13;
    shadow(hx, d, 1);
    ball(SP_PILLOW, hx, GROUND - d * 0.26, d * 1.25, 1 + breathe, 0);
    stand(SP_SLEEP, 0.5, 1, 0.012, 0);
    for (int i = 0; i < 3; i++) { /* he snores */
        double q = fmod(anim_t * 0.35 + i / 3.0, 1);
        text("Z", sw * (hx + 0.02 + q * 0.05), sh * (GROUND - 0.2 - q * 0.16), unit * (0.25 + 0.3 * q), 1, col(PAPER, sin(q * M_PI)));
    }
    say1 = "THE SPHERE: A PILLOW"; say2 = "MADE SOFT, AND JUST THE SIZE FOR A HEAD";
    (void)t;
}

static void scene_seat(double t)
{
    double grow = spring(t, 1.5, 4.5), d = 0.07 + 0.2 * grow, bx = 0.57;
    if (t < 6) {
        shadow(bx, d, 1);
        ball(SP_SPHERE, bx, GROUND - d / 2, d, 1 + 0.06 * sin(t * 9) * (grow < 1 && t > 1.5), anim_t * 0);
        stand(SP_TWIST, 0.42, 1, 0, 0);
        say1 = "A TWIST OF THE OPEN HAND"; say2 = "AND THE SPHERE GROWS";
    } else {
        double settle = 1 - 0.12 * exp(-(t - 6) * 4) * cos((t - 6) * 12); /* it gives a little as he sits */
        double d = 0.27, top = d * settle;
        shadow(0.49, d, 1);
        ball(SP_SPHERE, 0.49, GROUND - top / 2, d, settle, 0);
        /* his seat is about two-thirds of the way down the drawing of him, so that is
         * what goes on top of the sphere, a little sunk in, with his legs hanging in front */
        stand(SP_SIT, 0.5, 1, top - tall_of(SP_SIT) * 0.4, 0);
        say1 = "THE SPHERE: A SEAT"; say2 = "FIRM ENOUGH TO SIT ON, AND ANY HEIGHT YOU LIKE";
    }
}

static void scene_lamp(double t)
{
    double dark = go(t, 0.5, 3), lit = go(t, 3.5, 5.5), d = 0.1, bx = 0.5 + FRAA * 0.36 * ASP, by = GROUND - FRAA * 0.44 + sin(anim_t * 1.7) * 0.004;
    stand(SP_TWIST, 0.5, 1, 0, 0);
    flush(ren);
    tint(10, 14, 40, 0.62 * dark);
    glow(bx, by, 1.1, 0.22 * lit * (0.93 + 0.07 * sin(anim_t * 5)));
    if (lit > 0.02) { /* he and the ground about him are lit again */
        flush(ren);
        if (cur_tex && cur_tex[SP_TWIST]) SDL_SetTextureAlphaMod(cur_tex[SP_TWIST], (Uint8)(lit * 150));
        stand(SP_TWIST, 0.5, 1, 0, 0);
        flush(ren);
        if (cur_tex && cur_tex[SP_TWIST]) SDL_SetTextureAlphaMod(cur_tex[SP_TWIST], 255);
    }
    ball(lit > 0.5 ? SP_SPHERE_LIT : SP_SPHERE, bx, by, d, 1, 0);
    glow(bx, by, 0.22, 0.3 * lit);
    say1 = "THE SPHERE: A LAMP"; say2 = lit > 0.5 ? "IT GLOWS WHEN IT IS WANTED" : "NIGHT COMES TO THE CLOISTER";
}

static void scene_bowl(double t)
{
    double nod = fabs(sin(anim_t * 2.6)) * 0.006;
    stand(SP_EAT, 0.5, 1, nod, 0);
    shadow(0.5, 0.15, 0.8);
    ball(SP_BOWL, 0.5, GROUND - 0.04, 0.15, 1, 0);
    say1 = "THE SPHERE: A BOWL"; say2 = "PUSHED IN ON ONE SIDE, IT HOLDS HIS SUPPER";
    (void)t;
}

static void scene_bolt(double t)
{
    double cold = go(t, 4.5, 6.5), pop = spring(t, 6.2, 7.6);
    tint(255, 190, 90, 0.16 * (1 - cold));
    if (t < 6.2) stand(SP_HOT, 0.5, 1, 0, 0);
    else {
        stand(SP_COLD, 0.5, 0.8 + 0.2 * pop, 0, 0);
    }
    flush(ren);
    tint(120, 150, 200, 0.3 * cold);
    for (int i = 0; i < NFLAKE; i++) { /* snow */
        double x = flake[i].x + 0.012 * sin(anim_t * 1.3 + flake[i].sway), y = flake[i].y;
        disc(sw * x, sh * y, unit * (0.5 + 0.6 * flake[i].v * 8), col(PAPER, cold), col(PAPER, 0.75 * cold));
    }
    say1 = t < 6.2 ? "THE BOLT: THIN FOR THE HEAT" : "THE BOLT: THICK FOR THE COLD";
    say2 = t < 4.5 ? "ONE LENGTH OF CLOTH IS ALL HE WEARS" : t < 6.2 ? "BUT THE WEATHER TURNS" : "THE SAME CLOTH, MADE AS THICK AS HE LIKES";
}

static void scene_chord(double t)
{
    double up = go(t, 2.5, 9), bx = 0.62, bd = 0.2, by = GROUND - bd / 2 - 0.34 * up, sway = sin(anim_t * 1.6) * 0.004 * up;
    stand(SP_WATCH, 0.4, 1, 0, 0);
    shadow(bx, bd * (1 - 0.5 * up), 1 - 0.5 * up);
    /* the chord, from above down to the ring in the block */
    double x0 = sw * bx, x1 = sw * (bx + sway), y1 = sh * (by - bd * 0.5);
    line(x0, 0, x1, y1, unit * 0.9, col(INK, 1));
    line(x0, 0, x1, y1, unit * 0.55, col(CHORD, 1));
    for (double q = 0; q < y1; q += unit * 1.1) /* its twist, running up as it shortens */
        line(x0 + (x1 - x0) * q / y1 - unit * 0.16, fmod(q + anim_t * unit * 6 * (up > 0 && up < 1), y1), x0 + (x1 - x0) * q / y1 + unit * 0.16,
             fmod(q + anim_t * unit * 6 * (up > 0 && up < 1), y1) + unit * 0.4, unit * 0.12, col(INK, 0.55));
    flush(ren);
    ball(SP_BLOCK, bx + sway, by, bd, 1, sway * 400);
    say1 = "THE CHORD"; say2 = t < 2.5 ? "IT HOLDS HIS BOLT ON, AND MORE BESIDES" : up < 1 ? "IT CAN SHORTEN ITSELF, AND LIFT A STONE" : "AN HOUR IN THE SUN AND IT WILL DO IT AGAIN";
}

static void scene_shield(double t)
{
    double d = 0.26, bx = 0.5 + 0.09, by = GROUND - 0.2, hit = 0;
    stand(SP_SHIELD, 0.44, 1, 0, 0);
    for (int k = 0; k < 3; k++) { /* three apples, one after another */
        double t0 = 2 + k * 3.3, q = (t - t0) / 1.1, ax, ay;
        if (q < 0 || q > 2.2) continue;
        if (q < 1) { ax = 1.05 - (1.05 - bx - d * 0.5 * ASP) * q; ay = by - 0.25 * sin(q * M_PI) - 0.02 * k; }
        else { double r = q - 1; ax = bx + d * 0.5 * ASP + 0.22 * r; ay = by - 0.3 * r + 0.42 * r * r; hit = fmax(hit, exp(-r * 9)); }
        ball(SP_APPLE, ax, ay, 0.055, 1, q * 400);
    }
    ball(SP_SPHERE, bx - hit * 0.008, by, d, 1 - 0.1 * hit, 0);
    say1 = "THE SPHERE: A SHIELD"; say2 = "THOUGH NOT A VERY GOOD ONE";
}

static void scene_pill(double t)
{
    double shrink = go(t, 1.5, 5), d = 0.22 - 0.208 * shrink, hop = ramp(t, 5.4, 6.6), bx = 0.56, by = GROUND - d / 2;
    if (t < 7.2) {
        if (hop > 0) { bx = 0.56 - 0.13 * hop; by = GROUND - d / 2 - (0.24 * hop + 0.5 * hop * (1 - hop)) * 0.9; }
        stand(SP_TWIST, 0.42, 1, 0, 0);
        if (hop < 1) { shadow(0.56 - 0.13 * hop, d, 1 - hop); ball(SP_SPHERE, bx, by, d, 1, 0); }
        say1 = "THE SPHERE: A PILL"; say2 = hop > 0 ? "AND INTO A FOLD OF THE BOLT IT GOES" : "ANOTHER TWIST, AND IT SHRINKS TO ALMOST NOTHING";
    } else {
        double x = 0.42 + (t - 7.2) * 0.12, bob = fabs(sin(anim_t * 6)) * 0.008;
        stand(SP_WALK, x, 1, bob, 0);
        say1 = "BOLT, CHORD AND SPHERE"; say2 = "ALL THAT AN AVOUT OWNS, AND ALL HE NEEDS FOR THE ROAD";
    }
}

static void draw_caption(int w, int h, const char *l1, const char *l2)
{
    double s1 = unit * 0.46, s2 = unit * 0.3;
    double widest = fmax(text_width(l1, s1), text_width(l2, s2)), room = w * 0.6;
    if (widest > room) { s1 *= room / widest; s2 *= room / widest; }
    double bw = fmax(text_width(l1, s1), text_width(l2, s2)) + unit * 13, bh = unit * 11.5;
    double bx = w / 2.0 - bw / 2, by = unit * 1.2;
    double cap = 0.135, cw = bh * 1.15 * sprite[SP_BANNER].w * cap / sprite[SP_BANNER].h;
    pic_part(SP_BANNER, bx + cw - 1, by, bw - 2 * cw + 2, bh, cap, 1 - cap, 0, 0);
    pic_part(SP_BANNER, bx, by, cw, bh, 0, cap, 0, 0);
    pic_part(SP_BANNER, bx + bw - cw, by, cw, bh, 1 - cap, 1, 0, 0);
    text(l1, w / 2.0, by + bh * 0.25, s1, 0.95, col(INK, 1));
    text(l2, w / 2.0, by + bh * 0.25 + s1 * 6 + unit * 1.2, s2, 0.9, col(CHORD, 1));
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    ren = r; sw = w; sh = h; ASP = (double)h / w;
    unit = (w < h ? w : h) * 0.0125;
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    pic_part(SP_CLOISTER, 0, 0, w, h, 0, 1, 0, 0);
    int k = (int)(T / SCENE);
    if (k >= NSCENE) k = NSCENE - 1;
    double t = T - k * SCENE;
    say1 = say2 = "";
    switch (k) {
    case 0: scene_pillow(t); break;
    case 1: scene_seat(t); break;
    case 2: scene_lamp(t); break;
    case 3: scene_bowl(t); break;
    case 4: scene_bolt(t); break;
    case 5: scene_chord(t); break;
    case 6: scene_shield(t); break;
    default: scene_pill(t); break;
    }
    flush(r);
    draw_caption(w, h, say1, say2);
    flush(r);
    double fade = fmin(ramp(t, 0, 0.7), 1 - ramp(t, SCENE - 0.7, SCENE));
    if (fade < 1) { rect(0, 0, w, h, col(BG, 1 - fade)); flush(r); }
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene, *tex[NSPRITE]; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    cur_tex = v->tex;
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("Bolt, Chord and Sphere", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    make_textures(v->ren, v->tex);
    return 1;
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    anim_t = 0;
    new_round();
}

static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "bolt-chord-sphere: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex[NSPRITE];
    make_textures(r, tex);
    cur_tex = tex;
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "bolt-chord-sphere: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    static const char text[] =
        "Bolt, Chord and Sphere screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --speed X       animation speed, 1 = normal\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n";
    if (getenv("SDL_VIDEODRIVER") && !strcmp(getenv("SDL_VIDEODRIVER"), "dummy")) {
        fputs(text, stderr);
        return;
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Bolt, Chord and Sphere", text, NULL);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Bolt, Chord and Sphere", "Bolt, Chord and Sphere screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--time") && i + 1 < argc) {
            opt_time = SDL_atof(argv[++i]);
            if (opt_time < 0.0) opt_time = 0.0;
            if (opt_time > 86400.0) opt_time = 86400.0;
        }
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64 || opt_w > 8192 || opt_h > 8192) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_speed < 0.05) opt_speed = 0.05;
    load_sprites();

    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "bolt-chord-sphere: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "bolt-chord-sphere: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}



