#pragma once
//
// Sensirion SHTC3 - der auf dem Waveshare ESP32-S3-ePaper-1.54 verbaute
// Temperatur-/Feuchtesensor (I2C-Adresse 0x70, gemeinsamer Bus mit RTC und
// Audio-Codec). Mess- und CRC-Sequenz sind am Geraet verifiziert.
//
// ACHTUNG Eigenerwaermung: Der Sensor sitzt neben dem ESP32 auf derselben
// Platine und liest dadurch deutlich zu hoch (gemessen ~29 C bei klar
// kuehlerem Raum). Er taugt als Geraete-, nicht als Raumtemperatur. Ueber
// SHTC3_TEMP_OFFSET laesst sich das korrigieren - absichtlich mit Default 0,
// damit nichts still geschoent wird.
//
#ifdef INTERNAL_SHTC3

#include <Arduino.h>

#ifndef SHTC3_SDA
#define SHTC3_SDA 47
#endif
#ifndef SHTC3_SCL
#define SHTC3_SCL 48
#endif
#ifndef SHTC3_ADDR
#define SHTC3_ADDR 0x70
#endif
#ifndef SHTC3_TEMP_OFFSET
#define SHTC3_TEMP_OFFSET 0.0f
#endif

class Shtc3 {
 public:
  // I2C starten und Chip-ID pruefen. false = Sensor nicht gefunden.
  bool begin();

  // Eine Messung ausfuehren. false = kein ACK oder CRC-Fehler; die zuletzt
  // gueltigen Werte bleiben in dem Fall erhalten.
  bool read();

  bool  ok()        const { return _ok; }
  float temp()      const { return _temp + SHTC3_TEMP_OFFSET; }
  float tempRaw()   const { return _temp; }
  float hum()       const { return _hum; }
  bool  corrected() const { return SHTC3_TEMP_OFFSET != 0.0f; }

 private:
  bool cmd(uint16_t c);
  static uint8_t crc8(uint8_t msb, uint8_t lsb);

  bool  _ok   = false;    // mindestens eine gueltige Messung vorhanden
  float _temp = 0.0f;
  float _hum  = 0.0f;
};

#endif  // INTERNAL_SHTC3
