#ifndef NEO_EFFECT_CONFIG_H_
#define NEO_EFFECT_CONFIG_H_

#include <stdint.h>

/* ============================================================
 * Effect Timing / Profile Config
 *  - 이 파일에서 이펙트 시간/주기/페이드 파라미터를 조정
 * ============================================================ */

/* -------- Static Fade -------- */
#define NEO_CFG_STATIC_FADE_MS            500UL   // 0x03~0x05 등 색 전환 페이드 시간

/* -------- Blink 0x02 -------- */
#define NEO_CFG_BLINK02_PERIOD_MS        1500UL  // breathing period

/* -------- Boot wait (power-on, PC not connected) -------- */
#define NEO_CFG_BOOT_WAIT_PERIOD_MS      1000UL  // sky blue dimming: 0.5s up + 0.5s down

/* -------- SEQ 0x13 / 0x14 / 0x15 -------- */
#define NEO_CFG_SEQ13_CYCLE_MS           1800UL
#define NEO_CFG_SEQ14_PERIOD_MS          2000UL  // 1s IN + 1s OUT
#define NEO_CFG_SEQ15_CYCLE_MS           1800UL

/* -------- Patch common brightness -------- */
#define NEO_CFG_PATCH_MIN_BRIGHTNESS     0.10f
#define NEO_CFG_PATCH_MAX_BRIGHTNESS     1.00f

/* -------- Patch timing: FIRE (0x07/0x16), TINDLE (0x08/0x17) -------- */
#define NEO_CFG_FIRE_FADEIN_MS           1500UL
#define NEO_CFG_FIRE_HOLD_MS             2000UL
#define NEO_CFG_FIRE_FADEOUT_MS          1500UL

#define NEO_CFG_TINDLE_FADEIN_MS         1500UL
#define NEO_CFG_TINDLE_HOLD_MS           2000UL
#define NEO_CFG_TINDLE_FADEOUT_MS        1500UL

/* -------- Patch timing: SIMPLE CUSTOM_FADE (0x09/0x0A/0x18) -------- */
#define NEO_CFG_SIMPLE_FADEIN_MS         1500UL
#define NEO_CFG_SIMPLE_HOLD_MS           3000UL
#define NEO_CFG_SIMPLE_FADEOUT_MS        1500UL

/* ---- Other patch parameters도 필요하면 여기로 이동 가능 ---- */
/* 예:
 * #define NEO_CFG_FIRE_SPATIAL_AMP       0.20f
 * #define NEO_CFG_FIRE_NOISE_AMP         0.50f
 * ...
 */

#endif /* NEO_EFFECT_CONFIG_H_ */
