#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>

/* ============================================================
 * Board / Hardware Config   (DC27_FRT - 통합 LED 펌웨어)
 *
 * DEVICE_ID  : PC 에 응답하는 ID (시스템 구조 v0.3: LED = 0x10~, +1 씩 증가)
 * FX_TYPE    : DEVICE_ID 로 결정되는 이펙트/LED 구성 (작년 DEVICE_ID 0x01~0x04 와 동일한 구성)
 *              neo_effect.cpp 는 FX_TYPE 으로 이펙트를 구분한다 (작년 코드 그대로).
 *
 *   DEVICE_ID   FX_TYPE (작년 ID)   구성         LED 수
 *   0x10        0x01                f150x01      180
 *   0x11        0x02                f180x02      180
 *   0x12        0x03                f180x03      150
 *   0x13        0x04                f8x04          8
 *
 * 대응을 바꾸려면 아래 #if 표에서 FX_TYPE 줄만 고친다.
 * DEVICE_ID 는 platformio.ini 의 환경별 -DDEVICE_ID=0x1x 로 정한다.
 * (코드 기본값과 ini 의 기본 환경 값은 항상 같게 유지)
 * ============================================================ */

/* -------- Default DEVICE_ID (ini 에서 -DDEVICE_ID 를 주면 그 값이 우선) -------- */
#ifndef DEVICE_ID
#define DEVICE_ID   0x10
#endif

/* -------- Common HW config -------- */
#ifndef UART_BAUD
#define UART_BAUD   115200
#endif
#include "board_pins.h"   /* LED_PIN : ESP32-C6 GPIO (see include/board_pins.h) */

/* -------- Firmware identity / BLE wireless update (src/ble_ota.h) --------
 * BLE name = "FW_TYPE 0x<DEVICE_ID> FW_DESC"  ->  "LED 0x10 DC27_FRT"
 * FW_VERSION : 펌웨어를 고칠 때마다 올린다. bin 파일 이름(bin/LED_0x10_v1.0.bin)과
 *              exe 화면에 그대로 나온다.
 * BLE 는 USB 가 빠져 있을 때(USB 전원 OFF 또는 케이블 분리, 보드는 5 V 로 계속 동작)만 켜진다.
 * BLE_OTA_PIN      : led_tool.exe 에서 BLE 연결/업데이트할 때 입력하는 PIN (3 회 틀리면 재부팅 전까지 잠김). */
#define FW_TYPE             "LED"
#define FW_DESC             "DC27_FRT"
#define FW_VERSION          "1.0"
#ifndef BLE_OTA_PIN
#define BLE_OTA_PIN         "0000"
#endif

/* -------- DEVICE_ID -> FX_TYPE / LED 구성 -------- */
#if   (DEVICE_ID == 0x10)

  #define FX_TYPE             0x01
  #define LED_COUNT           180
  #define LED_REVERSE_DEFAULT true

#elif (DEVICE_ID == 0x11)

  #define FX_TYPE             0x02
  #define LED_COUNT           180
  #define LED_REVERSE_DEFAULT true

#elif (DEVICE_ID == 0x12)

  #define FX_TYPE             0x03
  #define LED_COUNT           150
  #define LED_REVERSE_DEFAULT true

#elif (DEVICE_ID == 0x13)

  #define FX_TYPE             0x04
  #define LED_COUNT             8
  #define LED_REVERSE_DEFAULT true

#else
  #error "Unknown DEVICE_ID (expected 0x10~0x13). See config.h table."
#endif

#endif /* CONFIG_H_ */
