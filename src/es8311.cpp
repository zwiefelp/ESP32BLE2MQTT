#ifdef AUDIO_ALARM
#include "es8311.h"

bool ES8311::writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}

int ES8311::readReg(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

bool ES8311::probe() {
  Wire.beginTransmission(ES8311_ADDR);
  return Wire.endTransmission() == 0;
}

bool ES8311::chipIdOk(uint8_t* id1, uint8_t* id2) {
  int a = readReg(0xFD), b = readReg(0xFE);
  if (a < 0 || b < 0) return false;
  *id1 = a; *id2 = b;
  return a == 0x83 && b == 0x11;
}

bool ES8311::begin(uint32_t sampleRate) {
  // Taktkoeffizienten fuer MCLK = 256 x Fs (Zeile {4096000, 16000, ...} der
  // Herstellertabelle; die Werte sind fuer alle 256x-Raten identisch).
  const uint8_t pre_div = 1, pre_multi = 1, adc_div = 1, dac_div = 1;
  const uint8_t fs_mode = 0, lrck_h = 0x00, lrck_l = 0xFF, bclk_div = 4;
  const uint8_t adc_osr = 0x10, dac_osr = 0x20;
  (void)sampleRate;

  bool ok = true;
  // Stoerfestigkeit des I2C; der erste Schreibzugriff schlaegt laut Hersteller
  // gelegentlich fehl, deshalb doppelt.
  ok &= writeReg(0x44, 0x08);
  writeReg(0x44, 0x08);

  ok &= writeReg(0x01, 0x30);
  ok &= writeReg(0x02, 0x00);
  ok &= writeReg(0x03, 0x10);
  ok &= writeReg(0x16, 0x24);
  ok &= writeReg(0x04, 0x10);
  ok &= writeReg(0x05, 0x00);
  ok &= writeReg(0x0B, 0x00);
  ok &= writeReg(0x0C, 0x00);
  ok &= writeReg(0x10, 0x1F);
  ok &= writeReg(0x11, 0x7F);
  ok &= writeReg(0x00, 0x80);          // Slave-Modus (ESP32 ist I2S-Master)

  int regv = readReg(0x00);
  ok &= writeReg(0x00, (uint8_t)(regv & 0xBF));   // Slave bestaetigen
  ok &= writeReg(0x01, 0x3F & 0x7F);              // externer MCLK, nicht invertiert

  // --- Taktteiler ---
  regv = readReg(0x02) & 0x07;
  regv |= (pre_div - 1) << 5;
  regv |= 0 << 3;                                  // pre_multi = 1
  ok &= writeReg(0x02, (uint8_t)regv);
  ok &= writeReg(0x05, (uint8_t)(((adc_div - 1) << 4) | (dac_div - 1)));
  regv = (readReg(0x03) & 0x80) | (fs_mode << 6) | adc_osr;
  ok &= writeReg(0x03, (uint8_t)regv);
  regv = (readReg(0x04) & 0x80) | dac_osr;
  ok &= writeReg(0x04, (uint8_t)regv);
  regv = (readReg(0x07) & 0xC0) | lrck_h;
  ok &= writeReg(0x07, (uint8_t)regv);
  ok &= writeReg(0x08, lrck_l);
  regv = (readReg(0x06) & 0xE0) | (bclk_div - 1);
  ok &= writeReg(0x06, (uint8_t)regv);

  // --- Format: I2S, 16 Bit ---
  int dac_if = readReg(0x09), adc_if = readReg(0x0A);
  dac_if &= 0xFC; adc_if &= 0xFC;                  // I2S normal
  dac_if |= 0x0C; adc_if |= 0x0C;                  // 16 Bit
  ok &= writeReg(0x09, (uint8_t)dac_if);
  ok &= writeReg(0x0A, (uint8_t)adc_if);

  // --- Start: DAC-Pfad aktivieren ---
  dac_if = readReg(0x09) & 0xBF;                   // Bit6 = 0 -> DAC laeuft
  adc_if = readReg(0x0A) | 0x40;                   // ADC bleibt aus
  ok &= writeReg(0x09, (uint8_t)dac_if);
  ok &= writeReg(0x0A, (uint8_t)adc_if);

  ok &= writeReg(0x17, 0xBF);
  ok &= writeReg(0x0E, 0x02);
  ok &= writeReg(0x12, 0x00);                      // DAC einschalten
  ok &= writeReg(0x14, 0x1A);
  ok &= writeReg(0x0D, 0x01);                      // System power up
  ok &= writeReg(0x15, 0x40);
  ok &= writeReg(0x37, 0x08);
  ok &= writeReg(0x45, 0x00);
  ok &= writeReg(0x31, 0x00);                      // nicht stummgeschaltet
  return ok;
}

void ES8311::setVolumeReg(uint8_t v) { writeReg(0x32, v); }
void ES8311::mute(bool on)           { writeReg(0x31, on ? 0x60 : 0x00); }

#endif  // AUDIO_ALARM
