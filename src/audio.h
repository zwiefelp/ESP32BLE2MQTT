#pragma once
//
// Tonausgabe ueber den ES8311-Codec und die NS4150-Endstufe des
// Waveshare ESP32-S3-ePaper-1.54.
//
// Signalweg: ESP32 -> I2S -> ES8311 -> NS4150 -> Lautsprecher. Der Codec muss
// dafuer ueber I2C konfiguriert werden; einen Direktweg zum Lautsprecher gibt
// es nicht. Der I2C-Bus wird mit SHTC3 (0x70) und RTC (0x51) geteilt.
//
#ifdef AUDIO_ALARM

#include <Arduino.h>

#ifndef AUDIO_I2S_MCLK
#define AUDIO_I2S_MCLK 14
#endif
#ifndef AUDIO_I2S_BCLK
#define AUDIO_I2S_BCLK 15
#endif
#ifndef AUDIO_I2S_WS
#define AUDIO_I2S_WS   38
#endif
#ifndef AUDIO_I2S_DOUT
#define AUDIO_I2S_DOUT 45
#endif
#ifndef AUDIO_SAMPLE_RATE
#define AUDIO_SAMPLE_RATE 16000
#endif
// Reg 0x32 des ES8311: dB = -95.5 + 0.5 * Wert
#ifndef AUDIO_VOLUME
#define AUDIO_VOLUME 0xC0
#endif

// Codec und I2S starten. Setzt voraus, dass Wire bereits laeuft und die
// Audio-Versorgung (AUDIO_PWR_PIN) eingeschaltet ist.
bool audioBegin();
bool audioReady();

// Kurzer Bestaetigungston beim Start.
void audioStartupBeep();

// Alarmsignal: AUDIO_ALARM_REPEATS Doppeltoene. Zeigt *abort waehrenddessen
// auf true, wird sofort abgebrochen - so quittiert ein Tastendruck sofort.
void audioAlarm(volatile bool* abort);

#endif  // AUDIO_ALARM
