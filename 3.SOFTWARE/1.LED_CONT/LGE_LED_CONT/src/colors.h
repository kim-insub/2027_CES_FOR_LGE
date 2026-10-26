#ifndef COLORS_H_
#define COLORS_H_

#include <stdint.h>

/* ============================================================
 * Project : lge_kisk_neo_frt
 * File    : colors.h
 * Note    : Centralized color/gradient definitions.
 * ============================================================ */

/* ===================== Base Palette ===================== */

#define COLOR_FIRE_R      255
#define COLOR_FIRE_G       90
#define COLOR_FIRE_B       15
#define COLOR_FIRE         COLOR_FIRE_R, COLOR_FIRE_G, COLOR_FIRE_B

#define COLOR_SUNSET_R    255
#define COLOR_SUNSET_G     90
#define COLOR_SUNSET_B     15
#define COLOR_SUNSET       COLOR_SUNSET_R, COLOR_SUNSET_G, COLOR_SUNSET_B

#define COLOR_FOREST_R     30
#define COLOR_FOREST_G    180
#define COLOR_FOREST_B     40
#define COLOR_FOREST       COLOR_FOREST_R, COLOR_FOREST_G, COLOR_FOREST_B

#define COLOR_LAKE_R       50
#define COLOR_LAKE_G      150
#define COLOR_LAKE_B       70
#define COLOR_LAKE         COLOR_LAKE_R, COLOR_LAKE_G, COLOR_LAKE_B

/* ===================== Static Command Colors ===================== */

#define COLOR_CMD_03_R    255
#define COLOR_CMD_03_G    235
#define COLOR_CMD_03_B    247
#define COLOR_CMD_03      COLOR_CMD_03_R, COLOR_CMD_03_G, COLOR_CMD_03_B

#define COLOR_CMD_04_R    237
#define COLOR_CMD_04_G    125
#define COLOR_CMD_04_B     49
#define COLOR_CMD_04      COLOR_CMD_04_R, COLOR_CMD_04_G, COLOR_CMD_04_B

#define COLOR_CMD_05_R    222
#define COLOR_CMD_05_G    235
#define COLOR_CMD_05_B    247
#define COLOR_CMD_05      COLOR_CMD_05_R, COLOR_CMD_05_G, COLOR_CMD_05_B

#define COLOR_CMD_0B_R     63
#define COLOR_CMD_0B_G    189
#define COLOR_CMD_0B_B    255
#define COLOR_CMD_0B      COLOR_CMD_0B_R, COLOR_CMD_0B_G, COLOR_CMD_0B_B

#define COLOR_CMD_0C_R    254
#define COLOR_CMD_0C_G    243
#define COLOR_CMD_0C_B    196
#define COLOR_CMD_0C      COLOR_CMD_0C_R, COLOR_CMD_0C_G, COLOR_CMD_0C_B

#define COLOR_CMD_0D_R     63
#define COLOR_CMD_0D_G    189
#define COLOR_CMD_0D_B    255
#define COLOR_CMD_0D      COLOR_CMD_0D_R, COLOR_CMD_0D_G, COLOR_CMD_0D_B

#define COLOR_CMD_0E_R    244
#define COLOR_CMD_0E_G    177
#define COLOR_CMD_0E_B    131
#define COLOR_CMD_0E      COLOR_CMD_0E_R, COLOR_CMD_0E_G, COLOR_CMD_0E_B

#define COLOR_CMD_0F_R    244
#define COLOR_CMD_0F_G    177
#define COLOR_CMD_0F_B    131
#define COLOR_CMD_0F      COLOR_CMD_0F_R, COLOR_CMD_0F_G, COLOR_CMD_0F_B

#define COLOR_CMD_11_R    243
#define COLOR_CMD_11_G    203
#define COLOR_CMD_11_B    196
#define COLOR_CMD_11      COLOR_CMD_11_R, COLOR_CMD_11_G, COLOR_CMD_11_B

/* ===================== SEQ Colors ===================== */

#define COLOR_SEQ_R       255
#define COLOR_SEQ_G       235
#define COLOR_SEQ_B       247
#define COLOR_SEQ         COLOR_SEQ_R, COLOR_SEQ_G, COLOR_SEQ_B

/* ===================== Patch / Sun Highlight ===================== */

#define COLOR_PATCH_SUN_R 255
#define COLOR_PATCH_SUN_G 230
#define COLOR_PATCH_SUN_B 180
#define COLOR_PATCH_SUN   COLOR_PATCH_SUN_R, COLOR_PATCH_SUN_G, COLOR_PATCH_SUN_B

/* ===================== Boot Wait (power-on, PC not connected) ===================== */
/* Sky blue (same as CMD_0B / 0x0D) */
#define COLOR_BOOT_WAIT_R  63
#define COLOR_BOOT_WAIT_G 189
#define COLOR_BOOT_WAIT_B 255
#define COLOR_BOOT_WAIT   COLOR_BOOT_WAIT_R, COLOR_BOOT_WAIT_G, COLOR_BOOT_WAIT_B

/* ===================== New Command Colors ===================== */

/* 0x16~0x1A (for DEVICE_ID 0x01/0x02 only) */
#define COLOR_CMD_16_R     14
#define COLOR_CMD_16_G    229
#define COLOR_CMD_16_B     97
#define COLOR_CMD_16       COLOR_CMD_16_R, COLOR_CMD_16_G, COLOR_CMD_16_B

