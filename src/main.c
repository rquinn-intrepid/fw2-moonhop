// fw2-moonhop — vector-style moon landing game for the FreeWili 2.
//
//   Tilt left/right ....... rotate the lander (BMI323 accelerometer)
//   Hold GRAY ............. thrust
//   Press RED ............. emergency thrust: 2.5x force, 4x fuel, 2 s burst
//                           (red held 6 s powers the board off - tap it)
//   GREEN ................. start / confirm
//   NAV LEFT/RIGHT ........ rotate (fallback when no IMU); NAV CENTER zeroes
//   Hold PAGE 5 s ......... About;  hold HOME 5 s ... leave the app
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "fw2.h"
#include "platform/diag.h"
#include "platform/psram.h"
#include "mh_game.h"

#define BE(c) ((uint16_t)((((c) >> 8) & 0xFF) | (((c) & 0xFF) << 8)))
#define RAD2DEG 57.29578f

#define KEY_FRESH_MS 1200     // > the keyboard's ~500 ms held-button report cadence
#define FB_W   480
#define FB_H   296            // HUD (32) + play field (MH_H 260) + gap
#define FIELD_Y 34            // play-field origin on screen

#define K_BLACK  BE(0x0000)
#define K_CYAN   BE(0x9FFF)   // bright line
#define K_GLOW   BE(0x0473)   // dim halo
#define K_WHITE  BE(0xFFFF)
#define K_FLAME  BE(0xFE60)
#define K_FLAME2 BE(0xFFF0)
#define K_RED    BE(0xF8A0)
#define K_PAD    BE(0xFFFF)
// Front-panel button colours, already in wire order (wilibsp/AGENTS.md).
#define BTN_GRAY_BG  0x9AD6
#define BTN_GREEN_BG 0x0012
#define BTN_RED_BG   0x0780

static uint16_t s_fb[FB_W * FB_H];

// ---------------------------------------------------------------- raster
static inline void px(int x, int y, uint16_t c)
{
    if ((unsigned)x < FB_W && (unsigned)y < FB_H) s_fb[y * FB_W + x] = c;
}

static void line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int guard = 0; guard < 2000; guard++) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// "Glowing" vector line: a dim halo one pixel around, then the bright core.
static void vline(float x0, float y0, float x1, float y1, uint16_t c)
{
    int a = (int)lroundf(x0), b = (int)lroundf(y0), d = (int)lroundf(x1), e = (int)lroundf(y1);
    line(a + 1, b, d + 1, e, K_GLOW);
    line(a - 1, b, d - 1, e, K_GLOW);
    line(a, b + 1, d, e + 1, K_GLOW);
    line(a, b - 1, d, e - 1, K_GLOW);
    line(a, b, d, e, c);
}

// ---------------------------------------------------------------- stroke font
// Each glyph: segments "x0y0x1y1" on a 4x6 grid (y down).
static const char *glyph(char ch)
{
    switch (ch) {
    case '0': return "0040404646060600" "4006";
    case '1': return "202611201636";
    case '2': return "00404043430303060646";
    case '3': return "0040404606461343";
    case '4': return "000303434046";
    case '5': return "40000003034343464606";
    case '6': return "40000006064646434303";
    case '7': return "00404016";
    case '8': return "00404046460606000343";
    case '9': return "03430300004040464606";
    case 'A': return "062020461434";
    case 'B': return "060000303041413203323243434536363606";
    case 'C': return "400000060646";
    case 'D': return "060000303042424444363606";
    case 'E': return "4000000606460333";
    case 'F': return "400000060333";
    case 'G': return "40000006064646434323";
    case 'H': return "000640460343";
    case 'I': return "004020260646";
    case 'K': return "000603400346";
    case 'L': return "00060646";
    case 'M': return "0600002323404046";
    case 'N': return "060000464640";
    case 'O': return "0040404646060600";
    case 'P': return "0600004040434303";
    case 'R': return "06000040404343032346";
    case 'S': return "40000003034343464606";
    case 'T': return "00402026";
    case 'U': return "000606464640";
    case 'V': return "00262640";
    case 'W': return "0006062323464640";
    case 'X': return "00464006";
    case 'Y': return "002323402326";
    case 'Z': return "004040060646";
    case '!': return "20242526";
    case '-': return "1333";
    case '+': return "13332224";
    case ':': return "21222425";
    case '.': return "2526";
    case '/': return "4006";
    default:  return "";
    }
}

