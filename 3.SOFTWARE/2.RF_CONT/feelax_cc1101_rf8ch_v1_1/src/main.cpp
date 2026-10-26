/*
 * FEELAX RF8CH v1.3 - ESP32-C6 SuperMini + CC1101 + EV1527
 * 433.92 MHz ASK/OOK, 8 channel 3.3V outputs.
 *
 * Output mode (saved in NVS, survives reboot):
 *   MOMENTARY : output is HIGH while the remote key is held, LOW after release.
 *   TOGGLE    : every key press flips the output ON/OFF.
 *
 * PAIR switch (GPIO0 -> GND):
 *   short press (< 1 s)   : change output mode            -> LED 2 blinks
 *   press 1 s .. 5 s      : ignored (avoids accidents)
 *   hold >= 5 s           : start pairing (learn 8 codes)   -> LED blinks 0.5 s
 *   short press in pairing: cancel pairing, old codes kept
 *   pairing complete      : LED ON for 1 s, then OFF
 *
 * Status: onboard WS2812 RGB LED (GPIO8). NO external status LED.
 *   Idle: PINK steady = MOMENTARY mode    GREEN steady = TOGGLE mode
 *   RED blink 0.5 s = pairing mode        RED 2 blinks = output mode changed
 *   RED 1 s = pairing complete            RED steady = CC1101 not found
 *   While holding PAIR: BLUE (<1 s, release = mode change) -> YELLOW (1..5 s, release = ignored)
 *                       -> at 5 s pairing starts (RED blink)
 * PlatformIO: Arduino / pioarduino, USB CDC 115200.
 */
#include <Arduino.h>
#include "usb_noreset.h"
#include <Preferences.h>
#include <SPI.h>
#include <RCSwitch.h>
#ifndef RCSWITCH_PATCHED
#error "Use the patched rc-switch in lib/rc-switch (decodes every frame). Do not use the stock library."
#endif
#include <SmartRC_CC1101.h>
#include <Adafruit_NeoPixel.h>
#include <cstring>


/* ---------------------------------------------------------------------------
 * Host protocol (PC program <-> board, UART 115200 8N1 over native USB CDC)
 *   Host -> Board : 0x00            -> board replies 1 byte DEVICE_ID
 *   Board -> Host : on key RELEASE  -> 1 byte: key index 0..7 (CH1..CH8),
 *                                      index | 0x80 when the key was held >= LONG_PRESS_THRESHOLD_MS
 * HOST_PROTOCOL=1 : only these bytes go out on Serial (all text logs are off) - use for the PC tool.
 * HOST_PROTOCOL=0 : text logs on (development / pairing debug); protocol bytes are still active.
 * ------------------------------------------------------------------------- */
#ifndef HOST_PROTOCOL
#define HOST_PROTOCOL 0
#endif
#ifndef DEVICE_ID
#define DEVICE_ID 0x30 // same default as kiosk_rfkey; change with -DDEVICE_ID=0x..
#endif
#define LONG_PRESS_THRESHOLD_MS 600UL
#if HOST_PROTOCOL
#define LOGLN(...) ((void)0)
#define LOGF(...) ((void)0)
#else
#define LOGLN(...) Serial.println(__VA_ARGS__)
#define LOGF(...) Serial.printf(__VA_ARGS__)
#endif

void handleHost(); // defined below (also used by the CC1101 error loop in setup)

namespace Cfg {
constexpr uint8_t SCK = 18;
constexpr uint8_t MISO_PIN = 19;
constexpr uint8_t MOSI_PIN = 20;
constexpr uint8_t CSN = 15;   // Right header pin. GPIO15 = strapping + onboard LED: CSN idles HIGH so the LED may stay lit.
constexpr uint8_t GDO0 = 14;
constexpr uint8_t PAIR_SW = 0;  // NO switch -> GPIO0 to GND (INPUT_PULLUP).
constexpr uint8_t RGB_LED = 8; // Integrated WS2812 (on typical C6 SuperMini).
constexpr uint8_t OUTPUTS[8] = {17, 1, 2, 3, 4, 5, 6, 7}; // CH1 moved to GPIO17 (GPIO0 is the PAIR switch).
constexpr uint8_t NCH = 8;
constexpr uint32_t RF_GAP_MS = 130; // 'released' after no *decoded* frame (frames arrive every ~55 ms; 130 ms rides out one lost frame)
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t SHORT_PRESS_MAX_MS = 1000; // shorter than this = short press
constexpr uint32_t LONG_PRESS_MS = 5000;      // hold this long = start pairing
constexpr uint32_t PAIR_IDLE_TIMEOUT_MS = 30000;
constexpr uint32_t PAIR_BLINK_MS = 500; // half period while pairing
constexpr uint32_t PAIR_DONE_MS = 1000; // LED ON time after pairing completes
constexpr uint32_t MODE_BLINK_MS = 150; // ON/OFF time of the 2 blinks on mode change
constexpr uint32_t LEARN_ACK_MS = 100;
constexpr uint32_t SERIAL_WAIT_MS = 2000; // wait for USB serial at boot (max)
constexpr uint32_t STAT_MS = 2000;        // periodic status line on Serial (0 = off)
} // namespace Cfg