#define COLOR_CMD_17_R    251
#define COLOR_CMD_17_G    243
#define COLOR_CMD_17_B      7
#define COLOR_CMD_17       COLOR_CMD_17_R, COLOR_CMD_17_G, COLOR_CMD_17_B

#define COLOR_CMD_18_R    255
#define COLOR_CMD_18_G      0
#define COLOR_CMD_18_B      0
#define COLOR_CMD_18       COLOR_CMD_18_R, COLOR_CMD_18_G, COLOR_CMD_18_B

#define COLOR_CMD_19_R    255
#define COLOR_CMD_19_G    195
#define COLOR_CMD_19_B      0
#define COLOR_CMD_19       COLOR_CMD_19_R, COLOR_CMD_19_G, COLOR_CMD_19_B

#define COLOR_CMD_1A_R     23
#define COLOR_CMD_1A_G    203
#define COLOR_CMD_1A_B    253
#define COLOR_CMD_1A       COLOR_CMD_1A_R, COLOR_CMD_1A_G, COLOR_CMD_1A_B

/* 0x1B~0x22 (for DEVICE_ID 0x03 only) */
#define COLOR_CMD_1B_R    145
#define COLOR_CMD_1B_G    202
#define COLOR_CMD_1B_B    117
#define COLOR_CMD_1B       COLOR_CMD_1B_R, COLOR_CMD_1B_G, COLOR_CMD_1B_B

#define COLOR_CMD_1C_R    243
#define COLOR_CMD_1C_G     90
#define COLOR_CMD_1C_B     60
#define COLOR_CMD_1C       COLOR_CMD_1C_R, COLOR_CMD_1C_G, COLOR_CMD_1C_B

#define COLOR_CMD_1D_R    202
#define COLOR_CMD_1D_G    117
#define COLOR_CMD_1D_B    201
#define COLOR_CMD_1D       COLOR_CMD_1D_R, COLOR_CMD_1D_G, COLOR_CMD_1D_B

#define COLOR_CMD_1E_R    249
#define COLOR_CMD_1E_G    134
#define COLOR_CMD_1E_B     34
#define COLOR_CMD_1E       COLOR_CMD_1E_R, COLOR_CMD_1E_G, COLOR_CMD_1E_B

#define COLOR_CMD_1F_R     92
#define COLOR_CMD_1F_G    201
#define COLOR_CMD_1F_B    188
#define COLOR_CMD_1F       COLOR_CMD_1F_R, COLOR_CMD_1F_G, COLOR_CMD_1F_B

#define COLOR_CMD_20_R    178
#define COLOR_CMD_20_G     46
#define COLOR_CMD_20_B    202
#define COLOR_CMD_20       COLOR_CMD_20_R, COLOR_CMD_20_G, COLOR_CMD_20_B

#define COLOR_CMD_21_R    171
#define COLOR_CMD_21_G     71
#define COLOR_CMD_21_B    225
#define COLOR_CMD_21       COLOR_CMD_21_R, COLOR_CMD_21_G, COLOR_CMD_21_B

#define COLOR_CMD_22_R    255
#define COLOR_CMD_22_G    195
#define COLOR_CMD_22_B      0
#define COLOR_CMD_22       COLOR_CMD_22_R, COLOR_CMD_22_G, COLOR_CMD_22_B

/* ===================== Additions (existing) ===================== */

/* 0x23~0x25 (for DEVICE_ID 0x01/0x02 only) */
#define COLOR_CMD_23_R    218
#define COLOR_CMD_23_G    153
#define COLOR_CMD_23_B    239
#define COLOR_CMD_23      COLOR_CMD_23_R, COLOR_CMD_23_G, COLOR_CMD_23_B

#define COLOR_CMD_25_R     14
#define COLOR_CMD_25_G    183
#define COLOR_CMD_25_B    229
#define COLOR_CMD_25      COLOR_CMD_25_R, COLOR_CMD_25_G, COLOR_CMD_25_B

/* 0x26~0x27 (for DEVICE_ID 0x03 only) */
#define COLOR_CMD_26_R    111
#define COLOR_CMD_26_G    237
#define COLOR_CMD_26_B    244
#define COLOR_CMD_26      COLOR_CMD_26_R, COLOR_CMD_26_G, COLOR_CMD_26_B

#define COLOR_CMD_27_R    255
#define COLOR_CMD_27_G    195
#define COLOR_CMD_27_B      0
#define COLOR_CMD_27      COLOR_CMD_27_R, COLOR_CMD_27_G, COLOR_CMD_27_B

/* ============================================================
 * NEW: 0x28 uses "remembered 0x0D" gradient table (GRADIENT_TABLE_01)
 * - Colors only are defined here.
 * - Positions are also provided as constants for convenience.
 * ============================================================ */

/* Gradient Table 01 positions */
#define GRAD01_POS_0   (0.0f)
#define GRAD01_POS_1   (0.35f)
#define GRAD01_POS_2   (0.75f)
#define GRAD01_POS_3   (1.0f)

/* Gradient Table 01 colors */
#define GRAD01_C0_R    255
#define GRAD01_C0_G     49
#define GRAD01_C0_B     46

#define GRAD01_C1_R    229
#define GRAD01_C1_G     29
#define GRAD01_C1_B    238

#define GRAD01_C2_R    180
#define GRAD01_C2_G     50
#define GRAD01_C2_B    245

#define GRAD01_C3_R    130
#define GRAD01_C3_G     39
#define GRAD01_C3_B    255

#endif /* COLORS_H_ */
