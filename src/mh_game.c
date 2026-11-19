#include "mh_game.h"
#include <math.h>
#include <string.h>

static uint32_t rng_next(uint32_t *s)
{
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return *s;
}
static float rng_f(uint32_t *s, float lo, float hi)
{
    return lo + (hi - lo) * (float)(rng_next(s) & 0xFFFFFF) / (float)0x1000000;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float mh_self_right(float target)
{
    float m = fabsf(target) - MH_RIGHTING_DEADZONE;
    if (m <= 0.0f) return 0.0f;
    return target > 0.0f ? m : -m;
}

static float wrapx(float x)
{
    while (x < 0.0f) x += MH_W;
    while (x >= MH_W) x -= MH_W;
    return x;
}

// Pad widths: wide 1x down to the narrow 5X. The lander's feet span 22 px.
static const struct { int mult; float w; } k_pads[MH_PADS] = {
    { 1, 64.0f }, { 2, 46.0f }, { 4, 34.0f }, { 5, 27.0f } };

void mh_gen_terrain(mh_game_t *g, uint32_t seed)
{
    uint32_t s = seed ? seed : 0x1234567u;
    g->seed = seed;

    // Place the four pads left to right in random order, never overlapping,
    // with jagged ground between them and around the wrap seam.
    int order[MH_PADS] = { 0, 1, 2, 3 };
    for (int i = MH_PADS - 1; i > 0; i--) {
        int j = (int)(rng_next(&s) % (uint32_t)(i + 1));
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }
    float slot = (float)MH_W / MH_PADS;
    for (int k = 0; k < MH_PADS; k++) {
        float w = k_pads[order[k]].w;
        float lo = k * slot + 18.0f, hi = (k + 1) * slot - 18.0f - w;
        float x0 = rng_f(&s, lo, hi);
        g->pad[k].x0 = x0;
        g->pad[k].x1 = x0 + w;
        g->pad[k].y = rng_f(&s, MH_H - 110.0f, MH_H - 25.0f);
        g->pad[k].mult = k_pads[order[k]].mult;
        if (g->pad[k].mult == 5) g->pad[k].y = rng_f(&s, MH_H - 140.0f, MH_H - 90.0f); // up on a peak
    }

    // Vertices: seam at x=0 and x=MH_W share a height so the wrap is seamless.
    int n = 0;
    float seam = rng_f(&s, MH_H - 120.0f, MH_H - 30.0f);
    g->vx_[n] = 0.0f; g->vy_[n++] = seam;
    float x = 0.0f;
    for (int k = 0; k <= MH_PADS; k++) {
        float stop = k < MH_PADS ? g->pad[k].x0 : (float)MH_W;
        // jagged run up to the next pad (or the seam)
        for (;;) {
            float step = rng_f(&s, 8.0f, 16.0f);
            if (x + step > stop - 6.0f) break;
            x += step;
            float y = rng_f(&s, MH_H - 150.0f, MH_H - 6.0f);
            g->vx_[n] = x; g->vy_[n++] = y;
            if (n >= MH_MAX_VERTS - 6) break;
        }
        if (k < MH_PADS) {
            g->vx_[n] = g->pad[k].x0; g->vy_[n++] = g->pad[k].y;
            g->vx_[n] = g->pad[k].x1; g->vy_[n++] = g->pad[k].y;
            x = g->pad[k].x1;
        }
    }
    g->vx_[n] = (float)MH_W; g->vy_[n++] = seam;
    g->nverts = n;
}

float mh_terrain_y(const mh_game_t *g, float x)
{
    x = wrapx(x);
    for (int i = 1; i < g->nverts; i++) {
        if (x <= g->vx_[i]) {
            float x0 = g->vx_[i - 1], x1 = g->vx_[i];
            float t = x1 > x0 ? (x - x0) / (x1 - x0) : 0.0f;
            return g->vy_[i - 1] + t * (g->vy_[i] - g->vy_[i - 1]);
        }
    }
    return g->vy_[g->nverts - 1];
}

const mh_pad_t *mh_pad_at(const mh_game_t *g, float a, float b)
{
    a = wrapx(a); b = wrapx(b);
    for (int k = 0; k < MH_PADS; k++) {
        const mh_pad_t *p = &g->pad[k];
        if (a >= p->x0 && a <= p->x1 && b >= p->x0 && b <= p->x1) return p;
    }
    return 0;
}

float mh_altitude(const mh_game_t *g)
{
    float feet = g->y + MH_FOOT_Y;
    float ground = fminf(mh_terrain_y(g, g->x - MH_FOOT_X),
                         fminf(mh_terrain_y(g, g->x), mh_terrain_y(g, g->x + MH_FOOT_X)));
    return ground - feet;
}

void mh_spawn(mh_game_t *g)
{
    uint32_t s = g->seed * 2654435761u + (uint32_t)g->stage * 977u + 1u;
    g->x = rng_f(&s, 20.0f, 120.0f);
    g->y = 22.0f;
    g->vx = rng_f(&s, 14.0f, 26.0f);
    g->vy = 0.0f;
    g->angle = 0.0f;
    g->thrusting = g->emerg_firing = false;
    g->emerg_t = 0.0f;
    g->emerg_armed = true;
    g->phase = MH_FLYING;
    g->phase_t = 0.0f;
}

void mh_new_game(mh_game_t *g, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->fuel = MH_FUEL_START;
    mh_gen_terrain(g, seed);
    mh_spawn(g);
}

// Points of the lander (body frame, y down) tested against the terrain.
static const float k_hull[][2] = {
    { -MH_FOOT_X, MH_FOOT_Y }, { MH_FOOT_X, MH_FOOT_Y },     // feet
    { -9.0f, 7.0f }, { 9.0f, 7.0f }, { -4.0f, 6.0f }, { 4.0f, 6.0f }, // legs
    { 0.0f, 7.0f },                                             // engine bell
    { -7.0f, 3.0f }, { 7.0f, 3.0f }, { -6.0f, -10.0f }, { 6.0f, -10.0f }, { 0.0f, -13.0f },
};

static void to_world(const mh_game_t *g, float bx, float by, float *wx, float *wy)
{
    float c = cosf(g->angle), s = sinf(g->angle);
    *wx = g->x + bx * c - by * s;
    *wy = g->y + bx * s + by * c;
}

static void explode(mh_game_t *g)
{
    uint32_t s = g->seed ^ (uint32_t)(g->time * 1000.0f) ^ 0x9E3779B9u;
    for (int i = 0; i < MH_DEBRIS; i++) {
        mh_debris_t *d = &g->debris[i];
        float dir = rng_f(&s, 0.0f, 6.2832f), sp = rng_f(&s, 20.0f, 70.0f);
        d->x = g->x + rng_f(&s, -8, 8);
        d->y = g->y + rng_f(&s, -8, 8);
        d->vx = cosf(dir) * sp + g->vx * 0.3f;
        d->vy = sinf(dir) * sp - 25.0f;
        d->a = rng_f(&s, 0.0f, 6.2832f);
        d->va = rng_f(&s, -8.0f, 8.0f);
        d->len = rng_f(&s, 4.0f, 11.0f);
    }
}

void mh_step(mh_game_t *g, float dt, const mh_input_t *in)
{
    g->crash_event = g->land_event = false;

    if (g->phase == MH_GAMEOVER) return;
    if (g->phase != MH_FLYING) {
        g->phase_t += dt;
        if (g->phase == MH_CRASHED) {
            for (int i = 0; i < MH_DEBRIS; i++) {
                mh_debris_t *d = &g->debris[i];
                d->vy += MH_GRAVITY * 4.0f * dt;
                d->x += d->vx * dt; d->y += d->vy * dt; d->a += d->va * dt;
            }
        }
        if (g->phase_t >= MH_STAGE_PAUSE) {
            if (g->phase == MH_LANDED) {
                g->stage++;
                mh_gen_terrain(g, g->seed * 1103515245u + 12345u);
                mh_spawn(g);
            } else if (g->fuel <= 0.0f) {
                g->phase = MH_GAMEOVER;
            } else {
                mh_spawn(g);                      // try again on the same terrain
            }
        }
        return;
    }

    g->time += dt;

    // Emergency burst: max MH_EMERG_MAX_S per press; release to rearm.
    if (!in->emergency) { g->emerg_armed = true; g->emerg_t = 0.0f; }
    bool emerg = in->emergency && g->emerg_armed && g->fuel > 0.0f;
    if (emerg) {
        g->emerg_t += dt;
        if (g->emerg_t >= MH_EMERG_MAX_S) { emerg = false; g->emerg_armed = false; }
    }
    bool normal = !emerg && in->thrust && g->fuel > 0.0f;
    g->emerg_firing = emerg;
    g->thrusting = normal;

    // Rotation follows the steering target at a limited rate. Bottom-heavy:
    // with the engine off, small tilts are ignored and the lander rights itself.
    float tgt = clampf(in->rot_target, -MH_ROT_MAX, MH_ROT_MAX);
    if (!emerg && !normal) tgt = mh_self_right(tgt);
    g->angle += clampf(tgt - g->angle, -MH_ROT_RATE * dt, MH_ROT_RATE * dt);

    float a = 0.0f;
    if (emerg) { a = MH_THRUST * MH_EMERG_MULT; g->fuel -= MH_BURN * MH_EMERG_BURN_MULT * dt; }
    else if (normal) { a = MH_THRUST; g->fuel -= MH_BURN * dt; }
    if (g->fuel < 0.0f) g->fuel = 0.0f;

    g->vx += a * sinf(g->angle) * dt;
    g->vy += (MH_GRAVITY - a * cosf(g->angle)) * dt;
    g->x = wrapx(g->x + g->vx * dt);
    g->y += g->vy * dt;
    if (g->y < 0.0f) { g->y = 0.0f; if (g->vy < 0.0f) g->vy = 0.0f; }   // ceiling

    // Contact: any hull point at or below the surface.
    bool contact = false;
    for (unsigned i = 0; i < sizeof k_hull / sizeof k_hull[0]; i++) {
        float wx, wy;
        to_world(g, k_hull[i][0], k_hull[i][1], &wx, &wy);
        if (wy >= mh_terrain_y(g, wx)) { contact = true; break; }
    }
    if (!contact) return;

    float lx, ly, rx, ry;
    to_world(g, -MH_FOOT_X, MH_FOOT_Y, &lx, &ly);
    to_world(g,  MH_FOOT_X, MH_FOOT_Y, &rx, &ry);
    const mh_pad_t *p = mh_pad_at(g, lx, rx);
    bool soft = p && fabsf(g->angle) <= MH_SAFE_ANGLE &&
                g->vy <= MH_SAFE_VY && fabsf(g->vx) <= MH_SAFE_VX;
    g->phase_t = 0.0f;
    g->thrusting = g->emerg_firing = false;
    if (soft) {
        g->phase = MH_LANDED;
        g->land_event = true;
        g->last_mult = p->mult;
        g->score += 100u * (uint32_t)p->mult;
        g->fuel += MH_LAND_BONUS;
        g->y = p->y - MH_FOOT_Y;
        g->angle = 0.0f;
    } else {
        g->phase = MH_CRASHED;
        g->crash_event = true;
        g->fuel -= MH_CRASH_FUEL;
        if (g->fuel < 0.0f) g->fuel = 0.0f;
        explode(g);
    }
    g->vx = g->vy = 0.0f;
}
