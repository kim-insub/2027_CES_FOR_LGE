#ifndef BLE_OTA_H_
#define BLE_OTA_H_
/* ============================================================================
 *  ble_ota.h  -  BLE wireless firmware update (OTA) for DC27 boards
 *
 *  - BLE name    : "<FW_TYPE> 0x<DEVICE_ID> <FW_DESC>"   e.g. "LED 0x10 DC27_FRT"
 *                  (same scheme for every DC27 board: RF / SW / PWR ... later)
 *  - BLE is on only while the USB port is out (USB unplugged or its power switched off while
 *    the board still gets 5 V): no USB SOF frames for 2 s -> advertising starts. When USB comes
 *    back, USB has priority: advertising stops, the BLE link is dropped and a running transfer is
 *    aborted (the running firmware stays). BLE host commands only work while USB is out.
 *  - BLE_OTA_PIN (config.h) is needed for a transfer and for HOST commands.
 *    3 wrong PINs -> BLE locked until power cycle.
 *  - HOST: the same 1-byte commands as the USB port (0x00 -> ID, 0x01..0x28 effects), so the
 *    board can be checked right after an update while USB is still out (tools/led_tool.exe).
 *  - The new image goes to the idle OTA slot (app0/app1 of the default partition table).
 *    It is checked (size, SHA-256, ESP image check, firmware tag type/ID) before the board
 *    switches to it. Any failure or a dropped link leaves the running firmware untouched.
 *  - The USB 1-byte host protocol is not touched, and nothing is printed on the serial port.
 *
 *  Firmware tag (inside every .bin, also the INFO characteristic):
 *      "DC27FW|<TYPE>|0x<ID>|<VERSION>|"     e.g. "DC27FW|LED|0x10|1.0|"
 *  The PC tool (tools/led_tool.exe) reads the tag from the .bin file before sending.
 *  The board rejects an image whose TYPE differs, and whose ID differs unless "force" is set.
 *
 *  GATT (service 4c474501-dc27-4f54-a000-000000000001):
 *    INFO ...0002  read      : firmware tag string of the running firmware
 *    CTRL ...0003  write+notify
 *        PC -> board
 *          01 size(u32) sha256(32) flags(u8) pinlen(u8) pin[]   BEGIN   (flags bit0 = force ID)
 *          02                                                    END
 *          03                                                    ABORT
 *          04 pinlen(u8) pin[]                                   AUTH (unlocks HOST)
 *        board -> PC (notify)
 *          81 status                      BEGIN result
 *          82 next_offset(u32)            block (4096 B) written, send the next one
 *          83 status next_offset(u32)     data error, resend from next_offset
 *          84 status                      END result (0 = OK, board reboots ~0.5 s later)
 *          85 status                      AUTH result
 *    DATA ...0004  write without response : offset(u32) payload[]
 *    HOST ...0005  write / write without response + notify
 *        PC -> board : 1-byte host commands (several bytes = several commands), after AUTH
 *        board -> PC : 1-byte answer (DEVICE_ID for 0x00)
 *  All integers little endian. The PC sends one 4096-byte block, then waits for 82.
 * ========================================================================== */
#include <stdint.h>
#include <stdbool.h>

enum {
  OTA_ST_OK          = 0,
  OTA_ST_BAD_PIN     = 1,
  OTA_ST_LOCKED      = 2,
  OTA_ST_TOO_BIG     = 3,
  OTA_ST_BUSY        = 4,
  OTA_ST_BEGIN_FAIL  = 5,
  OTA_ST_BAD_CMD     = 6,
  OTA_ST_OFFSET      = 7,
  OTA_ST_WRITE_FAIL  = 8,
  OTA_ST_NOT_STARTED = 9,
  OTA_ST_SIZE        = 10,
  OTA_ST_SHA         = 11,
  OTA_ST_IMAGE       = 12,
  OTA_ST_NO_TAG      = 13,
  OTA_ST_WRONG_TYPE  = 14,
  OTA_ST_WRONG_ID    = 15,
  OTA_ST_SET_BOOT    = 16,
};

/* Set up BLE and the OTA worker (advertising starts when USB is out). Call once in setup(). */
void ble_ota_init(void);

/* Call every loop(): USB plug check -> BLE on/off, disconnects, reboot after a finished update. */
void ble_ota_poll(void);

/* 1-byte host command received over BLE (HOST characteristic). false = none waiting. */
bool ble_ota_host_read(uint8_t *b);

/* Answer a BLE host command (DEVICE_ID for 0x00). */
void ble_ota_host_reply(uint8_t b);

/* true while a transfer is running (main loop turns the LEDs off). */
bool ble_ota_busy(void);

/* Firmware tag of this build ("DC27FW|LED|0x10|1.0|") */
const char *ble_ota_fw_tag(void);

#endif /* BLE_OTA_H_ */
