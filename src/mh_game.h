// mh_game — moonhop game rules: terrain, physics, landing, scoring.
//
// Pure C (no Pico SDK, no hardware). Built into the app and into tests/.
//
// Coordinates are screen pixels in the play field: x 0..MH_W (wraps),
// y grows DOWN, 0 at the top of the play field. Velocities are px/s.
// Lander angle: radians, 0 = upright, positive = tipped clockwise (nose to
// the right), so thrust then pushes right.
#ifndef MH_GAME_H
#define MH_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define MH_W            480
#define MH_H            260     // play field height (HUD above, buttons below)
#define MH_GRAVITY      2.4f    // px/s^2
#define MH_THRUST       5.5f    // px/s^2, normal engine
#define MH_EMERG_MULT   2.5f    // emergency thrust = 2.5x normal
#define MH_FUEL_START   1000.0f
#define MH_BURN         12.0f   // fuel/s, normal thrust
#define MH_EMERG_BURN_MULT 4.0f // emergency burns 4x faster
#define MH_EMERG_MAX_S  2.0f    // emergency burst cuts out after this, rearm on release
#define MH_CRASH_FUEL   300.0f  // fuel lost per crash
#define MH_LAND_BONUS   150.0f  // fuel gained per safe landing
#define MH_ROT_RATE     3.0f    // rad/s, max rotation speed
#define MH_ROT_MAX      1.57f   // +/-90 degrees
#define MH_SAFE_ANGLE   0.1745f // +/-10 degrees
#define MH_SAFE_VY      14.0f   // px/s downward
#define MH_SAFE_VX      9.0f    // px/s sideways
#define MH_STAGE_PAUSE  2.0f    // s after LANDED!/crash before the next attempt

// Lander footprint (px from its centre; y down).
#define MH_FOOT_X       11.0f
#define MH_FOOT_Y       10.0f

#define MH_MAX_VERTS    80
#define MH_PADS         4
#define MH_DEBRIS       18

typedef struct { float x0, x1, y; int mult; } mh_pad_t;
typedef struct { float x, y, vx, vy, a, va, len; } mh_debris_t;

typedef enum { MH_FLYING = 0, MH_LANDED, MH_CRASHED, MH_GAMEOVER } mh_phase_t;

typedef struct {
    // terrain
    int nverts;
    float vx_[MH_MAX_VERTS], vy_[MH_MAX_VERTS];   // vertices, x ascending 0..MH_W
    mh_pad_t pad[MH_PADS];
    uint32_t seed;
    // lander
    float x, y, vx, vy, angle;
    bool thrusting, emerg_firing;
    float emerg_t;          // seconds the current emergency press has burned
    bool emerg_armed;       // false until red is released after a burst
    // game
    float fuel, time;
    uint32_t score;
    int stage;
    mh_phase_t phase;
    float phase_t;          // seconds in LANDED/CRASHED
    int last_mult;          // pad multiplier of the last landing
    bool crash_event;       // one-shot: set on the step a crash happens
    bool land_event;        // one-shot: set on the step a landing happens
    mh_debris_t debris[MH_DEBRIS];
} mh_game_t;

typedef struct {
    float rot_target;   // desired angle, rad
    bool  thrust;       // GRAY held
    bool  emergency;    // RED held
} mh_input_t;

void  mh_new_game(mh_game_t *g, uint32_t seed);
void  mh_gen_terrain(mh_game_t *g, uint32_t seed);   // fills vertices + pads
void  mh_spawn(mh_game_t *g);                         // lander back to the top
float mh_terrain_y(const mh_game_t *g, float x);      // surface height at x (wraps)
float mh_altitude(const mh_game_t *g);                // px from feet to surface below
const mh_pad_t *mh_pad_at(const mh_game_t *g, float x0, float x1);  // pad under both feet
void  mh_step(mh_game_t *g, float dt, const mh_input_t *in);

#endif
