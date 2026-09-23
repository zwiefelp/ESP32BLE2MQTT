#pragma once
//
// TFT_eSPI-kompatibler Display-Wrapper fuer 1.54"-e-Paper (SSD1681 / "V2",
// 200x200, GDEH0154D67). Damit bleibt der Zeichencode in main.cpp fuer alle
// Boards derselbe - nur die Layout-Konstanten unterscheiden sich.
//
// Unterschiede zum TFT, die der Wrapper kapselt:
//  - Monochrom: TFT_BLACK = Papier (weiss), jede andere Farbe = Tinte (schwarz)
//  - Kein sofortiges Zeichnen: alles geht in den Framebuffer, flush() schiebt
//    ihn aufs Panel - und nur dann, wenn sich der Inhalt geaendert hat
//  - Adafruit_GFX setzt den Cursor auf die Grundlinie, TFT_eSPI auf die
//    Oberkante; der Wrapper rechnet den Ascent dazu
//
#ifdef EPAPER

#include <Arduino.h>
#include <Print.h>
#include <SPI.h>
#include <GxEPD2_BW.h>

// Panelklasse. Das Waveshare 1.54" e-Paper Module V2 ist ein GDEH0154D67
// (200x200, SSD1681). Neuere Panel-Revisionen von Good Display laufen unter
// GDEY0154D67 und brauchen eine leicht andere Init-Sequenz - falls das Bild
// leer bleibt oder stark geistert, -D EPD_PANEL_GDEY0154D67 setzen.
#ifdef EPD_PANEL_GDEY0154D67
#include <gdey/GxEPD2_154_GDEY0154D67.h>
typedef GxEPD2_154_GDEY0154D67 EpdPanelDriver;
#else
#include <epd/GxEPD2_154_D67.h>
typedef GxEPD2_154_D67 EpdPanelDriver;
#endif

// --- Verdrahtung (per build_flags ueberschreibbar) -------------------------
// Die Namen entsprechen der Beschriftung am 8-poligen Stecker des
// Waveshare-Moduls: VCC, GND, DIN, CLK, CS, DC, RST, BUSY.
#ifndef EPD_CS
#define EPD_CS   10
#endif
#ifndef EPD_DC
#define EPD_DC    9
#endif
#ifndef EPD_RST
#define EPD_RST   8
#endif
#ifndef EPD_BUSY
#define EPD_BUSY  7
#endif
#ifndef EPD_SCK
#define EPD_SCK  12
#endif
#ifndef EPD_MOSI
#define EPD_MOSI 11
#endif

// Spannungsfreigabe des Panels (Waveshare ESP32-S3-ePaper-1.54: GPIO 6,
// aktiv LOW). -1, wenn das Panel dauerhaft versorgt wird.
#ifndef EPD_PWR_PIN
#define EPD_PWR_PIN -1
#endif
#ifndef EPD_PWR_ON_LEVEL
#define EPD_PWR_ON_LEVEL 0
#endif

// 0..3 - das Panel ist quadratisch, die Rotation bestimmt nur, wo das
// Flachbandkabel sitzt. setRotation() aus main.cpp wird bewusst ignoriert.
#ifndef EPD_ROTATION
#define EPD_ROTATION 0
#endif

// SPI-Takt. Bei langen Jumperkabeln lieber niedrig halten.
#ifndef EPD_SPI_HZ
#define EPD_SPI_HZ 4000000
#endif

// Nonzero schaltet die GxEPD2-Diagnose auf Serial frei ("Busy Timeout!" usw.).
#ifndef EPD_DIAG_BAUD
#define EPD_DIAG_BAUD 0
#endif

// Nach so vielen Teil-Refreshes wird einmal voll aufgefrischt (gegen Ghosting).
#ifndef EPD_FULL_REFRESH_EVERY
#define EPD_FULL_REFRESH_EVERY 12
#endif

// --- Farbkonstanten aus TFT_eSPI ------------------------------------------
#define TFT_BLACK    0x0000
#define TFT_WHITE    0xFFFF
#define TFT_RED      0xF800
#define TFT_GREEN    0x07E0
#define TFT_BLUE     0x001F
#define TFT_YELLOW   0xFFE0
#define TFT_SKYBLUE  0x867D
#define TFT_ORANGE   0xFDA0
#define TFT_DARKGREY 0x7BEF

typedef GxEPD2_BW<EpdPanelDriver, EpdPanelDriver::HEIGHT> EpdPanel;

class EpdDisplay : public Print {
 public:
  EpdDisplay();

  void init();
  void setRotation(uint8_t r);          // ignoriert, siehe EPD_ROTATION
  int16_t width();
  int16_t height();

  void fillScreen(uint16_t color);
  void drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap,
                  int16_t w, int16_t h, uint16_t color);
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);

  void setTextColor(uint16_t fg);
  void setTextColor(uint16_t fg, uint16_t bg);
  void setTextSize(uint8_t s);
  void setTextFont(uint8_t f);
  void setCursor(int16_t x, int16_t y);
  int16_t getCursorX() const { return _x; }
  int16_t getCursorY() const { return _y; }
  // Oberkante -> Grundlinie der aktuellen Schrift. Damit lassen sich
  // unterschiedlich grosse Textteile auf einer Grundlinie ausrichten.
  int16_t fontAscent() const { return _ascent; }

  int16_t textWidth(const char* s);
  int16_t textWidth(const String& s) { return textWidth(s.c_str()); }

  // Framebuffer aufs Panel schieben. Ist der Inhalt unveraendert, passiert
  // nichts - das spart Refreshes bei zyklischen Neuzeichnungen.
  void flush();

  size_t write(uint8_t c) override;
  using Print::write;

 private:
  static const uint32_t FNV_OFFSET = 2166136261u;

  uint16_t _map(uint16_t color) const {
    return (color == TFT_BLACK) ? GxEPD_WHITE : GxEPD_BLACK;
  }
  void _applyFont();
  uint16_t _charAdvance(uint8_t c) const;
  void _mix(uint32_t v) { _hash ^= v; _hash *= 16777619u; }

  EpdPanel _epd;
  const GFXfont* _font;
  uint8_t  _textsize;
  int16_t  _ascent;
  int16_t  _lineHeight;
  int16_t  _x, _y;
  uint16_t _ink, _paper;
  uint32_t _hash, _lastHash;
  uint16_t _refreshCount;
};

#endif  // EPAPER
