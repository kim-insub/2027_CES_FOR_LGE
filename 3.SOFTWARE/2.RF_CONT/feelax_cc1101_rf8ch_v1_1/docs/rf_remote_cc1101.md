# RF REMOTE (ESP32-C6 SuperMini + CC1101 + EV1527 8-key 433 MHz) — FINAL SPEC v1.3 (2026-10-23)

Firmware: PlatformIO project `LGE_RF_REMOTE\feelax_cc1101_rf8ch_v1_1` (src/main.cpp v1.3). Spec confirmed by the user on 2026-10-23 ("이 사양으로 최종 확정"). Structure kept from the Arduino Nano version; reception via rc-switch on CC1101 GDO0 (async OOK, 433.92 MHz). Docs in the folder: README_WIRING.md, CONNECTION_MAP.txt (both v1.3).
**Other rooms: the controller-independent parts (PC open procedure, ID rule, tester, symptom table) are in `claude/host_protocol_and_tester.md`.**

**DEVICE_ID = 0x30 for the RF board** (user: RF side = 0x30, the switch side = 0x20). **Single source: main.cpp `#define DEVICE_ID 0x30`.** The `-DDEVICE_ID=...` line was removed from platformio.ini on the user's request (2026-10-23), so the ini no longer overrides it. Do not add it back (an ini flag would silently win over main.cpp).

**Verified on hardware:** CC1101 SPI (VERSION 0x14), PAIR button + LED states, mode toggle, 8-key pairing, momentary (incl. long hold) and toggle outputs, persistence after power cycle, output pins 3.3 V. User judged the reaction speed after the rc-switch patch as good enough. **Host protocol (2026-10-23 17:18): `rfkey_host` flashed on COM12 with `-e rfkey_host`; `rf_remote_tester.exe` scan returned Device ID 0x30, Note OK (no stray bytes, no eFuse needed on this board).** The user then reported the RF work as complete "for now" (일단 현재까지 완료); the 8-key short/long check via the tester was not reported explicitly.

## Wiring
CC1101 module Ebyte E07-M1101D (top view, antenna up: odd pins 1,3,5,7 / even pins 2,4,6,8):
| Module pin | Signal | SuperMini |
|---|---|---|
| 1 | GND | GND |
| 2 | VCC 3.3 V | 3V3 |
| 3 | GDO0 | GPIO14 |
| 4 | CSN | GPIO15 |
| 5 | SCK | GPIO18 |
| 6 | MOSI | GPIO20 |
| 7 | MISO | GPIO19 |
| 8 | GDO2 | not used |

- All CC1101 wires on the right header. PAIR switch: GPIO0 -> GND (INPUT_PULLUP, diagonal pins of a 4-pin tact switch).
- Outputs CH1..CH8 = GPIO17, 1, 2, 3, 4, 5, 6, 7 (3.3 V logic; optional, key data also goes to the PC over UART). GPIO4/5 strapping, GPIO6/7 JTAG: pull-downs/gating for safety loads.
- GPIO15 (CSN) also drives the onboard blue LED (not usable as status LED). Red LED = power. Status LED = onboard WS2812 RGB on GPIO8.

## Behaviour (v1.3, final)
- Output mode (NVS, default MOMENTARY): MOMENTARY = HIGH while key held; TOGGLE = ON/OFF per press. Idle LED: PINK = MOMENTARY, GREEN = TOGGLE; RED steady = CC1101 not found (SPI/wiring problem; the board still answers the host ID request, so the ID check passes but no keys arrive).
- PAIR button GPIO0 while held: < 1 s BLUE (release = change mode, LED 2 red blinks); 1..5 s YELLOW (release = ignored); >= 5 s RED blink 0.5 s = pairing starts; short press during pairing cancels; 30 s idle cancels; completion = RED 1 s then idle colour.
- Pairing order decides the mapping (first key = CH1 ... eighth = CH8); re-pairing replaces all 8.
- **Host protocol** (same as kiosk_rfkey): host 0x00 -> 1 byte DEVICE_ID (0x30); on key RELEASE 1 byte = index 0..7, `| 0x80` if held >= 600 ms. Release = no decoded frame for RF_GAP_MS = 130 ms.
- Speed: `lib/rc-switch` is a patched copy (decode every frame, was every 2nd -> ~55 ms instead of ~115 ms, fixes missed fast taps). Never put `sui77/rc-switch` in lib_deps (build stops with `#error` if the stock lib is used).
- Envs: `esp32-c6-supermini` (default; **text logs** `FEELAX RF8CH CC1101 EV1527 v1.3`, `[BTN] [MODE] [PAIR] [OUT] [HOST] [STAT]`, auto-reset upload — NOT for the PC program), `rfkey_host` (`-DHOST_PROTOCOL=1`, NO text on serial, `usb_disable_autoreset()` -> upload from rfkey_host needs BOOT+RST; this is the env for the PC program), `diag` (CC1101 wiring test). `[platformio] default_envs = esp32-c6-supermini` is required. **The right env must be named explicitly: `run -e rfkey_host -t upload --upload-port COMxx`.**

