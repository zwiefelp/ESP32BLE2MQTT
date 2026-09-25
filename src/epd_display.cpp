#ifdef EPAPER

#include "epd_display.h"

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

EpdDisplay::EpdDisplay()
    : _epd(EpdPanelDriver(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)),
      _font(nullptr), _textsize(1), _ascent(0), _lineHeight(8),
      _x(0), _y(0), _ink(GxEPD_BLACK), _paper(GxEPD_WHITE),
      _hash(FNV_OFFSET), _lastHash(0), _refreshCount(EPD_FULL_REFRESH_EVERY) {}

void EpdDisplay::init() {
#if EPD_PWR_PIN >= 0
  // Ohne Spannungsfreigabe bleibt das Panel stumm: GxEPD2 sendet zwar, der
  // BUSY-Pin meldet aber sofort "fertig" und das Bild bleibt leer.
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, EPD_PWR_ON_LEVEL);
  delay(20);
#endif
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  // reset_duration=2 statt 10: empfohlen fuer die "clever reset"-Schaltung
  // neuerer Waveshare-Boards. SPI explizit uebergeben, statt auf die
  // Default-Instanz zu vertrauen.
  _epd.init(EPD_DIAG_BAUD, true, 2, false, SPI,
            SPISettings(EPD_SPI_HZ, MSBFIRST, SPI_MODE0));
  _epd.setRotation(EPD_ROTATION);
  _epd.setFullWindow();
  _epd.setTextWrap(false);   // Umbruch macht write() selbst
  _epd.fillScreen(GxEPD_WHITE);
  _applyFont();
}

void EpdDisplay::setRotation(uint8_t r) {
  (void)r;  // fest ueber EPD_ROTATION, sonst kippt das Layout je nach Board
}

int16_t EpdDisplay::width()  { return _epd.width(); }
int16_t EpdDisplay::height() { return _epd.height(); }

void EpdDisplay::fillScreen(uint16_t color) {
  _epd.fillScreen(_map(color));
  _mix(0x46494C4Cu);
  _mix(_map(color));
  _x = 0;
  _y = 0;
}

void EpdDisplay::drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap,
                            int16_t w, int16_t h, uint16_t color) {
  _epd.drawBitmap(x, y, bitmap, w, h, _map(color));
  _mix(0x424D5000u);
  _mix((uint16_t)x << 16 | (uint16_t)y);
  _mix((uint32_t)(uintptr_t)bitmap);
  _mix(_map(color));
}

void EpdDisplay::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
  _epd.drawFastHLine(x, y, w, _map(color));
  _mix(0x484C4E00u);
  _mix((uint16_t)x << 16 | (uint16_t)y);
  _mix((uint16_t)w);
  _mix(_map(color));
}

void EpdDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  _epd.fillRect(x, y, w, h, _map(color));
  _mix(0x52454354u);
  _mix(((uint32_t)(uint16_t)x << 16) | (uint16_t)y);
  _mix(((uint32_t)(uint16_t)w << 16) | (uint16_t)h);
  _mix(_map(color));
}

void EpdDisplay::setTextColor(uint16_t fg) {
  _ink = _map(fg);
  _paper = _ink;               // wie TFT_eSPI: einarmig = transparenter Hintergrund
  _epd.setTextColor(_ink);
}

void EpdDisplay::setTextColor(uint16_t fg, uint16_t bg) {
  _ink = _map(fg);
  _paper = _map(bg);
  _epd.setTextColor(_ink, _paper);
}

void EpdDisplay::setTextSize(uint8_t s) {
  _textsize = s ? s : 1;
  _applyFont();
}

void EpdDisplay::setTextFont(uint8_t f) {
  // TFT_eSPI-Fontnummern auf GFX-Free-Fonts abbilden. Die Groessen sind auf
  // 200x200 abgestimmt und damit kleiner als die TFT-Originale.
  switch (f) {
    case 0:
    case 1:  _font = nullptr;              break;  // GLCD 5x7, ~6px/Zeichen
    case 2:  _font = &FreeSans9pt7b;       break;  // Fliesstext, ~10px/Zeichen
    case 4:  _font = &FreeSansBold12pt7b;  break;  // Einheiten, Nachkomma
    case 6:
    case 7:
    case 8:  _font = &FreeSansBold24pt7b;  break;  // grosse Messwerte
    default: _font = &FreeSans9pt7b;       break;
  }
  _applyFont();
}

void EpdDisplay::_applyFont() {
  _epd.setFont(_font);
  _epd.setTextSize(_textsize);
  // Ascent aus den Textgrenzen bestimmen: Adafruit_GFX zeichnet ab der
  // Grundlinie, main.cpp rechnet aber mit der Oberkante (TFT_eSPI-Semantik).
  int16_t x1, y1;
  uint16_t w, h;
  _epd.getTextBounds("Ag", 0, 0, &x1, &y1, &w, &h);
  _ascent = -y1;
  _lineHeight = _font ? (int16_t)(pgm_read_byte(&_font->yAdvance) * _textsize)
                      : (int16_t)(8 * _textsize);
}

uint16_t EpdDisplay::_charAdvance(uint8_t c) const {
  if (!_font) return 6 * _textsize;
  uint8_t first = pgm_read_byte(&_font->first);
  uint8_t last  = pgm_read_byte(&_font->last);
  if (c < first || c > last) return 0;
  const GFXglyph* g = &(((GFXglyph*)pgm_read_ptr(&_font->glyph))[c - first]);
  return pgm_read_byte(&g->xAdvance) * _textsize;
}

void EpdDisplay::setCursor(int16_t x, int16_t y) {
  _x = x;
  _y = y;
}

int16_t EpdDisplay::textWidth(const char* s) {
  int16_t w = 0;
  for (; *s; s++) w += _charAdvance((uint8_t)*s);
  return w;
}

size_t EpdDisplay::write(uint8_t c) {
  if (c == '\r') return 1;
  if (c == '\n') {
    _x = 0;
    _y += _lineHeight;
    return 1;
  }
  int16_t adv = (int16_t)_charAdvance(c);
  if (_x + adv > width()) {      // weicher Umbruch wie bei TFT_eSPI
    _x = 0;
    _y += _lineHeight;
  }
  _epd.setCursor(_x, _y + _ascent);
  _epd.write(c);
  _x = _epd.getCursorX();
  _mix(c);
  _mix(((uint32_t)(uint16_t)_x << 16) | (uint16_t)_y);
  _mix(_ink);
  return 1;
}

void EpdDisplay::flush() {
  uint32_t h = _hash;
  _hash = FNV_OFFSET;            // naechster Frame faengt wieder bei null an
  if (h == _lastHash) return;    // nichts geaendert -> Panel in Ruhe lassen
  _lastHash = h;

  if (_refreshCount >= EPD_FULL_REFRESH_EVERY) {
    _epd.display(false);         // Voll-Refresh raeumt Ghosting weg
    _refreshCount = 0;
  } else {
    _epd.display(true);          // Teil-Refresh, deutlich schneller
    _refreshCount++;
  }
}

#endif  // EPAPER