enum OutMode : uint8_t { MODE_MOMENTARY = 0, MODE_TOGGLE = 1 };

SmartRC_CC1101 radio;
RCSwitch receiver;
Preferences prefs;
Adafruit_NeoPixel statusLed(1, Cfg::RGB_LED, NEO_GRB + NEO_KHZ800);

uint32_t saved[Cfg::NCH] = {};
uint32_t pending[Cfg::NCH] = {};
bool paired = false;
bool learning = false;
uint8_t nextKey = 0;
uint32_t pairActivityMs = 0;

OutMode outMode = MODE_MOMENTARY;
bool outState[Cfg::NCH] = {};
int8_t activeCh = -1; // channel held HIGH in MOMENTARY mode

uint32_t rfFrames = 0;     // all decoded frames (any code), for debugging
uint32_t lastAnyCode = 0;  // last decoded code (any), for debugging
uint32_t lastStatMs = 0;
uint32_t activeRfCode = 0;
uint32_t lastRfFrameMs = 0;
int8_t activeIdx = -1;         // paired key index of the key currently held (-1 = none / not reported)
uint32_t rfPressStartMs = 0;   // when the held key was first decoded

bool rawSwitch = false;
bool stableSwitch = false;
uint32_t switchChangeMs = 0;
uint32_t pressStartMs = 0;
bool longHandled = false;

bool modeBlink = false;
uint32_t modeBlinkStartMs = 0;
bool doneActive = false;
uint32_t doneStartMs = 0;
bool learnAck = false;
uint32_t learnAckStartMs = 0;
int8_t ledCache = -1;

inline bool elapsed(uint32_t now, uint32_t from, uint32_t ms) {
  return static_cast<uint32_t>(now - from) >= ms;
}

const char* modeName(OutMode m) { return m == MODE_TOGGLE ? "TOGGLE" : "MOMENTARY"; }

bool validCodes(const uint32_t* codes) {
  for (uint8_t i = 0; i < Cfg::NCH; ++i) {
    if (codes[i] == 0 || codes[i] > 0xFFFFFFUL) return false;
    for (uint8_t j = 0; j < i; ++j)
      if (codes[i] == codes[j]) return false;
  }
  return true;
}

void outputsOff() {
  for (uint8_t i = 0; i < Cfg::NCH; ++i) {
    digitalWrite(Cfg::OUTPUTS[i], LOW);
    outState[i] = false;
  }
  activeCh = -1;
}

void loadPairing() {
  if (!prefs.begin("rf8ch", true)) return;
  uint32_t tmp[Cfg::NCH] = {};
  if (prefs.getBytesLength("codes") == sizeof(tmp) &&
      prefs.getBytes("codes", tmp, sizeof(tmp)) == sizeof(tmp) &&
      validCodes(tmp)) {
    memcpy(saved, tmp, sizeof(saved));
    paired = true;
    LOGLN("[NVS] 8 keys loaded");
  } else {
    LOGLN("[NVS] No paired remote");
  }
  prefs.end();
}

bool savePairing(const uint32_t* codes) {
  if (!prefs.begin("rf8ch", false)) return false;
  size_t wrote = prefs.putBytes("codes", codes, sizeof(saved));
  prefs.end();
  return wrote == sizeof(saved);
}

void loadMode() {
  if (!prefs.begin("rf8ch", true)) return;
  const uint8_t m = prefs.getUChar("mode", MODE_MOMENTARY);
  prefs.end();
  outMode = (m == MODE_TOGGLE) ? MODE_TOGGLE : MODE_MOMENTARY;
}

bool saveMode() {
  if (!prefs.begin("rf8ch", false)) return false;
  prefs.putUChar("mode", static_cast<uint8_t>(outMode));
  prefs.end();
  return true;
}

void toggleMode(uint32_t now) {
  outMode = (outMode == MODE_TOGGLE) ? MODE_MOMENTARY : MODE_TOGGLE;
  outputsOff(); // never carry an ON state from the other mode
  if (!saveMode()) LOGLN("[MODE] NVS write failed (mode not saved)");
  modeBlink = true;
  modeBlinkStartMs = now;
  LOGF("[MODE] %s\n", modeName(outMode));
}

