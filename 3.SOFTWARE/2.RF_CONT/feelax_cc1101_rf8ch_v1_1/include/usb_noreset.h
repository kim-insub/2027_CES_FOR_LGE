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
 * ========================================================================== */
#include <Arduino.h>

#if defined(CONFIG_IDF_TARGET_ESP32C6) && defined(ARDUINO_USB_CDC_ON_BOOT) && \
    (ARDUINO_USB_CDC_ON_BOOT == 1) && !defined(USB_KEEP_AUTORESET)
  #include "soc/soc.h"
  #include "soc/usb_serial_jtag_reg.h"
  static inline void usb_disable_autoreset(void)
  {
    REG_SET_BIT(USB_SERIAL_JTAG_CHIP_RST_REG, USB_SERIAL_JTAG_USB_UART_CHIP_RST_DIS);
  }
#else
  static inline void usb_disable_autoreset(void) {}
#endif
