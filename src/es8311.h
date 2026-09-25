#pragma once
#ifdef AUDIO_ALARM
// Am Geraet verifiziert (Chip-ID 0x83/0x11, Testton hoerbar).
// Minimaler ES8311-Treiber fuer die Pruef-Firmware.
// Register, Init-Reihenfolge und Taktkoeffizienten sind 1:1 uebernommen aus
// dem esp_codec_dev-Treiber, den Waveshare fuer dieses Board mitliefert
// (02_Example/Arduino/08_Audio_Test/src/esp_codec_dev/device/es8311/es8311.c).
// Nachgebaut, weil esp_codec_dev IDF 5.x braucht, das Projekt aber auf
// arduino-esp32 2.0.x (espressif32@6.8.1) laeuft.
#include <Arduino.h>
#include <Wire.h>

#define ES8311_ADDR 0x18

class ES8311 {
 public:
  bool writeReg(uint8_t reg, uint8_t val);
  int  readReg(uint8_t reg);        // <0 = Fehler

  bool probe();
  bool chipIdOk(uint8_t* id1, uint8_t* id2);

  // Codec als I2S-Slave mit externem MCLK (= 256 x Fs) initialisieren.
  bool begin(uint32_t sampleRate);
  void setVolumeReg(uint8_t v);     // Reg 0x32: dB = -95.5 + 0.5*v
  void mute(bool on);
};

#endif  // AUDIO_ALARM
