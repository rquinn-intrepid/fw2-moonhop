// Host tests for src/mh_game.c: terrain, landing rules, scoring, fuel,
// emergency burst limit, wrap and game over.
#include <math.h>
#include <stdio.h>
#include "mh_game.h"

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL %s:%d ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static const mh_input_t k_idle = { 0.0f, false, false };

static const mh_pad_t *pad_with(const mh_game_t *g, int mult)
{
    for (int k = 0; k < MH_PADS; k++) if (g->pad[k].mult == mult) return &g->pad[k];
    return 0;
}

static void test_terrain(void)
{
    for (uint32_t seed = 1; seed < 400; seed++) {
        mh_game_t g; mh_new_game(&g, seed);
        CHECK(g.nverts >= 10 && g.nverts <= MH_MAX_VERTS, "seed %u nverts %d", seed, g.nverts);
        CHECK(g.vx_[0] == 0.0f && g.vx_[g.nverts - 1] == (float)MH_W, "seed %u ends", seed);
        CHECK(g.vy_[0] == g.vy_[g.nverts - 1], "seed %u seam not continuous", seed);
        for (int i = 1; i < g.nverts; i++)
            CHECK(g.vx_[i] >= g.vx_[i - 1], "seed %u x not ascending at %d", seed, i);
        int seen[6] = {0};
        for (int k = 0; k < MH_PADS; k++) {
            const mh_pad_t *p = &g.pad[k];
            seen[p->mult]++;
            CHECK(p->x1 - p->x0 > 2 * MH_FOOT_X, "seed %u pad %dx narrower than feet", seed, p->mult);
            CHECK(p->y > 20.0f && p->y < MH_H, "seed %u pad y %f", seed, (double)p->y);
            // flat: surface equals pad height across it
            for (float x = p->x0 + 0.5f; x < p->x1; x += 3.0f)
                CHECK(fabsf(mh_terrain_y(&g, x) - p->y) < 0.01f, "seed %u pad not flat", seed);
            if (k) CHECK(p->x0 > g.pad[k - 1].x1, "seed %u pads overlap", seed);
        }
        CHECK(seen[1] == 1 && seen[2] == 1 && seen[4] == 1 && seen[5] == 1, "seed %u multipliers", seed);
        CHECK(pad_with(&g, 5)->x1 - pad_with(&g, 5)->x0 < pad_with(&g, 1)->x1 - pad_with(&g, 1)->x0,
              "5X not narrower than 1x");
    }
}

// Put the lander just above the centre of a pad with the given state.
static void hover_over(mh_game_t *g, const mh_pad_t *p, float vx, float vy, float angle)
{
    g->x = (p->x0 + p->x1) / 2.0f;
    g->y = p->y - MH_FOOT_Y - 0.5f;
    g->vx = vx; g->vy = vy; g->angle = angle;
}

static void run_until_contact(mh_game_t *g, mh_input_t in)
{
    for (int i = 0; i < 400 && g->phase == MH_FLYING; i++) mh_step(g, 0.01f, &in);
}

static void test_landings(void)
{
    mh_game_t g; mh_new_game(&g, 42);
    const mh_pad_t *p2 = pad_with(&g, 2);
    hover_over(&g, p2, 2.0f, 8.0f, 0.0f);
    float fuel0 = g.fuel;
    run_until_contact(&g, k_idle);
    CHECK(g.phase == MH_LANDED, "soft landing on 2X should land (phase %d)", g.phase);
    CHECK(g.score == 200, "2X landing scores 200, got %u", g.score);
    CHECK(fabsf(g.fuel - (fuel0 + MH_LAND_BONUS)) < 0.5f, "landing fuel bonus");
    CHECK(g.last_mult == 2, "last_mult");

    // After the pause: new stage, new terrain, flying again.
    uint32_t seed0 = g.seed;
    for (int i = 0; i < 250; i++) mh_step(&g, 0.01f, &k_idle);
    CHECK(g.phase == MH_FLYING && g.stage == 1 && g.seed != seed0, "new stage after LANDED");

    // Too fast.
    mh_new_game(&g, 42);
    hover_over(&g, pad_with(&g, 1), 0.0f, MH_SAFE_VY + 6.0f, 0.0f);
    run_until_contact(&g, k_idle);
    CHECK(g.phase == MH_CRASHED, "fast landing must crash");
    CHECK(fabsf(g.fuel - (MH_FUEL_START - MH_CRASH_FUEL)) < 0.5f, "crash costs fuel");

    // Too tilted (15 degrees).
    mh_new_game(&g, 42);
    hover_over(&g, pad_with(&g, 1), 0.0f, 5.0f, 0.26f);
    mh_input_t keep = { 0.26f, false, false };
    run_until_contact(&g, keep);
    CHECK(g.phase == MH_CRASHED, "tilted landing must crash");

    // Sideways too fast.
    mh_new_game(&g, 42);
    hover_over(&g, pad_with(&g, 1), MH_SAFE_VX + 8.0f, 5.0f, 0.0f);
    run_until_contact(&g, k_idle);
    CHECK(g.phase == MH_CRASHED, "sliding landing must crash");

    // Jagged terrain: drop onto the highest non-pad vertex.
    mh_new_game(&g, 42);
    int best = -1;
    for (int i = 1; i < g.nverts - 1; i++) {
        float x = g.vx_[i];
        if (mh_pad_at(&g, x - 20, x + 20)) continue;
        bool near_pad = false;
        for (int k = 0; k < MH_PADS; k++)
            if (x > g.pad[k].x0 - 25 && x < g.pad[k].x1 + 25) near_pad = true;
        if (!near_pad && (best < 0 || g.vy_[i] < g.vy_[best])) best = i;
    }
    g.x = g.vx_[best]; g.y = g.vy_[best] - 30.0f; g.vx = 0; g.vy = 4.0f; g.angle = 0;
    run_until_contact(&g, k_idle);
    CHECK(g.phase == MH_CRASHED, "landing on rocks must crash");

    // Crash: debris moves, respawn on the same terrain after the pause.
    float d0 = g.debris[0].x;
    seed0 = g.seed;
    for (int i = 0; i < 250; i++) mh_step(&g, 0.01f, &k_idle);
    CHECK(g.debris[0].x != d0 || g.phase == MH_FLYING, "debris animates");
    CHECK(g.phase == MH_FLYING && g.seed == seed0, "respawn on same terrain after crash");
}