void stopLearning(const char* why) {
  learning = false;
  nextKey = 0;
  learnAck = false;
  LOGF("[PAIR] cancelled: %s; old codes retained\n", why);
}

void startLearning(uint32_t now) {
  outputsOff();
  memset(pending, 0, sizeof(pending));
  learning = true;
  nextKey = 0;
  pairActivityMs = now;
  activeRfCode = 0;
  activeIdx = -1;
  modeBlink = false;
  doneActive = false;
  learnAck = false;
  LOGLN("[PAIR] mode ON: press remote key for CH1");
}

void finishLearning() {
  if (!validCodes(pending)) {
    stopLearning("invalid key list");
    return;
  }
  if (!savePairing(pending)) {
    stopLearning("NVS write failed");
    return;
  }
  memcpy(saved, pending, sizeof(saved));
  paired = true;
  learning = false;
  nextKey = 0;
  learnAck = false;
  doneActive = true;          // LED: ON for PAIR_DONE_MS, then OFF
  doneStartMs = millis();
  // Keep activeRfCode: the last key may still be held; do not fire CH8 again.
  LOGLN("[PAIR] COMPLETE: 8 keys stored");
}

// PAIR switch (GPIO0, active LOW).
//  release after < SHORT_PRESS_MAX_MS : short press -> cancel pairing / change mode
//  release after 1 s .. 5 s           : ignored
//  held for LONG_PRESS_MS             : start pairing (fires while still held)
void onPairSwitchReleased() {
  if (longHandled) return; // the long press already did its job
  const uint32_t heldMs = static_cast<uint32_t>(switchChangeMs - pressStartMs);
  if (heldMs >= Cfg::SHORT_PRESS_MAX_MS) {
    LOGF("[BTN] %lu ms press ignored (short < %lu ms, pairing >= %lu ms)\n",
                  static_cast<unsigned long>(heldMs),
                  static_cast<unsigned long>(Cfg::SHORT_PRESS_MAX_MS),
                  static_cast<unsigned long>(Cfg::LONG_PRESS_MS));
    return;
  }
  if (learning) stopLearning("user cancelled");
  else toggleMode(switchChangeMs);
}

void checkPairSwitch(uint32_t now) {
  const bool pressed = digitalRead(Cfg::PAIR_SW) == LOW;
  if (pressed != rawSwitch) {
    rawSwitch = pressed;
    switchChangeMs = now;
  }
  if (pressed != stableSwitch && elapsed(now, switchChangeMs, Cfg::DEBOUNCE_MS)) {
    stableSwitch = pressed;
    if (stableSwitch) {
      pressStartMs = switchChangeMs;
      longHandled = false;
      LOGLN("[BTN] pressed (GPIO0 = LOW)");
    } else {
      LOGF("[BTN] released after %lu ms\n",
                    static_cast<unsigned long>(switchChangeMs - pressStartMs));
      onPairSwitchReleased();
    }
  }
  if (stableSwitch && !longHandled && elapsed(now, pressStartMs, Cfg::LONG_PRESS_MS)) {
    longHandled = true;
    LOGLN("[BTN] 5 s reached");
    if (!learning) startLearning(now);
  }
}

void learnKey(uint32_t code, uint32_t now) {
  for (uint8_t i = 0; i < nextKey; ++i)
    if (pending[i] == code) {
      LOGLN("[PAIR] same button again; ignored");
      return;
    }
  pending[nextKey] = code;
  LOGF("[PAIR] CH%u = 0x%06lX\n",
                unsigned(nextKey + 1), static_cast<unsigned long>(code));
  ++nextKey;
  pairActivityMs = now;
  learnAck = true;
  learnAckStartMs = now;
  if (nextKey == Cfg::NCH) finishLearning();
  else LOGF("[PAIR] now press CH%u button\n", unsigned(nextKey + 1));
}

// MOMENTARY: drop the channel that is currently held HIGH.
void releaseActive() {
  if (activeCh < 0) return;
  digitalWrite(Cfg::OUTPUTS[activeCh], LOW);
  outState[activeCh] = false;
  LOGF("[OUT] CH%u LOW\n", unsigned(activeCh + 1));
  activeCh = -1;
}