static float text_width(const char *s, float u) { return (float)strlen(s) * 6.0f * u - 2.0f * u; }

static void vtext(float x, float y, float u, uint16_t c, const char *s)
{
    for (; *s; s++, x += 6.0f * u) {
        const char *g = glyph(*s);
        for (; g[0] && g[1] && g[2] && g[3]; g += 4)
            vline(x + (g[0] - '0') * u, y + (g[1] - '0') * u,
                  x + (g[2] - '0') * u, y + (g[3] - '0') * u, c);
    }
}

static void vtext_c(float cx, float y, float u, uint16_t c, const char *s)
{
    vtext(cx - text_width(s, u) / 2.0f, y, u, c, s);
}

// ---------------------------------------------------------------- lander art
typedef struct { float x, y, c, s; } xf_t;   // place + rotation
static void lseg(const xf_t *t, float ax, float ay, float bx, float by, uint16_t col)
{
    vline(t->x + ax * t->c - ay * t->s, t->y + ax * t->s + ay * t->c,
          t->x + bx * t->c - by * t->s, t->y + bx * t->s + by * t->c, col);
}

static void draw_lander_at(float x, float y, float ang, int flame, uint32_t frame)
{
    xf_t t = { x, y + FIELD_Y, cosf(ang), sinf(ang) };
    // ascent stage (faceted cabin)
    lseg(&t, -6, -4, -6, -9, K_WHITE); lseg(&t, -6, -9, -3, -13, K_WHITE);
    lseg(&t, -3, -13, 3, -13, K_WHITE); lseg(&t, 3, -13, 6, -9, K_WHITE);
    lseg(&t, 6, -9, 6, -4, K_WHITE);
    lseg(&t, -2, -10, 2, -10, K_CYAN);                  // window
    // descent stage
    lseg(&t, -8, -4, 8, -4, K_WHITE); lseg(&t, 8, -4, 8, 3, K_WHITE);
    lseg(&t, 8, 3, -8, 3, K_WHITE);   lseg(&t, -8, 3, -8, -4, K_WHITE);
    // legs and feet
    lseg(&t, -7, 1, -MH_FOOT_X, MH_FOOT_Y, K_CYAN);
    lseg(&t, 7, 1, MH_FOOT_X, MH_FOOT_Y, K_CYAN);
    lseg(&t, -4, 3, -MH_FOOT_X + 2, MH_FOOT_Y - 3, K_CYAN);
    lseg(&t, 4, 3, MH_FOOT_X - 2, MH_FOOT_Y - 3, K_CYAN);
    lseg(&t, -MH_FOOT_X - 3, MH_FOOT_Y, -MH_FOOT_X + 3, MH_FOOT_Y, K_WHITE);
    lseg(&t, MH_FOOT_X - 3, MH_FOOT_Y, MH_FOOT_X + 3, MH_FOOT_Y, K_WHITE);
    // engine bell
    lseg(&t, -2, 3, -3, 6, K_WHITE); lseg(&t, 2, 3, 3, 6, K_WHITE); lseg(&t, -3, 6, 3, 6, K_WHITE);
    // flame: 1 normal, 2 emergency
    if (flame) {
        float len = (flame == 2 ? 26.0f : 12.0f) + (float)(frame % 3) * (flame == 2 ? 4.0f : 2.0f);
        float w = flame == 2 ? 5.0f : 3.0f;
        uint16_t col = flame == 2 ? K_FLAME2 : K_FLAME;
        lseg(&t, -w, 7, 0, 7 + len, col);
        lseg(&t, w, 7, 0, 7 + len, col);
        if (flame == 2) lseg(&t, 0, 7, 0, 7 + len * 0.7f, K_FLAME);
    }
}

