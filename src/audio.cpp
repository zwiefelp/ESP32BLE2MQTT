#ifdef AUDIO_ALARM

#include "audio.h"
#include "es8311.h"
#include <driver/i2s.h>
#include <math.h>

static ES8311 codec;
static bool   ready = false;

bool audioReady() { return ready; }

static bool i2sBegin() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = AUDIO_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;   // der ES8311 erwartet 256 x Fs
  cfg.bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT;

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = AUDIO_I2S_MCLK;
  pins.bck_io_num   = AUDIO_I2S_BCLK;
  pins.ws_io_num    = AUDIO_I2S_WS;
  pins.data_out_num = AUDIO_I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;

  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL) != ESP_OK) return false;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) return false;
  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

bool audioBegin() {
  uint8_t id1 = 0, id2 = 0;
  if (!codec.probe()) {
    Serial.println("  Audio: ES8311 antwortet nicht auf 0x18");
    return false;
  }
  if (!codec.chipIdOk(&id1, &id2)) {
    Serial.printf("  Audio: unerwartete Chip-ID 0x%02X/0x%02X\n", id1, id2);
    return false;
  }
  if (!codec.begin(AUDIO_SAMPLE_RATE)) {
    Serial.println("  Audio: Codec-Init fehlgeschlagen");
    return false;
  }
  if (!i2sBegin()) {
    Serial.println("  Audio: I2S-Init fehlgeschlagen");
    return false;
  }
  codec.setVolumeReg(AUDIO_VOLUME);
  pinMode(AUDIO_PA_PIN, OUTPUT);
  digitalWrite(AUDIO_PA_PIN, LOW);      // Endstufe erst beim Abspielen zu
  ready = true;
  Serial.println("  Audio: ES8311 bereit");
  return true;
}

// Sinus mit kurzem Ein-/Ausblenden, sonst knackt es am Lautsprecher.
static void playTone(float hz, uint32_t ms, float amp) {
  const int N = 256;
  static int16_t buf[N * 2];
  uint32_t total = (uint32_t)((uint64_t)AUDIO_SAMPLE_RATE * ms / 1000);
  uint32_t done = 0, fade = AUDIO_SAMPLE_RATE / 100;   // 10 ms
  double phase = 0, step = 2.0 * M_PI * hz / AUDIO_SAMPLE_RATE;
  while (done < total) {
    int n = (int)min((uint32_t)N, total - done);
    for (int i = 0; i < n; i++) {
      uint32_t pos = done + i;
      float env = 1.0f;
      if (pos < fade)                 env = (float)pos / fade;
      else if (pos > total - fade)    env = (float)(total - pos) / fade;
      int16_t v = (int16_t)(sin(phase) * 32767.0f * amp * env);
      phase += step;
      buf[i * 2] = v; buf[i * 2 + 1] = v;
    }
    size_t wrote = 0;
    i2s_write(I2S_NUM_0, buf, n * 2 * sizeof(int16_t), &wrote, portMAX_DELAY);
    done += n;
  }
}

static void paOn()  { digitalWrite(AUDIO_PA_PIN, HIGH); delay(5); }
static void paOff() { i2s_zero_dma_buffer(I2S_NUM_0); delay(5); digitalWrite(AUDIO_PA_PIN, LOW); }

void audioStartupBeep() {
  if (!ready) return;
  paOn();
  playTone(880, 90, 0.35f);
  delay(40);
  playTone(1320, 120, 0.35f);
  paOff();
}

void audioAlarm(volatile bool* abort) {
  if (!ready) return;
  paOn();
  for (int i = 0; i < AUDIO_ALARM_REPEATS; i++) {
    if (abort && *abort) break;          // Tastendruck quittiert sofort
    playTone(1760, 160, 0.55f);
    delay(70);
    if (abort && *abort) break;
    playTone(1320, 160, 0.55f);
    delay(220);
  }
  paOff();
}

#endif  // AUDIO_ALARM
