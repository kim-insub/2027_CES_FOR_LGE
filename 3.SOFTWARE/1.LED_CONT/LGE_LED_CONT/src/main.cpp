/**
 * ============================================================
 * Project   : lge_vbuck_front_fx (Unified Firmware)
 * File      : main.cpp / lge_kiosk_neo_frt.ino
 *
 * Description:
 *   - Minimal Arduino entry point for the front FX firmware.
 *   - Handles UART communication with the host PC.
 *   - Delegates all LED effect logic to the neo_effect module.
 *
 * Protocol (summary):
 *   - Baud rate : UART_BAUD (see config.h)
 *
 *   Host → Device:
 *     0x00 : ID request
 *            - Device replies with 1-byte DEVICE_ID.
 *
 *     0x01 : OFF (all LEDs off, stop animation)
 *     0x02~0x28 : Effect selection command
 *            - See neo_effect_handle_command() and firmware spec
 *              for the mapping of each effect code.
 *
 *   Power-on:
 *     - Sky blue dimming (0.5 s up / 0.5 s down) until the first
 *       byte from the host arrives, then LEDs off.
 *
 *   Device → Host:
 *     - On 0x00 : 1-byte DEVICE_ID (0x10 ~ 0x13, see config.h)
 *
 * Notes:
 *   - All LED-driving and effect state management is contained in
 *     neo_effect.cpp. This file should remain simple and only
 *     handle:
 *       - Serial initialization
 *       - Command parsing
 *       - Regular stepping of the effect engine
 *
 * Author   : Richard Um (richard.um@feelax.co.kr)
 * Customer : LGE Smart Vision Design Team
 *
 * (C) 2025 Feelax Co., Ltd. All rights reserved.
 *
 * @warning Proprietary and confidential.
 *          Unauthorized use, distribution, or modification is
 *          strictly prohibited without prior written consent
 *          from Feelax Co., Ltd. and the customer.
 * ============================================================
 */

#include <Arduino.h>
#include "usb_noreset.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "config.h"
#include "neo_effect.h"
#include "ble_ota.h"


/**
 * @brief Arduino setup function.
 *
 * Responsibilities:
 *   - Initialize the UART interface used for the command protocol.
 *   - Initialize the NeoPixel effect engine (gamma LUT, strip, etc.).
 *
 * Notes:
 *   - UART_BAUD is defined in config.h.
 *   - neo_effect_init() prepares all internal state and
 *     ensures LEDs start in a known OFF state.
 */
void setup(void)
{
  usb_disable_autoreset();   /* see usb_noreset.h */
  // Host protocol: nothing but the 1-byte ID may appear on the serial port.
  // ESP-IDF logs (BLE stack) also go to the USB port -> switch them off.
  esp_log_level_set("*", ESP_LOG_NONE);
  // Initialize serial interface for communication with host PC.
  // This port is used for:
  //   - Receiving effect commands (0x01~0x18)
  //   - Receiving ID request (0x00) and sending DEVICE_ID back.
  Serial.begin(UART_BAUD);

  // Initialize NeoPixel, gamma LUT, and internal effect state.
  // After this call:
  //   - Effect engine is in FRONT_EFFECT_BOOT_WAIT state:
  //     sky blue dimming (0.5 s up / 0.5 s down) until the host
  //     PC sends its first byte, then all LEDs turn off.
  neo_effect_init();

  // BLE wireless firmware update + BLE host commands: advertises as
  // "LED 0x10 DC27_FRT" only while the USB port is out (see src/ble_ota.h).
  ble_ota_init();

  // Reached setup() end after a BLE update -> keep the new firmware
  // (a crash before this point makes the bootloader roll back).
  esp_ota_mark_app_valid_cancel_rollback();
}

/**
 * @brief Arduino main loop.
 *
 * Responsibilities:
 *   1) Poll UART for incoming commands.
 *      - 0x00 : ID request, reply with DEVICE_ID.
 *      - else : Forward command to effect engine.
 *
 *   2) Call neo_effect_step() every loop iteration to advance
 *      the currently running animation based on millis().
 *
 * Notes:
 *   - No delay() is used here so that:
 *       * UART latency stays low.
 *       * Effects can run at maximum possible frame rate
 *         for smooth animation.
 *   - If you need to add timing constraints later, prefer to
 *     do it inside neo_effect_step() or by using millis()-based
 *     scheduling here instead of delay().
 */
void loop(void)
{
  // 1) Handle all pending UART bytes in the receive buffer.
  //    This non-blocking loop ensures we process bursts of commands
  //    quickly without missing any bytes.
  while (Serial.available() > 0)
  {
    // Read one byte as a command.
    uint8_t cmd = (uint8_t)Serial.read();

    // Any byte from the host means the PC is connected:
    // stop the power-on sky blue dimming (first time only).
    neo_effect_host_seen();

    // 0xF0 0xA5 0x5A : reboot into ROM download mode (firmware update without BOOT button).
    if (usb_bootloader_magic(cmd)) continue;

    // 0x00 : ID request → respond with DEVICE_ID.
    //        neo_effect module does not need to know about this;
    //        it is purely a protocol / identification concern.
    if (cmd == 0x00)
    {
      uint8_t id = neo_effect_get_device_id();
      Serial.write(id);
    }
    else
    {
      // All other commands (0x01~0x18, etc.) are delegated to the
      // effect engine. neo_effect_handle_command() is responsible
      // for:
      //   - Selecting the appropriate effect.
      //   - Configuring parameters (colors, timings, modes).
      //   - Resetting internal state for the new effect.
      neo_effect_handle_command(cmd);
    }
  }

  // 1-b) Same 1-byte commands over BLE (only while USB is out, after the PIN).
  //      The ID answer goes back over BLE. No bootloader magic here.
  uint8_t bcmd;
  while (ble_ota_host_read(&bcmd))
  {
    neo_effect_host_seen();
    if (bcmd == 0x00)
      ble_ota_host_reply(neo_effect_get_device_id());
    else
      neo_effect_handle_command(bcmd);
  }

  // BLE update running: LEDs off once (flash writes + LED effects at the same time are not needed).
  static bool ota_leds_off = false;
  if (ble_ota_busy() && !ota_leds_off)
  {
    neo_effect_host_seen();
    neo_effect_handle_command(0x01);
    ota_leds_off = true;
  }
  else if (!ble_ota_busy())
  {
    ota_leds_off = false;
  }
  ble_ota_poll();

  // 2) Advance the current effect.
  //
  //    neo_effect_step() should:
  //      - Read current time via millis().
  //      - Update internal state machines / envelopes.
  //      - Write new RGB values to each LED.
  //      - Call strip.show() as needed.
  //
  //    This function must be called regularly for smooth motion.
  neo_effect_step();
}
