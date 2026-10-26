#pragma once
/* ============================================================================
 *  usb_noreset.h  -  stop the PC from resetting the board when it opens the COM port
 *
 *  Problem (ESP32-C6 native USB = USB-Serial-JTAG):
 *    When a PC program opens the COM port it toggles DTR/RTS.  The C6 USB peripheral treats
 *    that as "reset the chip".  The board reboots, the ROM prints "ESP-ROM:esp32c6-..." on the
 *    same port and nothing answers for ~1 s -> the host reads the boot text instead of the
 *    1-byte DEVICE_ID (the old scan tool showed 0x70 = 'p').  A Nano's USB-UART chip does not
 *    behave like this, so the original firmware never needed protection.
 *
 *  Fix (firmware side, works with ANY host program):
 *    Call usb_disable_autoreset() first thing in setup().  It sets the hardware bit
 *    USB_SERIAL_JTAG_USB_UART_CHIP_RST_DIS, so DTR/RTS from the host no longer reset the chip.
 *
 *  Side effect:
 *    PlatformIO / esptool cannot auto-reset the board into the bootloader while this firmware
 *    is running.  To upload:  hold BOOT, tap RESET, release BOOT, then Upload.
 *    For development builds that should keep auto-upload, add  -DUSB_KEEP_AUTORESET  to
 *    build_flags in platformio.ini.
 *
 *  Updating the firmware WITHOUT pressing BOOT (case already assembled):
 *    The host sends the 3 bytes  0xF0 0xA5 0x5A  to the running firmware.  The firmware then
 *    reboots into the ROM download mode (same state as holding BOOT + tapping RESET), the
 *    PlatformIO "Upload" works, and esptool restarts the new firmware afterwards.
 *    (PC tool: "ENTER BOOTLOADER" button.)  Call usb_bootloader_magic(byte) for every received
 *    byte first; if it returns true the byte belongs to the magic sequence - do not process it.
 * ========================================================================== */
#include <Arduino.h>

#if defined(CONFIG_IDF_TARGET_ESP32C6) && defined(ARDUINO_USB_CDC_ON_BOOT) && \
    (ARDUINO_USB_CDC_ON_BOOT == 1) && !defined(USB_KEEP_AUTORESET)
  #include "soc/soc.h"
  #include "soc/usb_serial_jtag_reg.h"
  #include "soc/lp_aon_reg.h"
  #include "esp_system.h"
  static inline void usb_disable_autoreset(void)
  {
    REG_SET_BIT(USB_SERIAL_JTAG_CHIP_RST_REG, USB_SERIAL_JTAG_USB_UART_CHIP_RST_DIS);
  }
  static inline void usb_enter_download_mode(void)
  {
    Serial.flush();
    REG_SET_BIT(LP_AON_SYS_CFG_REG, LP_AON_FORCE_DOWNLOAD_BOOT);   /* ROM stays in download mode */
    esp_restart();
  }
#else
  static inline void usb_disable_autoreset(void) {}
  static inline void usb_enter_download_mode(void) {}
#endif

/* returns true when `b` was swallowed as part of the 0xF0 0xA5 0x5A sequence */
static inline bool usb_bootloader_magic(uint8_t b)
{
  static const uint8_t M[3] = { 0xF0, 0xA5, 0x5A };
  static uint8_t i = 0;
  if (b == M[i]) {
    if (++i == 3) { i = 0; usb_enter_download_mode(); }
    return true;
  }
  i = (b == M[0]) ? 1 : 0;
  return (i == 1);
}
