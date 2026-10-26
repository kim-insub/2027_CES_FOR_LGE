# FEELAX CC1101 / EV1527 RF 8CH Controller V1.3 (final spec, 2026-10-23)

ESP32-C6 SuperMini + CC1101 (Ebyte E07-M1101D) + EV1527 433.92 MHz 8-key remote.
Remote keys -> (a) 1 byte to the PC over USB serial (host protocol), (b) 8 optional 3.3 V GPIO outputs.

## Wiring
| E07-M1101D pin | Signal | ESP32-C6 SuperMini | Note |
|---|---|---|---|
| 1 | GND | GND | |
| 2 | VCC | 3V3 | **3.3 V only**, never 5 V. Add 100 nF + 10 uF at the module |
| 3 | GDO0 | GPIO14 | raw ASK/OOK data |
| 4 | CSN | GPIO15 | right header; also drives the onboard blue LED |
| 5 | SCK | GPIO18 | |
| 6 | MOSI | GPIO20 | |
| 7 | MISO | GPIO19 | |
| 8 | GDO2 | - | not used |

Module top view (chip side, antenna up): odd pins 1,3,5,7 in one row, even pins 2,4,6,8 in the other. Use soldered / firmly seated wires: the 1.27 mm pins gave intermittent contact.

- PAIR switch: GPIO0 --- tact switch (use **diagonal** pins) --- GND (internal pull-up).
- Status LED: onboard WS2812 RGB on GPIO8 (no external LED).
- Keep GPIO9 (BOOT), GPIO12/13 (USB) unconnected.

## 8 channel outputs (3.3 V logic, optional)
| CH | GPIO | | CH | GPIO |
|---|---|---|---|---|
| 1 | GPIO17 | | 5 | GPIO4 (strapping) |
| 2 | GPIO1 | | 6 | GPIO5 (strapping) |
| 3 | GPIO2 | | 7 | GPIO6 (JTAG) |
| 4 | GPIO3 | | 8 | GPIO7 (JTAG) |

MOMENTARY mode: HIGH while the remote key is held. TOGGLE mode: every press flips ON/OFF.
3.3 V logic only - use drivers for loads; GPIOs are not glitch-free during reset.

## Output mode, PAIR button and LED
Output mode is saved in NVS (default MOMENTARY). Idle LED: **PINK = MOMENTARY, GREEN = TOGGLE**, RED steady = CC1101 not found.

While the PAIR button (GPIO0) is held:
| Held | LED | Release does |
|---|---|---|
| < 1 s | BLUE | change mode (LED 2 red blinks) |
| 1 .. 5 s | YELLOW | nothing (ignored) |
| 5 s reached | RED blink 0.5 s | pairing starts (while still held) |
| short press during pairing | BLUE | cancel pairing, old keys kept |

Pairing: press remote keys in order CH1 -> CH8 (8 distinct codes); after the 8th the keys are stored in NVS and the LED is RED for 1 s, then idle colour. 30 s without a key cancels (old keys kept).

## Host protocol (same as kiosk_rfkey)
UART 115200 8N1 over native USB CDC.
- Host sends `0x00` -> board replies 1 byte DEVICE_ID (default `0x30`, `-DDEVICE_ID=0x..`).
- On key **release** the board sends 1 byte: key index 0..7 (CH1..CH8); `| 0x80` if held >= 600 ms.
- Release = no decoded RF frame for RF_GAP_MS (130 ms; EV1527 sends no release frame).

## Build environments
| env | use |
|---|---|
| `esp32-c6-supermini` (default) | development: text logs `[BTN] [MODE] [PAIR] [OUT] [HOST] [STAT]`, auto-reset upload |
| `rfkey_host` | PC host mode: **no text on serial**, only protocol bytes, `usb_disable_autoreset()`; upload with BOOT + RST |
| `diag` | CC1101 wiring test (PARTNUM / VERSION 0x14, pin scan) |

`rc-switch` is a **patched copy in `lib/rc-switch`** (decodes every frame instead of every second one) - do not add it to `lib_deps`; the build stops with `#error` if the stock library is used.
One-time per board: eFuse `DIS_USB_SERIAL_JTAG_ROM_PRINT` (`tools\burn_silent_boot.bat COMx`).

## Notes
- EV1527 is a one-way fixed code: no real "connected" state. A very short tap may not contain enough frames to decode.
- Do not connect relay coils / 5 V loads directly to the GPIOs.
- Verified on hardware: CC1101 SPI, pairing, both output modes, key persistence. Host protocol (`rfkey_host`) is simulated and compiled; confirm with the PC RF-KEY tool.
