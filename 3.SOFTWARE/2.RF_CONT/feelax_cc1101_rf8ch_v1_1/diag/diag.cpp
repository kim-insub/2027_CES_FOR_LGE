// CC1101 wiring diagnostic (bit-banged SPI, no library). Env: diag
#include <Arduino.h>
constexpr uint8_t SCK_P = 18, MISO_P = 19, MOSI_P = 20, CSN_P = 15, GDO0_P = 14;

uint8_t xfer(uint8_t v) {
  uint8_t r = 0;
  for (int i = 7; i >= 0; --i) {
    digitalWrite(MOSI_P, (v >> i) & 1);
    delayMicroseconds(5);
    digitalWrite(SCK_P, HIGH);
    delayMicroseconds(5);
    r = (r << 1) | digitalRead(MISO_P);
    digitalWrite(SCK_P, LOW);
  }
  return r;
}

uint8_t readStatus(uint8_t addr) { // status regs: burst bit + read bit
  digitalWrite(CSN_P, LOW);
  delayMicroseconds(10);
  uint32_t t = millis();
  while (digitalRead(MISO_P) && millis() - t < 50) {}   // wait chip ready (MISO low)
  xfer(addr | 0xC0);
  uint8_t v = xfer(0);
  digitalWrite(CSN_P, HIGH);
  return v;
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  pinMode(SCK_P, OUTPUT); pinMode(MOSI_P, OUTPUT); pinMode(CSN_P, OUTPUT);
  pinMode(MISO_P, INPUT); pinMode(GDO0_P, INPUT);
  digitalWrite(SCK_P, LOW); digitalWrite(CSN_P, HIGH);
  Serial.println("=== CC1101 DIAG ===");
}

// Pin scan: with a pin unconnected, pullup must read 1 and pulldown must read 0.
void pinScan() {
  const uint8_t pins[] = {0, 1, 2, 3, 6, 7, 14, 15, 18, 19, 20};
  Serial.print("PINSCAN (pin:pullup/pulldown) ");
  for (uint8_t p : pins) {
    pinMode(p, INPUT_PULLUP);   delayMicroseconds(300); int u = digitalRead(p);
    pinMode(p, INPUT_PULLDOWN); delayMicroseconds(300); int d = digitalRead(p);
    pinMode(p, INPUT);
    Serial.printf("%u:%d/%d%s ", p, u, d, (u == 1 && d == 0) ? "" : "*");
  }
  Serial.println("  (* = pin is tied/driven by something)");
}

void loop() {
  static int n = 0;
  if (n++ % 3 == 0) {
    pinScan();
    pinMode(SCK_P, OUTPUT); pinMode(MOSI_P, OUTPUT); pinMode(CSN_P, OUTPUT); pinMode(MISO_P, INPUT);
    digitalWrite(CSN_P, HIGH); digitalWrite(SCK_P, LOW);
  }
  // reset sequence per datasheet
  digitalWrite(CSN_P, HIGH); delayMicroseconds(30);
  digitalWrite(CSN_P, LOW);  delayMicroseconds(30);
  digitalWrite(CSN_P, HIGH); delayMicroseconds(60);
  digitalWrite(CSN_P, LOW);
  uint32_t t = millis();
  while (digitalRead(MISO_P) && millis() - t < 50) {}
  bool misoLowAfterCsn = !digitalRead(MISO_P);
  xfer(0x30); // SRES
  t = millis();
  while (digitalRead(MISO_P) && millis() - t < 50) {}
  digitalWrite(CSN_P, HIGH);
  delay(5);

  // MISO level with CSN high (idle) / pull test
  pinMode(MISO_P, INPUT_PULLUP); delayMicroseconds(200); int idleUp = digitalRead(MISO_P);
  pinMode(MISO_P, INPUT_PULLDOWN); delayMicroseconds(200); int idleDn = digitalRead(MISO_P);
  pinMode(MISO_P, INPUT);

  uint8_t part = readStatus(0x30), ver = readStatus(0x31);
  Serial.printf("MISO low after CSN=%d | idle MISO pullup=%d pulldown=%d | PARTNUM=0x%02X VERSION=0x%02X\n",
                misoLowAfterCsn, idleUp, idleDn, part, ver);
  if (part == 0x00 && (ver == 0x14 || ver == 0x04)) Serial.println(" -> OK: CC1101 found");
  else if (part == 0xFF && ver == 0xFF) Serial.println(" -> all 0xFF: MISO high/floating - no power, CSN or MISO not connected");
  else if (part == 0x00 && ver == 0x00) Serial.println(" -> all 0x00: MISO stuck low - MISO shorted to GND/swapped, or no chip power");
  else Serial.println(" -> unexpected values: noise/loose wire/MOSI-MISO swapped?");
  delay(1000);
}