void applyKey(uint8_t ch) {
  if (outMode == MODE_TOGGLE) {
    outState[ch] = !outState[ch];
    digitalWrite(Cfg::OUTPUTS[ch], outState[ch] ? HIGH : LOW);
    LOGF("[OUT] CH%u %s (toggle)\n", unsigned(ch + 1), outState[ch] ? "ON" : "OFF");
  } else {
    if (activeCh >= 0 && activeCh != ch) releaseActive(); // another key pressed while one is held
    outState[ch] = true;
    digitalWrite(Cfg::OUTPUTS[ch], HIGH);
    activeCh = static_cast<int8_t>(ch);
    LOGF("[OUT] CH%u HIGH (momentary)\n", unsigned(ch + 1));
  }
}

// A key is recognized only on the FIRST decoded frame after RF idle.
void processNewKey(uint32_t code, uint32_t now) {
  if (learning) {
    learnKey(code, now);
    return;
  }
  if (!paired) return;
  for (uint8_t i = 0; i < Cfg::NCH; ++i)
    if (saved[i] == code) {
      activeIdx = static_cast<int8_t>(i);
      rfPressStartMs = now;
      applyKey(i);
      return;
    }
}

void expireRfHold(uint32_t now) {
  if (activeRfCode != 0 && elapsed(now, lastRfFrameMs, Cfg::RF_GAP_MS)) {
    activeRfCode = 0;
    releaseActive(); // key released (no decoded frame for RF_GAP_MS)
    if (activeIdx >= 0) { // host protocol: one byte on release, +0x80 for a long press
      const uint32_t heldMs = static_cast<uint32_t>(lastRfFrameMs - rfPressStartMs);
      uint8_t out = static_cast<uint8_t>(activeIdx);
      if (heldMs >= LONG_PRESS_THRESHOLD_MS) out |= 0x80;
      Serial.write(out);
      LOGF("[HOST] key %d %s (held %lu ms) -> byte 0x%02X\n", activeIdx + 1,
           (out & 0x80) ? "LONG" : "short", static_cast<unsigned long>(heldMs), out);
      activeIdx = -1;
    }
  }
}

void pollRf(uint32_t now) {
  if (!receiver.available()) return;
  const unsigned long value = receiver.getReceivedValue();
  const unsigned int bits = receiver.getReceivedBitlength();
  receiver.resetAvailable();
  if (value == 0 || bits != 24) return; // EV1527 fixed-code only
  ++rfFrames;
  lastAnyCode = static_cast<uint32_t>(value);
  const uint32_t code = static_cast<uint32_t>(value);
  if (code == activeRfCode) {
    lastRfFrameMs = now; // held key: suppress repeats
    return;
  }
  // Ignore non-paired frames in normal mode so noise does not unlock the held key.
  if (!learning) {
    if (!paired) return;
    bool known = false;
    for (uint8_t i = 0; i < Cfg::NCH; ++i)
      if (saved[i] == code) { known = true; break; }
    if (!known) return;
  }
  activeRfCode = code;
  lastRfFrameMs = now;
  processNewKey(code, now);
}

// LED states (RGB): 0 OFF, 1 RED (pairing / mode change / pairing done / CC1101 error),
// 2 GREEN (idle, TOGGLE mode), 3 PINK (idle, MOMENTARY mode). Written only on change.
void showLed(uint8_t state) {
  if (ledCache == static_cast<int8_t>(state)) return;
  ledCache = state;
  uint32_t color = 0;
  if (state == 1) color = statusLed.Color(255, 0, 0);
  if (state == 2) color = statusLed.Color(0, 255, 0);
  if (state == 3) color = statusLed.Color(255, 20, 147); // pink; dimmed by setBrightness()
  if (state == 4) color = statusLed.Color(0, 0, 255);    // blue: button held < 1 s (release = mode change)
  if (state == 5) color = statusLed.Color(255, 160, 0);  // yellow: button held 1..5 s (release = ignored)
  statusLed.setPixelColor(0, color);
  statusLed.show();
}

void updateStatusLed(uint32_t now) {
  const uint8_t idleColor = (outMode == MODE_TOGGLE) ? 2 : 3; // TOGGLE = green, MOMENTARY = pink
  // Live feedback while the PAIR button is held (before the 5 s pairing threshold).
  if (stableSwitch && !longHandled) {
    modeBlink = false;
    showLed(elapsed(now, pressStartMs, Cfg::SHORT_PRESS_MAX_MS) ? 5 : 4);
    return;
  }
  if (modeBlink) { // 2 red blinks: ON, OFF, ON, OFF
    const uint32_t phase = static_cast<uint32_t>(now - modeBlinkStartMs) / Cfg::MODE_BLINK_MS;
    if (phase >= 4) {
      modeBlink = false;
    } else {
      showLed((phase % 2U) == 0U ? 1 : 0);
      return;
    }
  }
  if (learning) { // continuous 0.5 s red blink
    bool on = ((now / Cfg::PAIR_BLINK_MS) % 2U) == 0U;
    if (learnAck) {
      if (elapsed(now, learnAckStartMs, Cfg::LEARN_ACK_MS)) learnAck = false;
      else on = true;
    }
    showLed(on ? 1 : 0);
    return;
  }
  if (doneActive) { // pairing complete: red ON for 1 s, then back to green
    if (elapsed(now, doneStartMs, Cfg::PAIR_DONE_MS)) doneActive = false;
    else {
      showLed(1);
      return;
    }
  }
  showLed(idleColor);
}