static void draw_lander(const mh_game_t *g, int flame, uint32_t frame)
{
    draw_lander_at(g->x, g->y, g->angle, flame, frame);
    if (g->x < 20.0f) draw_lander_at(g->x + MH_W, g->y, g->angle, flame, frame);
    if (g->x > MH_W - 20.0f) draw_lander_at(g->x - MH_W, g->y, g->angle, flame, frame);
}

// ---------------------------------------------------------------- scene
static void draw_terrain(const mh_game_t *g)
{
    for (int i = 1; i < g->nverts; i++)
        vline(g->vx_[i - 1], g->vy_[i - 1] + FIELD_Y, g->vx_[i], g->vy_[i] + FIELD_Y, K_CYAN);
    for (int k = 0; k < MH_PADS; k++) {
        const mh_pad_t *p = &g->pad[k];
        vline(p->x0, p->y + FIELD_Y, p->x1, p->y + FIELD_Y, K_PAD);
        vline(p->x0, p->y + FIELD_Y + 1, p->x1, p->y + FIELD_Y + 1, K_PAD);
        if (p->mult > 1) {
            char m[4];
            snprintf(m, sizeof m, "%dX", p->mult);
            vtext_c((p->x0 + p->x1) / 2.0f, p->y + FIELD_Y + 6, 1.5f, K_WHITE, m);
        }
    }
}

static void draw_hud(const mh_game_t *g)
{
    char b[64];
    int t = (int)g->time;
    snprintf(b, sizeof b, "SCORE %04u", (unsigned)g->score);           vtext(6, 4, 1.5f, K_CYAN, b);
    snprintf(b, sizeof b, "TIME %d:%02d", t / 60, t % 60);             vtext(186, 4, 1.5f, K_CYAN, b);
    snprintf(b, sizeof b, "FUEL %04d", (int)g->fuel);
    vtext(354, 4, 1.5f, g->fuel < 200.0f ? K_RED : K_CYAN, b);
    int alt = (int)mh_altitude(g); if (alt < 0) alt = 0;
    snprintf(b, sizeof b, "ALTITUDE %03d", alt);                        vtext(6, 18, 1.5f, K_CYAN, b);
    snprintf(b, sizeof b, "HORIZ SPEED %+03d", (int)lroundf(g->vx));    vtext(156, 18, 1.5f, K_CYAN, b);
    snprintf(b, sizeof b, "VERT SPEED %+03d", (int)lroundf(-g->vy));    vtext(330, 18, 1.5f, K_CYAN, b);
}

static void draw_debris(const mh_game_t *g)
{
    for (int i = 0; i < MH_DEBRIS; i++) {
        const mh_debris_t *d = &g->debris[i];
        float c = cosf(d->a) * d->len / 2, s = sinf(d->a) * d->len / 2;
        vline(d->x - c, d->y - s + FIELD_Y, d->x + c, d->y + s + FIELD_Y, i % 3 ? K_WHITE : K_FLAME);
    }
}

static void blit(void)
{
    st7796_blit_rect(0, 0, FB_W - 1, FB_H - 1, s_fb);
}

// ---------------------------------------------------------------- button bar
static void btn(int slot, uint16_t bg, uint16_t fg, const char *l1, const char *l2)
{
    const int w = (ST7796_W - 12) / 5, x = slot * 96;
    st7796_fill_rect(x, 296, w, 24, bg);
    if (l2) {
        st7796_draw_text(x + (w - (int)strlen(l1) * 6) / 2, 299, 1, fg, bg, l1);
        st7796_draw_text(x + (w - (int)strlen(l2) * 6) / 2, 310, 1, fg, bg, l2);
    } else {
        st7796_draw_text(x + (w - (int)strlen(l1) * 12) / 2, 300, 2, fg, bg, l1);
    }
}