## PC program procedure (from the user's `lge_rfkey_receiver_for_ces_r3.exe`, Python/tkinter, analysed 2026-10-23)
Open 115200 8N1 (timeout 1.0 s) -> DTR False -> RTS False -> reset buffers -> wait BOOT_WAIT 1.0 s -> `read(10)` pre-existing bytes -> write 0x00 -> `read(4)` -> first byte = DEVICE_ID. Then a reader thread reads 1 byte at a time (timeout 0.5 s): index = b & 0x7F, long = b & 0x80. Window 820x580: Mode radio (Vision Buck / Smart Kiosk zone maps A,B,C / F,R), SCAN DEVICES, status, device rows (COM Port, Device ID, Zone, Last Key, Port state + Connect/Disconnect), Log. **Any stray byte before/after the ID request breaks the ID**: ROM boot text -> 0x70 'p' (eFuse case, see usb_device_id_fix.md); firmware text-log banner -> 0x43 'C' (wrong env flashed). (Full version in `host_protocol_and_tester.md`.)

## Test tool (in `tools/`)
- `rf_remote_tester.exe` — **GUI in the style of that PC program** (Go, no console, no install): local server on 127.0.0.1 + Edge/Chrome app window (default browser fallback); closing the window exits the program. Same open/ID procedure as above. Layout (final, per user request 2026-10-23: NO expected-ID field, NO test-result panel): Port select ("All ports" or one) + SCAN DEVICES; device rows COM Port | Device ID | Note (OK / Stray bytes / No ID response / Open failed) | Last Key (`idx=N, S|L, 0xNN`) | Connected + Connect/Disconnect; Log (copy / save `rf_tester_log_*.txt` next to the exe). Page never scrolls, only the log. Source `tools/rf_remote_tester_src` (main.go, ui.html, gui_windows.go, console_other.go). Tested with a simulated pty board + Playwright; **used on the real board: 17:11 found the wrong-env problem, 17:18 returned ID 0x30 OK.** If the exe is running Windows locks the file, so a re-commit from here fails silently (use a new name; `rf_remote_tester_v2.exe` is an obsolete copy and can be deleted).
- `rf_remote_tester.html` (Chrome/Edge Web Serial, older tool; does NOT mimic the PC program exactly).
- The board must run `rfkey_host` firmware and no other program may hold the COM port.

## Build / flash notes
- platformio.ini needs `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`.
- If `pio` is not on PATH: `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e <env> -t upload --upload-port COMxx`.
- eFuse `DIS_USB_SERIAL_JTAG_ROM_PRINT` is NOT burned on the COM12 board and was **not needed**: with `rfkey_host` the tester showed ID 0x30 and no ROM text. (An earlier note that it was required for COM12 was wrong.) Irreversible, once per board — burn only if a scan's `pre-existing bytes` shows `ESP-ROM:...`. `BURN` must be typed exactly (a `BRUN` typo aborted harmlessly). Needs `bitstring==4.1.4` right before espefuse (see usb_device_id_fix.md).
- VS Code reverts files that are open in an editor tab when changed from outside (platformio.ini, README_WIRING.md were reverted once); edit/paste in VS Code or close the tab. Never save a stale tab over a file changed from outside.

## Lessons from bring-up
- "CC1101 SPI not detected" / VERSION 0x00 / steady RED LED = intermittent contact on the 1.27 mm module pins (happened twice, 2nd time after the final spec; pin contact confirmed as the cause); re-seat, then solder / use a fixed connector for the final product.
- Boot log is lost when the USB-CDC port re-enumerates; rely on the periodic `[STAT]` line.
- 4-pin tact switch must be wired on diagonal pins.
- **Tester log 2026-10-23 17:11 on COM12**: pre-existing bytes `FEELAX RF8` (46 45 45 4C 41 58 20 52 46 38), response `CH C` -> ID 0x43, followed by endless `[STAT] t=…s btn=1 mode=MOMENTARY paired=1 …` text every 2 s = the **default text-log env was running, not `rfkey_host`**. (My first guess "ROM boot text / eFuse" was wrong for this case.) Fix: upload with `-e rfkey_host` explicitly -> 17:18 scan = 0x30. Device ID reading as a printable letter = text on the port; read the `pre-existing bytes` log line to tell ROM text (`ESP-ROM`) from firmware text (`FEELAX`/`[STAT]`).

## Open items
- Optional confirmation: 8-key short/long/rapid check through the tester (Last Key column) with `rfkey_host`, then the real PC RF-KEY program accepting ID 0x30.
- Solder / fix the CC1101 wiring for the final unit.
- Optional: move CH5/CH6 off GPIO4/5 (candidate GPIO 22/23) if the GPIO outputs are kept; pin the platform version; delete `rf_remote_tester_v2.exe`.