static void test_fuel_and_emergency(void)
{
    mh_game_t g; mh_new_game(&g, 7);
    g.y = 30; g.vy = -50;   // keep well clear of the ground
    mh_input_t gray = { 0, true, false };
    for (int i = 0; i < 100; i++) mh_step(&g, 0.01f, &gray);
    CHECK(fabsf((MH_FUEL_START - g.fuel) - MH_BURN) < 0.2f, "normal burn %f/s",
          (double)(MH_FUEL_START - g.fuel));

    mh_new_game(&g, 7);
    g.y = 30; g.vy = -80;
    mh_input_t red = { 0, false, true };
    for (int i = 0; i < 100; i++) mh_step(&g, 0.01f, &red);
    CHECK(fabsf((MH_FUEL_START - g.fuel) - MH_BURN * 4.0f) < 0.5f, "emergency burns 4x");

    // Burst limit: holding red past 2 s stops the burn until released.
    for (int i = 0; i < 150; i++) mh_step(&g, 0.01f, &red);       // now 2.5 s held
    CHECK(!g.emerg_firing && !g.emerg_armed, "burst must cut out after 2 s");
    float f = g.fuel;
    for (int i = 0; i < 50; i++) mh_step(&g, 0.01f, &red);
    CHECK(g.fuel == f, "no fuel burned while held past the limit");
    mh_step(&g, 0.01f, &k_idle);                                   // release
    mh_step(&g, 0.01f, &red);
    CHECK(g.emerg_firing, "re-press fires again");

    // Emergency is 2.5x normal acceleration.
    mh_new_game(&g, 7); g.y = 100; g.vy = 0;
    mh_step(&g, 0.1f, &gray); float a1 = -(g.vy - MH_GRAVITY * 0.1f);
    mh_new_game(&g, 7); g.y = 100; g.vy = 0;
    mh_step(&g, 0.1f, &red);  float a2 = -(g.vy - MH_GRAVITY * 0.1f);
    CHECK(fabsf(a2 / a1 - 2.5f) < 0.01f, "emergency/normal = %f", (double)(a2 / a1));
}

static void test_wrap(void)
{
    mh_game_t g; mh_new_game(&g, 3);
    g.x = MH_W - 1.0f; g.y = 20; g.vx = 200.0f; g.vy = -30;
    mh_step(&g, 0.02f, &k_idle);
    CHECK(g.x >= 0.0f && g.x < 10.0f, "wrap right->left, x=%f", (double)g.x);
    g.x = 1.0f; g.vx = -200.0f;
    mh_step(&g, 0.02f, &k_idle);
    CHECK(g.x > MH_W - 10.0f && g.x < MH_W, "wrap left->right, x=%f", (double)g.x);
}

static void test_game_over(void)
{
    mh_game_t g; mh_new_game(&g, 9);
    g.fuel = 100.0f;
    hover_over(&g, pad_with(&g, 1), 0.0f, 40.0f, 0.0f);
    run_until_contact(&g, k_idle);
    CHECK(g.phase == MH_CRASHED && g.fuel == 0.0f, "crash drains to zero");
    for (int i = 0; i < 250; i++) mh_step(&g, 0.01f, &k_idle);
    CHECK(g.phase == MH_GAMEOVER, "fuel 0 after crash -> game over");
}

int main(void)
{
    test_terrain();
    test_landings();
    test_fuel_and_emergency();
    test_wrap();
    test_game_over();
    if (g_fail) { printf("%d failure(s)\n", g_fail); return 1; }
    printf("mh_game: all passed\n");
    return 0;
}