static int s_bar = -1;
static void button_bar(int mode)   // 0 = calibrate/start, 1 = flying
{
    if (s_bar == mode) return;
    s_bar = mode;
    st7796_fill_rect(0, 296, ST7796_W, 24, K_BLACK);
    if (mode == 0) {
        btn(2, BTN_GREEN_BG, K_WHITE, "START", NULL);
    } else {
        btn(0, BTN_GRAY_BG, K_BLACK, "THRUST", NULL);
        btn(4, BTN_RED_BG, K_WHITE, "EMERGENCY", "TAP");
    }
}

// ---------------------------------------------------------------- IMU steering
// Two ways to "turn the board right", learned from the player's own gesture
// on the second calibration screen:
//   TILT  - the gesture changed the direction of gravity (a tilt/roll, or a
//           steering-wheel turn with the board upright): accelerometer angle,
//           absolute, no drift.
//   TURN  - the gesture was a rotation about the gravity axis (e.g. turning
//           the board while it lies flat), which the accelerometer cannot see:
//           integrate the gyro about the learned axis. Relative; re-centred on
//           every spawn and by NAV CENTER.
static bool  s_have_imu;
static float s_g[3], s_n[3] = { 0, 0, 1 }, s_u[3] = { 1, 0, 0 };
static float s_w[3], s_bias[3];            // gyro dps, and its at-rest bias
static float s_rot[3];                     // gyro integral during the turn-right step, deg
static float s_k[3] = { 0, 0, 1 };         // learned turn axis (TURN mode)
static float s_turn;                       // integrated turn angle, rad (TURN mode)
static bool  s_turn_mode;
static const char *s_mode_name = "TILT";

