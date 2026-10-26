/* ============================================================
 * Project : lge_kisk_neo_frt
 * File    : neo_effect.cpp
 *
 * Note:
 *  - NeoPixel effect logic only.
 *  - UART / DEVICE_ID query handling is done by upper layer.
 * ============================================================ */

#include "neo_effect.h"
#include "config.h"
#include "neo_effect_config.h"
#include "colors.h"

#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <stdint.h>
#include <Arduino.h>

/* ===================== Hardware Config ===================== */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Direction reversal (depends on physical wiring) */
static bool LED_REVERSE = LED_REVERSE_DEFAULT;

/* ===================== Type Declarations ===================== */

/* Main effect enum */
typedef enum front_effect_e {
  FRONT_EFFECT_NONE = 0,
  FRONT_EFFECT_BLINK02,
  FRONT_EFFECT_COLOR_BLINK,   /* color blink (triangle fade in/out) */
  FRONT_EFFECT_STATIC_FADE,
  FRONT_EFFECT_CROSS_PAIR,
  FRONT_EFFECT_MUSIC_12,
  FRONT_EFFECT_SEQ_13,
  FRONT_EFFECT_SEQ_14,
  FRONT_EFFECT_SEQ_15,
  FRONT_EFFECT_AURORA_07_0A,
  FRONT_EFFECT_FIRE,          /* FIRE dynamic env */
  FRONT_EFFECT_CUSTOM_FADE,   /* TINDLE / SIMPLE_FADE dynamic env */

  /* 0x28: remembered 0x0D effect (fade-in then flowing gradient) */
  FRONT_EFFECT_GRAD_FLOW_28,

  /* power-on: sky blue dimming until the host PC sends its first byte */
  FRONT_EFFECT_BOOT_WAIT
} front_effect_t;

/* Internal env engine mode */
typedef enum env_mode_e {
  ENV_MODE_SUN_BEAM = 0,      /* forest + sunbeam */
  ENV_MODE_FIRE_SWAY,         /* fire sway + noise */
  ENV_MODE_SIMPLE_FADE        /* global fade only */
} env_mode_t;

struct env_params_s;
typedef struct env_params_s env_params_t;

/* ===================== LED Mapping ===================== */

static inline uint16_t mapLED(uint16_t i)
{
  return LED_REVERSE ? (LED_COUNT - 1 - i) : i;
}

/* ===================== NeoPixel & Gamma ===================== */

static Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
static uint8_t gammaLUT[256];

static void initGammaLUT(void)
{
  const float gamma = 2.8f;
  for (int i = 0; i < 256; i++)
  {
    float x = (float)i / 255.0f;
    float y = powf(x, gamma);
    int v = (int)(y * 255.0f + 0.5f);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    gammaLUT[i] = (uint8_t)v;
  }
}

/* Gamma-corrected output */
static inline void setPixelGamma(uint16_t idx, uint8_t r, uint8_t g, uint8_t b)
{
  strip.setPixelColor(idx, strip.Color(gammaLUT[r], gammaLUT[g], gammaLUT[b]));
}

/* Raw output (no gamma) */
static inline void setPixelRaw(uint16_t idx, uint8_t r, uint8_t g, uint8_t b)
{
  strip.setPixelColor(idx, strip.Color(r, g, b));
}

/* ===================== Small Helpers ===================== */

