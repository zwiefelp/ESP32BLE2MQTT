#ifdef INTERNAL_SHTC3

#include "shtc3.h"
#include <Wire.h>

// Kommandos laut Datenblatt
#define SHTC3_WAKEUP   0x3517
#define SHTC3_SLEEP    0xB098
#define SHTC3_READ_ID  0xEFC8
#define SHTC3_MEAS_T   0x7866   // T zuerst, normal, ohne Clock-Stretching

bool Shtc3::cmd(uint16_t c) {
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write(c >> 8);
  Wire.write(c & 0xFF);
  return Wire.endTransmission() == 0;
}

uint8_t Shtc3::crc8(uint8_t msb, uint8_t lsb) {   // Poly 0x31, Init 0xFF
  uint8_t crc = 0xFF, d[2] = {msb, lsb};
  for (int i = 0; i < 2; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
  }
  return crc;
}

// Loest einen haengenden I2C-Bus: haelt ein Slave SDA fest (z. B. weil der
// ESP32 mitten in einem Transfer resettet wurde), bringen ihn neun Taktflanken
// plus ein STOP wieder in den Leerlauf. Ohne das blockiert Wire dauerhaft.
static bool i2cRecover() {
  pinMode(SHTC3_SDA, INPUT_PULLUP);
  pinMode(SHTC3_SCL, INPUT_PULLUP);
  delayMicroseconds(50);
  if (digitalRead(SHTC3_SDA) == HIGH) return true;      // Bus ist frei

  Serial.println("  SHTC3: SDA haengt auf LOW - Bus-Recovery");
  pinMode(SHTC3_SCL, OUTPUT);
  for (int i = 0; i < 9 && digitalRead(SHTC3_SDA) == LOW; i++) {
    digitalWrite(SHTC3_SCL, LOW);  delayMicroseconds(5);
    digitalWrite(SHTC3_SCL, HIGH); delayMicroseconds(5);
  }
  // STOP: SDA steigt waehrend SCL high ist
  pinMode(SHTC3_SDA, OUTPUT);
  digitalWrite(SHTC3_SDA, LOW);  delayMicroseconds(5);
  digitalWrite(SHTC3_SCL, HIGH); delayMicroseconds(5);
  digitalWrite(SHTC3_SDA, HIGH); delayMicroseconds(5);
  pinMode(SHTC3_SDA, INPUT_PULLUP);
  pinMode(SHTC3_SCL, INPUT_PULLUP);
  delayMicroseconds(50);
  return digitalRead(SHTC3_SDA) == HIGH;
}

bool Shtc3::begin() {
  // Falls ein Slave SDA festhaelt, erst befreien - nicht abbrechen, wenn es
  // nicht klappt: SDA kann im Leerlauf LOW lesen und der Bus trotzdem gehen,
  // sobald das I2C-Peripheriegeraet die Leitungen selbst treibt.
  bool freed = i2cRecover();

  Wire.begin(SHTC3_SDA, SHTC3_SCL, 100000);
  Wire.setTimeOut(50);     // ohne Timeout blockiert ein toter Bus die Firmware
  delay(50);

  bool idOk = false;
  uint16_t id = 0;
  if (cmd(SHTC3_WAKEUP)) {
    delay(2);
    if (cmd(SHTC3_READ_ID)) {
      delay(2);
      if (Wire.requestFrom((uint8_t)SHTC3_ADDR, (uint8_t)3) == 3) {
        uint8_t a = Wire.read(), b = Wire.read();
        Wire.read();
        id = (a << 8) | b;
        idOk = (id & 0x083F) == 0x0807;
      }
    }
  }
  cmd(SHTC3_SLEEP);

  if (idOk) {
    Serial.printf("  SHTC3: Chip-ID 0x%04X, Bus ok\n", id);
    return true;
  }

  // Fehlerfall ausfuehrlich melden - hier ist die Ursache meist der Bus,
  // nicht der Sensor.
  Serial.printf("  SHTC3: nicht erkannt (ID 0x%04X). SDA(%d)=%d SCL(%d)=%d%s\n",
                id, SHTC3_SDA, digitalRead(SHTC3_SDA),
                SHTC3_SCL, digitalRead(SHTC3_SCL),
                freed ? "" : ", Bus-Recovery erfolglos");
  const uint8_t known[] = {0x18, 0x51, SHTC3_ADDR};
  const char*   names[] = {"ES8311", "PCF85063", "SHTC3"};
  Serial.print("  SHTC3: Bus:");
  for (int i = 0; i < 3; i++) {
    Wire.beginTransmission(known[i]);
    Serial.printf("  0x%02X %s=%s", known[i], names[i],
                  Wire.endTransmission() == 0 ? "ja" : "nein");
  }
  Serial.println();
  return false;
}

bool Shtc3::read() {
  if (!cmd(SHTC3_WAKEUP)) return false;
  delay(2);
  if (!cmd(SHTC3_MEAS_T)) return false;
  delay(15);
  if (Wire.requestFrom((uint8_t)SHTC3_ADDR, (uint8_t)6) != 6) return false;

  uint8_t tm = Wire.read(), tl = Wire.read(), tc = Wire.read();
  uint8_t hm = Wire.read(), hl = Wire.read(), hc = Wire.read();
  cmd(SHTC3_SLEEP);

  if (crc8(tm, tl) != tc || crc8(hm, hl) != hc) return false;

  _temp = -45.0f + 175.0f * (((tm << 8) | tl) / 65535.0f);
  _hum  = 100.0f * (((hm << 8) | hl) / 65535.0f);
  _ok   = true;
  return true;
}

#endif  // INTERNAL_SHTC3