void setup() {
#if HOST_PROTOCOL
  usb_disable_autoreset(); // DTR/RTS from the PC program must not reset the board (see usb_noreset.h)
#endif
  // First configure all control pins LOW (boot-level glitches need HW protection).
  for (uint8_t i = 0; i < Cfg::NCH; ++i) {
    digitalWrite(Cfg::OUTPUTS[i], LOW);
    pinMode(Cfg::OUTPUTS[i], OUTPUT);
  }
  pinMode(Cfg::PAIR_SW, INPUT_PULLUP);
  statusLed.begin();
  statusLed.setBrightness(80);
  statusLed.clear();
  statusLed.show();
  ledCache = 0;

  Serial.begin(115200);
  // Give the PC time to open the USB port so the boot log is not lost (continues without a PC).
#if !HOST_PROTOCOL
  const uint32_t t0 = millis();
  while (!Serial && !elapsed(millis(), t0, Cfg::SERIAL_WAIT_MS)) delay(10);
  delay(200);
#endif
  LOGLN("FEELAX RF8CH CC1101 EV1527 v1.3");
  loadPairing();
  loadMode();
  LOGF("[MODE] %s\n", modeName(outMode));
  LOGF("[BTN] GPIO0 idle level = %d (1 = open/not pressed, 0 = pressed or shorted to GND)\n",
                digitalRead(Cfg::PAIR_SW));

  radio.setSpiPin(Cfg::SCK, Cfg::MISO_PIN, Cfg::MOSI_PIN, Cfg::CSN);
  radio.setGDO0(Cfg::GDO0);
  radio.Init();
  if (!radio.getCC1101()) {
    outputsOff();
    showLed(1); // RF error: steady RED (still answers the host DEVICE_ID request)
    uint32_t lastErrMs = millis();
    for (;;) {
      handleHost();
      if (elapsed(millis(), lastErrMs, 1000)) {
        lastErrMs = millis();
        LOGLN("[ERROR] CC1101 SPI not detected; check wiring");
      }
      delay(5);
    }
  }
  radio.setCCMode(false); // asynchronous serial RF data on GDO0
  radio.setModulation(2); // ASK/OOK
  radio.setMHZ(433.92);
  radio.setRxBW(325.0);  // kHz
  radio.setDRate(4.8);   // kbaud
  radio.setSyncMode(0);
  radio.setPktFormat(3); // asynchronous serial
  radio.setCrc(false);
  receiver.enableReceive(Cfg::GDO0);
  radio.SetRx();
  LOGLN("[RF] 433.92 MHz OOK, raw GDO0 / rc-switch");
  LOGLN(paired ? "[READY] keys learned" : "[READY] unpaired: hold PAIR switch 5 s");
}

// Host -> Board: 0x00 = request DEVICE_ID (1 byte). Other bytes are ignored.
void handleHost() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == 0x00) Serial.write(static_cast<uint8_t>(DEVICE_ID));
  }
}

void printStatus(uint32_t now) {
  if (Cfg::STAT_MS == 0 || !elapsed(now, lastStatMs, Cfg::STAT_MS)) return;
  lastStatMs = now;
  LOGF("[STAT] t=%lus btn=%d mode=%s paired=%d learning=%d rfFrames=%lu lastCode=0x%06lX\n",
                static_cast<unsigned long>(now / 1000), digitalRead(Cfg::PAIR_SW), modeName(outMode),
                paired, learning, static_cast<unsigned long>(rfFrames),
                static_cast<unsigned long>(lastAnyCode));
}

void loop() {
  const uint32_t now = millis();
  handleHost();
  checkPairSwitch(now);
  expireRfHold(now); // MUST precede pollRf: a new same-code press must re-trigger.
  pollRf(now);
  if (learning && elapsed(now, pairActivityMs, Cfg::PAIR_IDLE_TIMEOUT_MS))
    stopLearning("30s inactivity");
  updateStatusLed(now);
  printStatus(now);
}