static inline float clamp01(float x)
{
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

static inline uint8_t lerp_u8(uint8_t a, uint8_t b, float t)
{
  float v = (1.0f - t) * (float)a + t * (float)b;
  if (v < 0.0f) v = 0.0f;
  if (v > 255.0f) v = 255.0f;
  return (uint8_t)(v + 0.5f);
}

/* ===================== Effect State ===================== */

static front_effect_t g_effect        = FRONT_EFFECT_NONE;
static uint32_t       g_effectStartMs = 0;

/* ---------------- SEQ smoothing buffers ---------------- */

static uint8_t g_seqSmoothR[LED_COUNT];
static uint8_t g_seqSmoothG[LED_COUNT];
static uint8_t g_seqSmoothB[LED_COUNT];
static bool    g_seqSmoothInit = false;

static inline void seq_smooth_reset(void)
{
  g_seqSmoothInit = false;
}

static inline void seq_smooth_apply(uint16_t ledIndex,
                                    uint8_t targetR,
                                    uint8_t targetG,
                                    uint8_t targetB,
                                    float alpha)
{
  uint8_t prevR, prevG, prevB;

  if (!g_seqSmoothInit)
  {
    prevR = targetR;
    prevG = targetG;
    prevB = targetB;
  }
  else
  {
    prevR = g_seqSmoothR[ledIndex];
    prevG = g_seqSmoothG[ledIndex];
    prevB = g_seqSmoothB[ledIndex];
  }

  int16_t diffR = (int16_t)targetR - (int16_t)prevR;
  int16_t diffG = (int16_t)targetG - (int16_t)prevG;
  int16_t diffB = (int16_t)targetB - (int16_t)prevB;

  uint8_t outR = (uint8_t)(prevR + alpha * (float)diffR);
  uint8_t outG = (uint8_t)(prevG + alpha * (float)diffG);
  uint8_t outB = (uint8_t)(prevB + alpha * (float)diffB);

  g_seqSmoothR[ledIndex] = outR;
  g_seqSmoothG[ledIndex] = outG;
  g_seqSmoothB[ledIndex] = outB;
  g_seqSmoothInit        = true;

  setPixelGamma(mapLED(ledIndex), outR, outG, outB);
}

/* ============================================================
 *                    COMMON FADE PROFILE
 * ============================================================ */

static float fadeShort(float local)
{
  if (local < 0.10f)
    return local / 0.10f;               /* fade-in  0 -> 1 */
  else if (local < 0.18f)
    return 1.0f;                        /* hold */
  else if (local < 0.28f)
    return (0.28f - local) / 0.10f;     /* fade-out 1 -> 0 */
  else
    return 0.0f;                        /* off */
}

/* ============================================================
 *                    STATIC FADE
 * ============================================================ */

static const uint32_t kStaticFadeMs = NEO_CFG_STATIC_FADE_MS;

static uint8_t g_staticFromR, g_staticFromG, g_staticFromB;
static uint8_t g_staticToR,   g_staticToG,   g_staticToB;
static bool    g_hasLastStatic   = false;
static bool    g_staticUseGamma  = true;

static void start_static_fade_ex(uint8_t r, uint8_t g, uint8_t b, bool useGamma)
{
  if (!g_hasLastStatic)
  {
    g_staticFromR = 0;
    g_staticFromG = 0;
    g_staticFromB = 0;
  }
  else
  {
    g_staticFromR = g_staticToR;
    g_staticFromG = g_staticToG;
    g_staticFromB = g_staticToB;
  }

  g_staticToR      = r;
  g_staticToG      = g;
  g_staticToB      = b;
  g_staticUseGamma = useGamma;

  g_effect        = FRONT_EFFECT_STATIC_FADE;
  g_effectStartMs = millis();
}

static void start_static_fade(uint8_t r, uint8_t g, uint8_t b)
{
  start_static_fade_ex(r, g, b, true);
}

static void start_static_fade_raw(uint8_t r, uint8_t g, uint8_t b)
{
  start_static_fade_ex(r, g, b, false);
}

static void effect_static_fade_step(void)
{
  uint32_t now     = millis();
  uint32_t elapsed = now - g_effectStartMs;

  if (elapsed >= kStaticFadeMs)
    elapsed = kStaticFadeMs;

  float t = (float)elapsed / (float)kStaticFadeMs;

  uint8_t r = lerp_u8(g_staticFromR, g_staticToR, t);
  uint8_t g = lerp_u8(g_staticFromG, g_staticToG, t);
  uint8_t b = lerp_u8(g_staticFromB, g_staticToB, t);

  for (uint16_t i = 0; i < LED_COUNT; i++)
  {
    uint16_t idx = mapLED(i);
    if (g_staticUseGamma)
      setPixelGamma(idx, r, g, b);
    else
      setPixelRaw(idx, r, g, b);
  }

  strip.show();

  if (elapsed == kStaticFadeMs)
  {
    g_effect        = FRONT_EFFECT_NONE;
    g_hasLastStatic = true;
  }
}

/* ============================================================
 *                      BLINK 0x02
 * ============================================================ */

static const uint32_t kBlink02Period = NEO_CFG_BLINK02_PERIOD_MS;

static void effect_blink02_step(void)
{
  uint32_t now = millis();
  float phase = (float)((now - g_effectStartMs) % kBlink02Period) /
                (float)kBlink02Period;

  float bright = (phase < 0.5f) ? (phase * 2.0f)
                                : ((1.0f - phase) * 2.0f);

  uint8_t r = (uint8_t)((float)COLOR_CMD_03_R * bright);
  uint8_t g = (uint8_t)((float)COLOR_CMD_03_G * bright);
  uint8_t b = (uint8_t)((float)COLOR_CMD_03_B * bright);

  for (uint16_t i = 0; i < LED_COUNT; i++)
    setPixelGamma(mapLED(i), r, g, b);

  strip.show();
}

/* ============================================================
 *          BOOT WAIT (power-on, PC not connected yet)
 *  - Sky blue triangle dimming: 0.5 s up + 0.5 s down.
 *  - Ends on the first byte from the host (neo_effect_host_seen()).
 * ============================================================ */

static const uint32_t kBootWaitPeriod = NEO_CFG_BOOT_WAIT_PERIOD_MS;

static void effect_boot_wait_step(void)
{
  uint32_t now = millis();
  float phase = (float)((now - g_effectStartMs) % kBootWaitPeriod) /
                (float)kBootWaitPeriod;

  float bright = (phase < 0.5f) ? (phase * 2.0f)
                                : ((1.0f - phase) * 2.0f);

  uint8_t r = (uint8_t)((float)COLOR_BOOT_WAIT_R * bright);
  uint8_t g = (uint8_t)((float)COLOR_BOOT_WAIT_G * bright);
  uint8_t b = (uint8_t)((float)COLOR_BOOT_WAIT_B * bright);

  for (uint16_t i = 0; i < LED_COUNT; i++)
    setPixelGamma(mapLED(i), r, g, b);

  strip.show();
}

/* ============================================================
 *                 COLOR BLINK (triangle)
 * ============================================================ */

static uint8_t g_colorBlinkR = 0;
static uint8_t g_colorBlinkG = 0;
static uint8_t g_colorBlinkB = 0;

static void start_color_blink(uint8_t r, uint8_t g, uint8_t b)
{
  g_colorBlinkR = r;
  g_colorBlinkG = g;
  g_colorBlinkB = b;

  g_effect        = FRONT_EFFECT_COLOR_BLINK;
  g_effectStartMs = millis();
}

static void effect_color_blink_step(void)
{
  uint32_t now = millis();
  float phase = (float)((now - g_effectStartMs) % kBlink02Period) /
                (float)kBlink02Period;

  float bright = (phase < 0.5f) ? (phase * 2.0f)
                                : ((1.0f - phase) * 2.0f);

  uint8_t r = (uint8_t)((float)g_colorBlinkR * bright);
  uint8_t g = (uint8_t)((float)g_colorBlinkG * bright);
  uint8_t b = (uint8_t)((float)g_colorBlinkB * bright);

  for (uint16_t i = 0; i < LED_COUNT; i++)
    setPixelGamma(mapLED(i), r, g, b);

  strip.show();
}

/* ============================================================
 *                      CROSS PAIR
 * ============================================================ */

static uint8_t g_crossC1R, g_crossC1G, g_crossC1B;
static uint8_t g_crossC2R, g_crossC2G, g_crossC2B;

static void effect_cross_pair_step(void)
{
  uint32_t now = millis();
  float t = (float)(now - g_effectStartMs) / 1000.0f;

  float w = 0.5f + 0.5f * sinf(2.0f * M_PI * 0.5f * t);

  for (uint16_t i = 0; i < LED_COUNT; i++)
  {
    bool even = ((i & 1U) == 0);
    float local = even ? w : (1.0f - w);

    uint8_t r = lerp_u8(g_crossC1R, g_crossC2R, local);
    uint8_t g = lerp_u8(g_crossC1G, g_crossC2G, local);
    uint8_t b = lerp_u8(g_crossC1B, g_crossC2B, local);

    setPixelGamma(mapLED(i), r, g, b);
  }

  strip.show();
}

/* ============================================================
 *                    MUSIC (0x12, FX_TYPE == 0x03)
 * ============================================================ */

static void effect_music_12_step(void)
{
  uint32_t now = millis();
  float t = (float)(now - g_effectStartMs) / 1000.0f;

  float s = 0.5f + 0.5f * sinf(2.0f * M_PI * 0.2f * t);

  uint8_t r = lerp_u8(255, 243, s);
  uint8_t g = lerp_u8(235, 203, s);
  uint8_t b = lerp_u8(247, 246, s);

  for (uint16_t i = 0; i < LED_COUNT; i++)
    setPixelGamma(mapLED(i), r, g, b);

  strip.show();
}

/* ============================================================
 *                SEQ_13 / SEQ_14 / SEQ_15 (0x13~0x15)
 * ============================================================ */

static const uint32_t kSeq13CycleMs  = NEO_CFG_SEQ13_CYCLE_MS;
static const uint32_t kSeq14PeriodMs = NEO_CFG_SEQ14_PERIOD_MS;
static const uint32_t kSeq15CycleMs  = NEO_CFG_SEQ15_CYCLE_MS;

/* 0x13: Liquid ring */
static void effect_seq_13_step(void)
{
  uint32_t now = millis();
  float t = (float)((now - g_effectStartMs) % kSeq13CycleMs) /
            (float)kSeq13CycleMs;

  const float alpha = 0.35f;

  for (uint16_t i = 0; i < LED_COUNT; i++)
  {
    float offset = (float)i / (float)LED_COUNT;
    float local  = t - offset;
    if (local < 0.0f) local += 1.0f;

    float bright = fadeShort(local);
    bright = clamp01(bright);

    uint8_t targetR = (uint8_t)((float)COLOR_SEQ_R * bright);
    uint8_t targetG = (uint8_t)((float)COLOR_SEQ_G * bright);
    uint8_t targetB = (uint8_t)((float)COLOR_SEQ_B * bright);

    seq_smooth_apply(i, targetR, targetG, targetB, alpha);
  }

  strip.show();
}

/* 0x14: Group blink fade in/out */
static void effect_seq_14_step(void)
{
  uint32_t now = millis();
  float phase = (float)((now - g_effectStartMs) % kSeq14PeriodMs) /
                (float)kSeq14PeriodMs;

  float bright = (phase < 0.5f) ? (phase * 2.0f)
                                : ((1.0f - phase) * 2.0f);

  uint8_t baseR = (uint8_t)((float)COLOR_SEQ_R * bright);
  uint8_t baseG = (uint8_t)((float)COLOR_SEQ_G * bright);
  uint8_t baseB = (uint8_t)((float)COLOR_SEQ_B * bright);

  const float alpha = 0.35f;

  static const uint8_t grp[5][3] =
  {
    {0, 255, 255},
    {1, 7,   255},
    {2, 6,   255},
    {3, 5,   255},
    {4, 255, 255}
  };

  for (uint16_t i = 0; i < LED_COUNT; i++)
  {
    bool inGroup = false;

    for (uint8_t s = 0; s < 5 && !inGroup; s++)
    {
      for (uint8_t k = 0; k < 3; k++)
      {
        uint8_t led = grp[s][k];
        if (led == 255U) continue;
        if (led == i) { inGroup = true; break; }
      }
    }

    uint8_t targetR = inGroup ? baseR : 0;
    uint8_t targetG = inGroup ? baseG : 0;
    uint8_t targetB = inGroup ? baseB : 0;

    seq_smooth_apply(i, targetR, targetG, targetB, alpha);
  }

  strip.show();
}

/* 0x15: cumulative on -> cumulative off (same order) */
static void effect_seq_15_step(void)
{
  uint32_t now     = millis();
  uint32_t elapsed = now - g_effectStartMs;

  float phase = fmodf((float)elapsed / (float)kSeq15CycleMs, 2.0f);
  const float alpha = 0.25f;

  for (int i = 0; i < (int)LED_COUNT; i++)
  {
    float bright = 0.0f;

    if (phase < 1.0f)
    {
      float pos  = phase * (float)LED_COUNT;
      int   head = (int)floorf(pos);
      float frac = pos - (float)head;

      if (head >= (int)LED_COUNT) { head = (int)LED_COUNT - 1; frac = 1.0f; }

      if (i < head)        bright = 1.0f;
      else if (i == head)  bright = frac;
      else                 bright = 0.0f;
    }
    else
    {
      float pos  = (phase - 1.0f) * (float)LED_COUNT;
      int   off  = (int)floorf(pos);
      float frac = pos - (float)off;

      if (off >= (int)LED_COUNT) { off = (int)LED_COUNT - 1; frac = 1.0f; }

      if (i < off)        bright = 0.0f;
      else if (i == off)  bright = 1.0f - frac;
      else                bright = 1.0f;
    }

    bright = clamp01(bright);

    uint8_t targetR = (uint8_t)((float)COLOR_SEQ_R * bright);
    uint8_t targetG = (uint8_t)((float)COLOR_SEQ_G * bright);
    uint8_t targetB = (uint8_t)((float)COLOR_SEQ_B * bright);

    seq_smooth_apply((uint16_t)i, targetR, targetG, targetB, alpha);
  }

  strip.show();
}

/* ============================================================
 *                       AURORA (kept for compatibility)
 * ============================================================ */

static uint8_t g_auroraR, g_auroraG, g_auroraB;
static float   g_auroraSpeedCmd = 60.0f;
static float   g_auroraScaleCmd = 3.0f;
static float   g_auroraBaseCmd  = 0.2f;
static float   g_auroraAmpCmd   = 0.8f;

static void start_aurora(uint8_t r, uint8_t g, uint8_t b,
                         float speed, float scale,
                         float base, float amp)
{
  g_auroraR        = r;
  g_auroraG        = g;
  g_auroraB        = b;
  g_auroraSpeedCmd = speed;
  g_auroraScaleCmd = scale;
  g_auroraBaseCmd  = base;
  g_auroraAmpCmd   = amp;

  g_effect        = FRONT_EFFECT_AURORA_07_0A;
  g_effectStartMs = millis();
}

static void effect_aurora_step(void)
{
  uint32_t now = millis();
  float t = (float)(now - g_effectStartMs) / 1000.0f;

  for (uint16_t i = 0; i < LED_COUNT; i++)
  {
    float x = (LED_COUNT > 1) ? (float)i / (float)(LED_COUNT - 1) : 0.0f;

    float phase1 = 2.0f * M_PI * (g_auroraScaleCmd * x - g_auroraSpeedCmd * t);
    float phase2 = 2.0f * M_PI * ((g_auroraScaleCmd * 0.5f) * x +
                                  g_auroraSpeedCmd * 0.6f * t);

    float w1 = 0.5f + 0.5f * sinf(phase1);
    float w2 = 0.5f + 0.5f * sinf(phase2);

    float wave = 0.6f * w1 + 0.4f * w2;
    wave = clamp01(wave);

    float bright = g_auroraBaseCmd + g_auroraAmpCmd * wave;
    bright = clamp01(bright);

    uint8_t r = (uint8_t)((float)g_auroraR * bright);
    uint8_t g = (uint8_t)((float)g_auroraG * bright);
    uint8_t b = (uint8_t)((float)g_auroraB * bright);

    setPixelGamma(mapLED(i), r, g, b);
  }

  strip.show();
}

/* ============================================================
 *       ENV ENGINE (FIRE / TINDLE / SIMPLE GLOBAL FADE)
 * ============================================================ */

struct env_params_s {
  uint32_t fadeInMs;
  uint32_t holdMs;
  uint32_t fadeOutMs;
  float    minBrightness;
  float    maxBrightness;

  uint8_t  baseR, baseG, baseB;
  uint8_t  sunR,  sunG,  sunB;

  float    sunPosSpeed;
  float    sunPosAmp;
  float    sunFlickerSpeed;
  float    sunFlickerMin;
  float    sunFlickerMax;
  float    spread;
  float    sunStrengthMax;

  float    fireSpatialAmp;
  float    fireNoiseAmp;
  float    fireSwaySpeed;

  env_mode_t mode;
};

static env_params_t g_envParams;
static uint32_t     g_envStartMs  = 0;
static bool         g_envUseGamma = true;

/* FIRE preset */
static const env_params_t kEnvFire =
{
  NEO_CFG_FIRE_FADEIN_MS,
  NEO_CFG_FIRE_HOLD_MS,
  NEO_CFG_FIRE_FADEOUT_MS,

  NEO_CFG_PATCH_MIN_BRIGHTNESS,
  NEO_CFG_PATCH_MAX_BRIGHTNESS,

  COLOR_FIRE,
  COLOR_PATCH_SUN,

  0.00005f, 0.20f, 0.004f, 0.3f, 1.0f, 0.12f, 0.8f,

  0.20f, 0.50f, 0.40f,

  ENV_MODE_FIRE_SWAY
};

/* TINDLE preset */
static const env_params_t kEnvTindle =
{
  NEO_CFG_TINDLE_FADEIN_MS,
  NEO_CFG_TINDLE_HOLD_MS,
  NEO_CFG_TINDLE_FADEOUT_MS,

  NEO_CFG_PATCH_MIN_BRIGHTNESS,
  NEO_CFG_PATCH_MAX_BRIGHTNESS,

  COLOR_FOREST,
  COLOR_PATCH_SUN,

  0.00005f, 0.20f, 0.004f, 0.3f, 1.0f, 0.12f, 0.8f,

  0.0f, 0.0f, 0.0f,

  ENV_MODE_SUN_BEAM
};

/* SIMPLE global fade preset */
static const env_params_t kEnvCustomFadeSimple =
{
  NEO_CFG_SIMPLE_FADEIN_MS,
  NEO_CFG_SIMPLE_HOLD_MS,
  NEO_CFG_SIMPLE_FADEOUT_MS,

  NEO_CFG_PATCH_MIN_BRIGHTNESS,
  NEO_CFG_PATCH_MAX_BRIGHTNESS,

  COLOR_FOREST,
  0, 0, 0,

  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,

  0.0f, 0.0f, 0.0f,

  ENV_MODE_SIMPLE_FADE
};

static void start_env_effect_ex(const env_params_t *p,
                                front_effect_t eff,
                                bool useGamma)
{
  g_envParams     = *p;
  g_envStartMs    = millis();
  g_effectStartMs = g_envStartMs;
  g_envUseGamma   = useGamma;
  g_effect        = eff;
}

static void start_env_effect_raw(const env_params_t *p, front_effect_t eff)
{
  start_env_effect_ex(p, eff, false);
}

static void start_tindle_color_ex(uint8_t r, uint8_t g, uint8_t b, bool useGamma)
{
  env_params_t p = kEnvTindle;
  p.baseR = r;
  p.baseG = g;
  p.baseB = b;
  start_env_effect_ex(&p, FRONT_EFFECT_CUSTOM_FADE, useGamma);
}

static void start_tindle_color_raw(uint8_t r, uint8_t g, uint8_t b)
{
  start_tindle_color_ex(r, g, b, false);
}

static void start_custom_fade_color_ex(uint8_t r, uint8_t g, uint8_t b,
                                       uint32_t inMs, uint32_t holdMs, uint32_t outMs,
                                       bool useGamma)
{
  env_params_t p = kEnvCustomFadeSimple;
  p.baseR     = r;
  p.baseG     = g;
  p.baseB     = b;
  p.fadeInMs  = inMs;
  p.holdMs    = holdMs;
  p.fadeOutMs = outMs;
  start_env_effect_ex(&p, FRONT_EFFECT_CUSTOM_FADE, useGamma);
}

static void start_custom_fade_color_raw(uint8_t r, uint8_t g, uint8_t b,
                                        uint32_t inMs, uint32_t holdMs, uint32_t outMs)
{
  start_custom_fade_color_ex(r, g, b, inMs, holdMs, outMs, false);
}

static float env_calc_brightness(uint32_t nowMs)
{
  uint32_t total = g_envParams.fadeInMs + g_envParams.holdMs + g_envParams.fadeOutMs;

  if (total == 0U)
    return g_envParams.maxBrightness;

  uint32_t t = (nowMs - g_envStartMs) % total;

  if (t < g_envParams.fadeInMs)
  {
    float n = (float)t / (float)g_envParams.fadeInMs;
    return g_envParams.minBrightness +
           (g_envParams.maxBrightness - g_envParams.minBrightness) * n;
  }

  t -= g_envParams.fadeInMs;
  if (t < g_envParams.holdMs)
    return g_envParams.maxBrightness;

  t -= g_envParams.holdMs;

  if (g_envParams.fadeOutMs == 0U)
    return g_envParams.minBrightness;

  float n = (float)t / (float)g_envParams.fadeOutMs;
  return g_envParams.maxBrightness -
         (g_envParams.maxBrightness - g_envParams.minBrightness) * n;
}

static void effect_env_step(void)
{
  uint32_t now     = millis();
  float    ambient = env_calc_brightness(now);
  unsigned long ms = now;

  if (g_envParams.mode == ENV_MODE_FIRE_SWAY)
  {
    float t = ms * 0.001f;

    for (int i = 0; i < (int)LED_COUNT; i++)
    {
      float pos   = (LED_COUNT > 1) ? (float)i / (float)(LED_COUNT - 1) : 0.5f;
      float phase = pos * M_PI;

      float spatial = sinf(phase + t * g_envParams.fireSwaySpeed) * g_envParams.fireSpatialAmp;
      float noise   = ((float)random(-100, 101) / 100.0f) * g_envParams.fireNoiseAmp;

      float br = ambient + spatial + noise;
      if (br < 0.02f) br = 0.02f;
      if (br > 1.0f)  br = 1.0f;

      uint8_t R = (uint8_t)((float)g_envParams.baseR * br);
      uint8_t G = (uint8_t)((float)g_envParams.baseG * br);
      uint8_t B = (uint8_t)((float)g_envParams.baseB * br);

      uint16_t idx = mapLED((uint16_t)i);
      if (g_envUseGamma) setPixelGamma(idx, R, G, B);
      else              setPixelRaw(idx, R, G, B);
    }

    strip.show();
    return;
  }

  if (g_envParams.mode == ENV_MODE_SIMPLE_FADE)
  {
    float f = ambient;

    uint8_t R = (uint8_t)((float)g_envParams.baseR * f);
    uint8_t G = (uint8_t)((float)g_envParams.baseG * f);
    uint8_t B = (uint8_t)((float)g_envParams.baseB * f);

    for (uint16_t i = 0; i < LED_COUNT; i++)
    {
      uint16_t idx = mapLED(i);
      if (g_envUseGamma) setPixelGamma(idx, R, G, B);
      else              setPixelRaw(idx, R, G, B);
    }

    strip.show();
    return;
  }

  float tSlow      = ms * g_envParams.sunPosSpeed;
  float sunPos     = 0.5f + g_envParams.sunPosAmp * sinf(tSlow);

  float tFast      = ms * g_envParams.sunFlickerSpeed;
  float sunFlicker = 0.5f + 0.5f * sinf(tFast);
  sunFlicker = g_envParams.sunFlickerMin +
               (g_envParams.sunFlickerMax - g_envParams.sunFlickerMin) * sunFlicker;

  const float spread = g_envParams.spread;

  for (int i = 0; i < (int)LED_COUNT; i++)
  {
    float x = (LED_COUNT > 1) ? (float)i / (float)(LED_COUNT - 1) : 0.5f;
    float d = x - sunPos;

    float beam = expf(-(d * d) / (2.0f * spread * spread));

    float sunStrength = beam * sunFlicker * ambient;
    if (sunStrength > g_envParams.sunStrengthMax)
      sunStrength = g_envParams.sunStrengthMax;

    float baseR = (float)g_envParams.baseR * ambient;
    float baseG = (float)g_envParams.baseG * ambient;
    float baseB = (float)g_envParams.baseB * ambient;

    float sunR  = (float)g_envParams.sunR * sunStrength;
    float sunG  = (float)g_envParams.sunG * sunStrength;
    float sunB  = (float)g_envParams.sunB * sunStrength;

    float mix  = sunStrength;
    float outR = baseR * (1.0f - mix) + sunR * mix;
    float outG = baseG * (1.0f - mix) + sunG * mix;
    float outB = baseB * (1.0f - mix) + sunB * mix;

    if (outR < 0.0f)   outR = 0.0f;
    if (outR > 255.0f) outR = 255.0f;
    if (outG < 0.0f)   outG = 0.0f;
    if (outG > 255.0f) outG = 255.0f;
    if (outB < 0.0f)   outB = 0.0f;
    if (outB > 255.0f) outB = 255.0f;

    uint8_t R = (uint8_t)outR;
    uint8_t G = (uint8_t)outG;
    uint8_t B = (uint8_t)outB;

    uint16_t idx = mapLED((uint16_t)i);
    if (g_envUseGamma) setPixelGamma(idx, R, G, B);
    else              setPixelRaw(idx, R, G, B);
  }

  strip.show();
}

/* ============================================================
 *                  CUSTOM STATIC (no animation)
 * ============================================================ */

static void start_custom_static(uint8_t r, uint8_t g, uint8_t b)
{
  for (uint16_t i = 0; i < LED_COUNT; i++)
    setPixelGamma(mapLED(i), r, g, b);

  strip.show();
  g_effect = FRONT_EFFECT_NONE;
}

/* ============================================================
 * 0x28: remembered 0x0D effect (fade-in 2s -> flowing gradient)
 *  - FIXED: remove modulo-phase jump
 *  - FIXED: remove while-wrap O(N) loops (use fmodf wrap)
 * ============================================================ */

static const uint32_t kFx28FadeInMs      = 2000UL;
static const int8_t   kFx28FlowDir       = -1;
static const float    kFx28SpeedUPerSec  = 0.35f;
static const float    kFx28WrapLen       = 0.35f;

static uint32_t g_fx28FlowStartMs = 0;

/* O(1) wrap helper: wraps u into [0, len) */
static inline float wrap_len(float u, float len)
{
  u = fmodf(u, len);
  if (u < 0.0f) u += len;
  return u;
}

static void grad01_sample(float pos01, uint8_t *r, uint8_t *g, uint8_t *b)
{
  pos01 = clamp01(pos01);

  float p0 = GRAD01_POS_0, p1 = GRAD01_POS_1;
  uint8_t r0 = GRAD01_C0_R, g0 = GRAD01_C0_G, b0 = GRAD01_C0_B;
  uint8_t r1 = GRAD01_C1_R, g1 = GRAD01_C1_G, b1 = GRAD01_C1_B;

  if (pos01 <= GRAD01_POS_1)
  {
    p0 = GRAD01_POS_0; p1 = GRAD01_POS_1;
    r0 = GRAD01_C0_R;  g0 = GRAD01_C0_G;  b0 = GRAD01_C0_B;
    r1 = GRAD01_C1_R;  g1 = GRAD01_C1_G;  b1 = GRAD01_C1_B;
  }
  else if (pos01 <= GRAD01_POS_2)
  {
    p0 = GRAD01_POS_1; p1 = GRAD01_POS_2;
    r0 = GRAD01_C1_R;  g0 = GRAD01_C1_G;  b0 = GRAD01_C1_B;
    r1 = GRAD01_C2_R;  g1 = GRAD01_C2_G;  b1 = GRAD01_C2_B;
  }
  else
  {
    p0 = GRAD01_POS_2; p1 = GRAD01_POS_3;
    r0 = GRAD01_C2_R;  g0 = GRAD01_C2_G;  b0 = GRAD01_C2_B;
    r1 = GRAD01_C3_R;  g1 = GRAD01_C3_G;  b1 = GRAD01_C3_B;
  }

  float t = (p1 > p0) ? (pos01 - p0) / (p1 - p0) : 0.0f;
  t = clamp01(t);

  *r = lerp_u8(r0, r1, t);
  *g = lerp_u8(g0, g1, t);
  *b = lerp_u8(b0, b1, t);
}

static void grad01_sample_wrapped(float u, uint8_t *r, uint8_t *g, uint8_t *b)
{
  const float len = 1.0f + kFx28WrapLen;

  /* FIX: while-loop wrap 제거 */
  u = wrap_len(u, len);

  if (u <= 1.0f)
  {
    grad01_sample(u, r, g, b);
    return;
  }

  float t = (u - 1.0f) / kFx28WrapLen;
  t = clamp01(t);

  uint8_t lr = GRAD01_C3_R, lg = GRAD01_C3_G, lb = GRAD01_C3_B;
  uint8_t fr = GRAD01_C0_R, fg = GRAD01_C0_G, fb = GRAD01_C0_B;

  *r = lerp_u8(lr, fr, t);
  *g = lerp_u8(lg, fg, t);
  *b = lerp_u8(lb, fb, t);
}

static void effect_grad_flow_28_step(void)
{
  uint32_t now = millis();
  uint32_t elapsed = now - g_effectStartMs;

  float fade = 1.0f;

  if (elapsed < kFx28FadeInMs)
  {
    fade = (float)elapsed / (float)kFx28FadeInMs;
    fade = clamp01(fade);
  }
  else
  {
    if (g_fx28FlowStartMs == 0U)
      g_fx28FlowStartMs = now;   /* flow timeline start (fixed) */
  }

  uint32_t flowBase = (g_fx28FlowStartMs == 0U) ? now : g_fx28FlowStartMs;
  uint32_t flowElapsed = now - flowBase;

  /* FIX: modulo 제거 (jump 방지) */
  float tSec  = (float)flowElapsed / 1000.0f;
  float phase = kFx28SpeedUPerSec * tSec;
  float dir   = (float)kFx28FlowDir;

  for (uint16_t i = 0; i < LED_COUNT; i++)
  {
    float x = (LED_COUNT > 0) ? ((float)i / (float)LED_COUNT) : 0.0f;
    float u = x + dir * phase;

    uint8_t r0, g0, b0;
    grad01_sample_wrapped(u, &r0, &g0, &b0);

    uint8_t r = (uint8_t)((float)r0 * fade + 0.5f);
    uint8_t g = (uint8_t)((float)g0 * fade + 0.5f);
    uint8_t b = (uint8_t)((float)b0 * fade + 0.5f);

    setPixelGamma(mapLED(i), r, g, b);
  }

  strip.show();
}

/* ============================================================
 *                      EFFECT DISPATCHER
 * ============================================================ */

static void effect_step_internal(void)
{
  switch (g_effect)
  {
    case FRONT_EFFECT_STATIC_FADE:   effect_static_fade_step();  break;
    case FRONT_EFFECT_BLINK02:       effect_blink02_step();      break;
    case FRONT_EFFECT_COLOR_BLINK:   effect_color_blink_step();  break;
    case FRONT_EFFECT_CROSS_PAIR:    effect_cross_pair_step();   break;
    case FRONT_EFFECT_MUSIC_12:      effect_music_12_step();     break;
    case FRONT_EFFECT_SEQ_13:        effect_seq_13_step();       break;
    case FRONT_EFFECT_SEQ_14:        effect_seq_14_step();       break;
    case FRONT_EFFECT_SEQ_15:        effect_seq_15_step();       break;
    case FRONT_EFFECT_AURORA_07_0A:  effect_aurora_step();       break;
    case FRONT_EFFECT_FIRE:
    case FRONT_EFFECT_CUSTOM_FADE:   effect_env_step();          break;
    case FRONT_EFFECT_GRAD_FLOW_28:  effect_grad_flow_28_step(); break;
    case FRONT_EFFECT_BOOT_WAIT:     effect_boot_wait_step();    break;
    default: break;
  }
}

/* ============================================================
 *                      COMMAND HANDLER
 * ============================================================ */

static void handle_off(void)
{
  g_effect = FRONT_EFFECT_NONE;
  strip.clear();
  strip.show();
}

static void handle_command_internal(uint8_t cmd)
{
  if (cmd == 0x01 || cmd == 0x06)
  {
    handle_off();
    return;
  }

  switch (cmd)
  {
    case 0x02:
      if (FX_TYPE == 0x04)
      {
        g_effect        = FRONT_EFFECT_BLINK02;
        g_effectStartMs = millis();
      }
      else
      {
        start_custom_static(COLOR_CMD_03);
      }
      break;

    case 0x03: start_static_fade(COLOR_CMD_03);  break;
    case 0x04: start_static_fade(COLOR_CMD_04);  break;
    case 0x05: start_static_fade(COLOR_CMD_05);  break;

    case 0x07:
    #if (FX_TYPE == 0x01) || (FX_TYPE == 0x02) || (FX_TYPE == 0x04)
      start_static_fade_raw(COLOR_FIRE);
    #elif (FX_TYPE == 0x03)
      start_env_effect_raw(&kEnvFire, FRONT_EFFECT_FIRE);
    #else
      start_static_fade_raw(COLOR_FIRE);
    #endif
      break;

    case 0x08:
    #if (FX_TYPE == 0x01) || (FX_TYPE == 0x02)
      start_static_fade_raw(COLOR_SUNSET);
    #elif (FX_TYPE == 0x03)
      start_tindle_color_raw(COLOR_SUNSET);
    #elif (FX_TYPE == 0x04)
      start_custom_fade_color_raw(COLOR_SUNSET,
                                  NEO_CFG_SIMPLE_FADEIN_MS,
                                  NEO_CFG_SIMPLE_HOLD_MS,
                                  NEO_CFG_SIMPLE_FADEOUT_MS);
    #else
      start_static_fade_raw(COLOR_SUNSET);
    #endif
      break;

    case 0x09:
    #if (FX_TYPE == 0x01) || (FX_TYPE == 0x02)
      start_static_fade_raw(COLOR_FOREST);
    #elif (FX_TYPE == 0x03)
      start_tindle_color_raw(COLOR_FOREST);
    #elif (FX_TYPE == 0x04)
      start_custom_fade_color_raw(COLOR_FOREST,
                                  NEO_CFG_SIMPLE_FADEIN_MS,
                                  NEO_CFG_SIMPLE_HOLD_MS,
                                  NEO_CFG_SIMPLE_FADEOUT_MS);
    #else
      start_static_fade_raw(COLOR_FOREST);
    #endif
      break;

    case 0x0A:
    #if (FX_TYPE == 0x01) || (FX_TYPE == 0x02)
      start_static_fade_raw(COLOR_LAKE);
    #elif (FX_TYPE == 0x03)
      start_tindle_color_raw(COLOR_LAKE);
    #elif (FX_TYPE == 0x04)
      start_custom_fade_color_raw(COLOR_LAKE,
                                  NEO_CFG_SIMPLE_FADEIN_MS,
                                  NEO_CFG_SIMPLE_HOLD_MS,
                                  NEO_CFG_SIMPLE_FADEOUT_MS);
    #else
      start_static_fade_raw(COLOR_LAKE);
    #endif
      break;

    case 0x0B: start_static_fade(COLOR_CMD_0B);  break;
    case 0x0C: start_static_fade(COLOR_CMD_0C);  break;
    case 0x0D: start_static_fade(COLOR_CMD_0D);  break;
    case 0x0E: start_static_fade(COLOR_CMD_0E);  break;
    case 0x0F: start_static_fade(COLOR_CMD_0F);  break;

    case 0x11:
      if (FX_TYPE == 0x03)
        start_static_fade(COLOR_CMD_11);
      break;

    case 0x12:
      if (FX_TYPE == 0x03)
      {
        g_effect        = FRONT_EFFECT_MUSIC_12;
        g_effectStartMs = millis();
      }
      break;

    case 0x13:
      if (FX_TYPE == 0x04)
      {
        seq_smooth_reset();
        g_effect        = FRONT_EFFECT_SEQ_13;
        g_effectStartMs = millis();
      }
      break;

    case 0x14:
      if (FX_TYPE == 0x04)
      {
        seq_smooth_reset();
        g_effect        = FRONT_EFFECT_SEQ_14;
        g_effectStartMs = millis();
      }
      break;

    case 0x15:
      if (FX_TYPE == 0x04)
      {
        seq_smooth_reset();
        g_effect        = FRONT_EFFECT_SEQ_15;
        g_effectStartMs = millis();
      }
      break;

    case 0x16:
      if (FX_TYPE == 0x01 || FX_TYPE == 0x02) start_color_blink(COLOR_CMD_16);
      break;
    case 0x17:
      if (FX_TYPE == 0x01 || FX_TYPE == 0x02) start_color_blink(COLOR_CMD_17);
      break;
    case 0x18:
      if (FX_TYPE == 0x01 || FX_TYPE == 0x02) start_color_blink(COLOR_CMD_18);
      break;
    case 0x19:
      if (FX_TYPE == 0x01 || FX_TYPE == 0x02) start_color_blink(COLOR_CMD_19);
      break;
    case 0x1A:
      if (FX_TYPE == 0x01 || FX_TYPE == 0x02) start_custom_static(COLOR_CMD_1A);
      break;

    case 0x1B:
      if (FX_TYPE == 0x03) start_custom_static(COLOR_CMD_1B);
      break;
    case 0x1C:
      if (FX_TYPE == 0x03) start_color_blink(COLOR_CMD_1C);
      break;
    case 0x1D:
      if (FX_TYPE == 0x03) start_color_blink(COLOR_CMD_1D);
      break;
    case 0x1E:
      if (FX_TYPE == 0x03) start_custom_static(COLOR_CMD_1E);
      break;
    case 0x1F:
      if (FX_TYPE == 0x03) start_custom_static(COLOR_CMD_1F);
      break;
    case 0x20:
      if (FX_TYPE == 0x03) start_custom_static(COLOR_CMD_20);
      break;
    case 0x21:
      if (FX_TYPE == 0x03) start_color_blink(COLOR_CMD_21);
      break;
    case 0x22:
      if (FX_TYPE == 0x03) start_custom_static(COLOR_CMD_22);
      break;

    case 0x23:
      if (FX_TYPE == 0x01 || FX_TYPE == 0x02)
        start_custom_static(COLOR_CMD_23);
      break;

    case 0x24:
      if (FX_TYPE == 0x01 || FX_TYPE == 0x02)
        start_color_blink(COLOR_CMD_23);
      break;

    case 0x25:
      if (FX_TYPE == 0x01 || FX_TYPE == 0x02)
        start_color_blink(COLOR_CMD_25);
      break;

    case 0x26:
      if (FX_TYPE == 0x03)
        start_custom_static(COLOR_CMD_26);
      break;

    case 0x27:
      if (FX_TYPE == 0x03)
        start_color_blink(COLOR_CMD_27);
      break;

    /* 0x28: Common (DEVICE_ID independent) */
    case 0x28:
      g_effect          = FRONT_EFFECT_GRAD_FLOW_28;
      g_effectStartMs   = millis();
      g_fx28FlowStartMs = 0;
      break;

    default:
      break;
  }
}

/* ============================================================
 *                     PUBLIC API
 * ============================================================ */

void neo_effect_init(void)
{
  initGammaLUT();
  strip.begin();
  strip.clear();
  strip.show();

  /* Power-on indication: sky blue dimming until the host PC talks to us */
  g_effect        = FRONT_EFFECT_BOOT_WAIT;
  g_effectStartMs = millis();
}

void neo_effect_host_seen(void)
{
  /* First byte from the host -> PC connected, stop the boot indication.
   * Only once: a later effect (or OFF) is never touched by this. */
  if (g_effect == FRONT_EFFECT_BOOT_WAIT)
    handle_off();
}

uint8_t neo_effect_get_device_id(void)
{
  return (uint8_t)DEVICE_ID;
}

void neo_effect_handle_command(uint8_t cmd)
{
  handle_command_internal(cmd);
}

void neo_effect_step(void)
{
  effect_step_internal();
}