static float dot3(const float *a, const float *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static float len3(const float *a) { return sqrtf(dot3(a, a)); }
static void norm3(float *a)
{
    float m = len3(a);
    if (m > 1e-6f) { a[0] /= m; a[1] /= m; a[2] /= m; }
}
static void imu_poll(float dt, bool learn_bias, bool learn_rot)
{
    bmi323_reading_t r;
    if (!s_have_imu || !bmi323_read(&r) || !r.valid) return;
    float a[3] = { r.ax, r.ay, r.az }, w[3] = { r.gx, r.gy, r.gz };
    for (int i = 0; i < 3; i++) {
        s_g[i] += 0.3f * (a[i] - s_g[i]);
        s_w[i] = w[i];
        if (learn_bias) s_bias[i] += 0.02f * (w[i] - s_bias[i]);   // ~1 s average at rest
        if (learn_rot)  s_rot[i] += (w[i] - s_bias[i]) * dt;
    }
    if (s_turn_mode) {
        float rate = 0.0f, mag = 0.0f;
        for (int i = 0; i < 3; i++) {
            rate += (w[i] - s_bias[i]) * s_k[i];
            mag += (w[i] - s_bias[i]) * (w[i] - s_bias[i]);
        }
        if (fabsf(rate) > 0.8f) s_turn += rate * dt / RAD2DEG;     // ignore sub-deadband jitter
        // Keep learning the gyro's resting offset whenever the board is still,
        // so a small offset cannot integrate into a phantom turn.
        if (mag < 1.5f * 1.5f)
            for (int i = 0; i < 3; i++) s_bias[i] += 0.01f * (w[i] - s_bias[i]);
    }
}
static void cal_level(void)
{
    memcpy(s_n, s_g, sizeof s_n); norm3(s_n);
    memset(s_rot, 0, sizeof s_rot);
}
static void cal_right(void)
{
    // How much did gravity move (tilt), and how much did the board rotate?
    float gn[3]; memcpy(gn, s_g, sizeof gn); norm3(gn);
    float tilt_deg = acosf(fmaxf(-1.0f, fminf(1.0f, dot3(gn, s_n)))) * RAD2DEG;
    float rot_deg = len3(s_rot);
    float k[3]; memcpy(k, s_rot, sizeof k); norm3(k);
    bool about_gravity = fabsf(dot3(k, s_n)) > 0.7f;
    DIAG("moonhop: cal tilt=%d deg rot=%d deg axis.n=%d/1000\n",
         (int)tilt_deg, (int)rot_deg, (int)(dot3(k, s_n) * 1000));

    if (rot_deg > 10.0f && about_gravity && tilt_deg < 0.5f * rot_deg) {
        s_turn_mode = true;  s_mode_name = "TURN";
        memcpy(s_k, k, sizeof s_k);          // sign included: this way = right
        s_turn = 0.0f;
        return;
    }
    s_turn_mode = false; s_mode_name = "TILT";
    float u[3]; memcpy(u, s_g, sizeof u);
    float d = dot3(u, s_n);
    for (int i = 0; i < 3; i++) u[i] -= d * s_n[i];
    if (len3(u) < 0.15f) {
        // No clear gesture: steer by the board's left-right axis (accelerometer
        // X runs across the screen; Y runs top to bottom, so tipping the board
        // toward or away from you must not steer). Y only if X is near gravity.
        float e[3] = { 1, 0, 0 };
        if (fabsf(s_n[0]) > 0.8f) { e[0] = 0; e[1] = 1; }
        d = dot3(e, s_n);
        for (int i = 0; i < 3; i++) u[i] = e[i] - d * s_n[i];
        s_mode_name = "TILT?";
        DIAG("moonhop: no clear gesture, steering by board X axis\n");
    }
    norm3(u);
    memcpy(s_u, u, sizeof s_u);
}
static float steer_rad(void)
{
    if (!s_have_imu) return 0.0f;
    if (s_turn_mode) return s_turn;          // 1:1 with the board
    float t = atan2f(dot3(s_g, s_u), dot3(s_g, s_n));
    const float dead = 2.0f / RAD2DEG;
    if (fabsf(t) < dead) return 0.0f;
    return (t > 0 ? t - dead : t + dead) * 1.5f;
}

// ---------------------------------------------------------------- audio
// Looping white-noise rumble; the gain is applied by rewriting the ring.
#define SND_FRAMES 4096                       // 16 KB, power of two, ring-aligned
static uint32_t s_snd[SND_FRAMES] __attribute__((aligned(SND_FRAMES * 4)));
static int16_t  s_noise[SND_FRAMES];
static bool  s_audio_ok, s_snd_on;
static float s_gain_now;
static float s_boom;                          // explosion envelope 0..1
static uint32_t s_quiet_ms;
#define SND_MAX_GAIN 0.22f                    // keep well inside the 0.5 W speaker

static void audio_setup(void)
{
    picpwr_keep_awake(picpwr_zone_bit(PICPWR_ZONE_AUDIO));
    absolute_time_t give_up = make_timeout_time_ms(6000), no_frames = make_timeout_time_ms(3000);
    uint32_t rails;
    while (!time_reached(give_up)) {
        fw2_app_recovery_task();
        picpwr_task();
        if (picpwr_rails(&rails)) { if (rails & picpwr_zone_bit(PICPWR_ZONE_AUDIO)) break; }
        else if (time_reached(no_frames)) break;
        fw2_app_recovery_sleep_ms(25);
    }
    codec_nau88c10_init();
    codec_nau88c10_dac_mute(false);
    audio_i2s_duplex_init(16000);             // playback only: no capture, RX autopush stays off
    codec_nau88c10_speaker_low_power();       // silent until something plays

    // Brown-ish noise: white noise through a one-pole low-pass, normalised.
    // Pass 0 settles the filter, pass 1 finds the peak, pass 2 replays the
    // same sequence (same RNG state) and stores it normalised.
    float peak = 1e-3f;
    for (int pass = 0; pass < 3; pass++) {
        uint32_t r = 0x1202u;
        float lp = 0.0f;
        for (int rep = 0; rep < 2; rep++)
            for (int i = 0; i < SND_FRAMES; i++) {
                r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                float w = (float)(int32_t)r / 2147483648.0f;
                lp += 0.18f * (w - lp);
                if (rep == 0) continue;            // first lap only settles the filter
                if (pass == 1 && fabsf(lp) > peak) peak = fabsf(lp);
                if (pass == 2) s_noise[i] = (int16_t)(lp / peak * 32000.0f);
            }
    }
    s_audio_ok = true;
    DIAG("moonhop: audio up\n");
}

static void audio_update(float dt, int flame, bool crash_event)
{
    if (!s_audio_ok) return;
    if (crash_event) s_boom = 1.0f;
    s_boom -= dt / 1.4f;
    if (s_boom < 0.0f) s_boom = 0.0f;
    float g = flame == 2 ? 0.16f : flame == 1 ? 0.07f : 0.0f;
    g += s_boom * s_boom * SND_MAX_GAIN;
    if (g > SND_MAX_GAIN) g = SND_MAX_GAIN;

    if (g <= 0.001f) {
        s_quiet_ms += (uint32_t)(dt * 1000.0f);
        if (s_snd_on && s_quiet_ms > 400) {    // shut the path down when silent
            audio_i2s_duplex_play_stop();
            codec_nau88c10_speaker_low_power();
            s_snd_on = false;
            s_gain_now = 0.0f;
        }
        return;
    }
    s_quiet_ms = 0;
    if (fabsf(g - s_gain_now) > 0.004f || !s_snd_on) {
        s_gain_now = g;
        for (int i = 0; i < SND_FRAMES; i++) {
            uint16_t v = (uint16_t)(int16_t)((float)s_noise[i] * g);
            s_snd[i] = ((uint32_t)v << 16) | v;
        }
    }
    if (!s_snd_on) {
        codec_nau88c10_dac_mute(false);       // low_power soft-muted the DAC; undo it first
        codec_nau88c10_set_output(CODEC_OUT_SPEAKER);
        audio_i2s_duplex_play_loop(s_snd, SND_FRAMES);
        s_snd_on = true;
    }
}

// ---------------------------------------------------------------- LEDs
static void leds(rgb_t c)
{
    ws2812_fill(c);
    ws2812_show();
}

// ---------------------------------------------------------------- main
typedef enum { ST_CAL_LEVEL, ST_CAL_RIGHT, ST_PLAY } state_t;

int main(void)
{
    board_init();
    fw2_app_recovery_init();
    size_t psram_bytes = psram_init();
    st7796_init();
    fw2_app_about_use_lcd();
    fw2kb_t kb;
    fw2kb_init(&kb);
    bool agentio = psram_bytes >= (size_t)ST7796_W * ST7796_H * 2;
    if (agentio) { agentio_init(); agentio_bind_keyboard(&kb); }
    st7796_fill_screen(K_BLACK);              // whole surface before the backlight
    board_backlight_set(1);
    picpwr_release_unused();

    ws2812_init(pio1, 0, PIN_LED_DATA);
    ws2812_set_brightness(40);
    ws2812_clear(); ws2812_show();

    memset(s_fb, 0, sizeof s_fb);
    vtext_c(240, 120, 4.0f, K_CYAN, "MOONHOP");
    blit();

    uint64_t t0 = time_us_64();
    while (!(s_have_imu = bmi323_init()) && time_us_64() - t0 < 2000000) {
        fw2_app_recovery_task();
        picpwr_task();
        if (agentio) agentio_task();
        fw2_app_recovery_sleep_ms(50);
    }
    audio_setup();
    DIAG("moonhop: v005 up, imu=%d agentio=%d\n", s_have_imu, agentio);

    static mh_game_t g;
    mh_new_game(&g, 0x5EED1u);
    state_t st = s_have_imu ? ST_CAL_LEVEL : ST_PLAY;
    bool waiting_start = true;               // title / game-over screen until GREEN
    float nav_trim = 0.0f, acc = 0.0f;
    uint32_t frame = 0, seed = 0x5EED1u;
    uint64_t last = time_us_64(), t_draw = 0, t_led = 0, t_diag = 0;
    int last_flame = 0;
    bool crash_seen = false;

    for (;;) {
        fw2_app_recovery_task();
        picpwr_task();

        uint64_t now = time_us_64();
        float dt = (float)(now - last) * 1e-6f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;            // e.g. after a screenshot stalled us
        imu_poll(dt, st == ST_CAL_LEVEL, st == ST_CAL_RIGHT);

        bool green = false;
        uartkbd_event_t ev;
        while (uartkbd_next_event(&ev)) {
            if (!ev.pressed) continue;
            if (ev.btn == UARTKBD_BTN_GREEN) green = true;
            if (ev.btn == UARTKBD_BTN_NAV_CENTER) { nav_trim = 0.0f; s_turn = 0.0f; }
        }
        // The keyboard controller re-reports a held button about every 500 ms,
        // so "fresh" must allow longer than that or a hold reads as pulses.
        bool gray = uartkbd_button_down_fresh(UARTKBD_BTN_GREY, KEY_FRESH_MS);
        bool red  = uartkbd_button_down_fresh(UARTKBD_BTN_RED, KEY_FRESH_MS);
        if (uartkbd_button_down_fresh(UARTKBD_BTN_NAV_LEFT, KEY_FRESH_MS))  nav_trim -= 2.0f * dt;
        if (uartkbd_button_down_fresh(UARTKBD_BTN_NAV_RIGHT, KEY_FRESH_MS)) nav_trim += 2.0f * dt;
        if (nav_trim > MH_ROT_MAX) nav_trim = MH_ROT_MAX;
        if (nav_trim < -MH_ROT_MAX) nav_trim = -MH_ROT_MAX;

        if (st != ST_PLAY) {
            if (green) {
                if (st == ST_CAL_LEVEL) { cal_level(); st = ST_CAL_RIGHT; }
                else { cal_right(); st = ST_PLAY; waiting_start = false; mh_new_game(&g, seed); }
                green = false;
            }
        } else if (waiting_start || g.phase == MH_GAMEOVER) {
            if (green) {
                seed = seed * 1664525u + 1013904223u;
                mh_new_game(&g, seed);
                waiting_start = false;
                DIAG("moonhop: new game seed=%x\n", (unsigned)seed);
            }
        } else if (uartkbd_button_down_fresh(UARTKBD_BTN_PAGE, KEY_FRESH_MS)) {
            acc = 0.0f;                      // paused while PAGE is held (About screen)
        } else {
            if (g.phase != MH_FLYING) s_turn = 0.0f;   // re-centre TURN mode each attempt
            mh_input_t in = { steer_rad() + nav_trim, gray, red };
            acc += dt;
            while (acc >= 0.01f) {
                acc -= 0.01f;
                mh_step(&g, 0.01f, &in);
                if (g.crash_event) { crash_seen = true; DIAG("moonhop: CRASH fuel=%d\n", (int)g.fuel); }
                if (g.land_event) DIAG("moonhop: LANDED %dX score=%u\n", g.last_mult, (unsigned)g.score);
            }
        }

        int flame = (st == ST_PLAY && g.phase == MH_FLYING)
                        ? (g.emerg_firing ? 2 : g.thrusting ? 1 : 0) : 0;
        audio_update(dt, flame, crash_seen);
        crash_seen = false;

        if (now - t_led > 50000 || flame != last_flame) {
            t_led = now; last_flame = flame;
            if (g.phase == MH_CRASHED && st == ST_PLAY)
                leds(((now / 150000) & 1) ? (rgb_t){ .r = 255 } : (rgb_t){0});
            else if (g.phase == MH_LANDED) leds((rgb_t){ .g = 200 });
            else if (flame == 2) leds((rgb_t){ .r = 255, .g = 200, .b = 120 });
            else if (flame == 1) leds((rgb_t){ .r = 120, .g = 50 });
            else leds((rgb_t){0});
        }

        if (now - t_draw > 33000) {          // ~30 fps
            t_draw = now; frame++;
            memset(s_fb, 0, sizeof s_fb);
            if (st == ST_CAL_LEVEL || st == ST_CAL_RIGHT) {
                button_bar(0);
                vtext_c(240, 60, 4.0f, K_CYAN, "MOONHOP");
                char b[40];
                if (st == ST_CAL_LEVEL) {
                    vtext_c(240, 125, 2.0f, K_WHITE, "HOLD IT HOW YOU WILL PLAY");
                    vtext_c(240, 155, 2.0f, K_WHITE, "KEEP STILL - PRESS GREEN");
                    snprintf(b, sizeof b, "X %+05d Y %+05d Z %+05d",
                             (int)(s_g[0] * 1000), (int)(s_g[1] * 1000), (int)(s_g[2] * 1000));
                } else {
                    vtext_c(240, 125, 2.0f, K_WHITE, "NOW TURN OR TILT IT RIGHT");
                    vtext_c(240, 155, 2.0f, K_WHITE, "HOLD THERE - PRESS GREEN");
                    snprintf(b, sizeof b, "TURNED %d DEG", (int)len3(s_rot));
                }
                vtext_c(240, 210, 1.5f, K_CYAN, b);
            } else {
                draw_hud(&g);
                draw_terrain(&g);
                if (waiting_start) {
                    button_bar(0);
                    vtext_c(240, 90, 3.0f, K_WHITE, "PRESS GREEN");
                } else if (g.phase == MH_GAMEOVER) {
                    button_bar(0);
                    vtext_c(240, 80, 4.0f, K_RED, "GAME OVER");
                    vtext_c(240, 130, 2.0f, K_WHITE, "PRESS GREEN");
                } else {
                    button_bar(1);
                    if (g.phase == MH_CRASHED) {
                        draw_debris(&g);
                        vtext_c(240, 90, 3.0f, K_RED, g.fuel > 0 ? "CRASHED" : "OUT OF FUEL");
                    } else {
                        draw_lander(&g, flame, frame);
                        if (g.phase == MH_LANDED) {
                            char b[32];
                            vtext_c(240, 80, 4.0f, K_WHITE, "LANDED!");
                            snprintf(b, sizeof b, "%d POINTS", 100 * g.last_mult);
                            vtext_c(240, 125, 2.0f, K_CYAN, b);
                        } else if (g.fuel <= 0.0f) {
                            vtext_c(240, 90, 2.0f, K_RED, "OUT OF FUEL");
                        }
                    }
                }
            }
            blit();
        }

        if (now - t_diag > 1000000) {
            t_diag = now;
            DIAG("st=%d ph=%d x=%d y=%d vx=%d vy=%d ang=%d steer=%d(%s) gray=%d red=%d fuel=%d score=%u flame=%d\n",
                 st, g.phase, (int)g.x, (int)g.y, (int)g.vx, (int)g.vy,
                 (int)(g.angle * RAD2DEG), (int)(steer_rad() * RAD2DEG), s_mode_name,
                 uartkbd_button_down_fresh(UARTKBD_BTN_GREY, KEY_FRESH_MS),
                 uartkbd_button_down_fresh(UARTKBD_BTN_RED, KEY_FRESH_MS),
                 (int)g.fuel, (unsigned)g.score, flame);
        }
        if (agentio) agentio_task();
    }
}
