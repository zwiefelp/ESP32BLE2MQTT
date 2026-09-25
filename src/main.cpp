#include <list>
#include <Arduino.h>
#include <BLEDevice.h>
#ifdef EPAPER
#include "epd_display.h"
#else
#include <TFT_eSPI.h>
#endif
#ifdef AUDIO_ALARM
#include "audio.h"
#endif
#ifdef INTERNAL_SHTC3
#include "shtc3.h"
#if !defined(EPAPER)
#error "INTERNAL_SHTC3 ist bislang nur fuer die e-Paper-Variante umgesetzt (Layout fehlt)."
#endif
#endif
#include <WiFiManager.h>
#include <aes/esp_aes.h>
#include <array>

#if BOARD==ESP32S3
  #define S3
#endif 

#define WIFI
#define MQTT
bool DEBUG = false;
String version = "V3.0";

#define CONFIG_ARDUINO_LOOP_STACK_SIZE 16384

#ifdef WIFI
#include <WiFi.h>
#endif

#ifdef MQTT
#include <PubSubClient.h>
#endif
// e-Paper zeichnet in einen Framebuffer und muss explizit aufs Panel
// geschoben werden; beim TFT ist das ein No-Op.
#ifdef EPAPER
#define DISPLAY_FLUSH() display.flush()
#else
#define DISPLAY_FLUSH() ((void)0)
#endif

#if defined(EPAPER)
  // Generisches ESP32-S3-DevKit: Button2 ist der BOOT-Taster, Button1 ein
  // externer Taster gegen GND (interner Pullup). Per build_flags aenderbar.
  #ifndef BUTTON1PIN
  #define BUTTON1PIN 14
  #endif
  #ifndef BUTTON2PIN
  #define BUTTON2PIN 0
  #endif
#elif defined(S3)
#define BUTTON1PIN 14
#define BUTTON2PIN 0
#else
#define BUTTON1PIN 35
#define BUTTON2PIN 0
#endif

#if defined(EPAPER)
// 200x200 ist quadratisch statt quer: die Werte stehen untereinander (nicht
// in zwei Spalten) und dafuer deutlich groesser. Die Y-Werte unten sind die
// Zeilenoberkanten des Layouts, siehe displayScreen()/displayDateTime().
#define SCREEN_WIDTH  200
#define SCREEN_HEIGHT 200
#define MARGIN_X  4     // Abstand vom linken Displayrand
#define IND_ICON_X (SCREEN_WIDTH - 50)
#define IND_MQ_X   (SCREEN_WIDTH - 30)
#define EPD_DOT_STEP 14 // Abstand der Screen-Punkte in der Kopfzeile
#define EPD_Y_RULE   20 // Trennlinie unter der Kopfzeile
#define EPD_Y_FOOT  178 // Trennlinie ueber der Fusszeile
#define EPD_ICON_GAP  6 // Abstand zwischen Messwert und Alarm-Glocke
#define EPD_ICON_DY   9 // Glocke mittig zur Zeile der grossen Ziffern
#elif defined(S3)
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 170
#define COL2_X    160   // x der rechten Wertespalte (Feuchte/RSSI)
#define ROW_STEP  40    // vertikaler Zeilenabstand
#define MARGIN_X  10    // Abstand vom linken Displayrand (nur S3)
#define IND_ICON_X (SCREEN_WIDTH - 50)
#define IND_MQ_X   (SCREEN_WIDTH - 20)
#else
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 135
#define COL2_X    130
#define ROW_STEP  34
#define MARGIN_X  0
#define IND_ICON_X (SCREEN_WIDTH - 50)
#define IND_MQ_X   (SCREEN_WIDTH - 20)
#endif

#define GOVEE_BT_mac_OUI_PREFIX "a4:c1:38"
#define H5075_UPDATE_UUID16  "88:ec"
#define VICTRON_BT_mac_OUI_PREFIX "60:a4:23"
#define SMARTSOLAR_ENCRYPTION_KEY "0x0df4d0395b7d1a876c0c33ecb9e70aea"
#define VICTRON_ENCRYPTED_DATA_MAX_SIZE 16

#define FINE_SCAN true

// WiFi Icon
const unsigned char wifiicon[] PROGMEM  = {
	0b00000000, 0b00000000, //                 
	0b00000111, 0b11100000, //      ######     
	0b00011111, 0b11111000, //    ##########   
	0b00111111, 0b11111100, //   ############  
	0b01110000, 0b00001110, //  ###        ### 
	0b01100111, 0b11100110, //  ##  ######  ## 
	0b00001111, 0b11110000, //     ########    
	0b00011000, 0b00011000, //    ##      ##   
	0b00000011, 0b11000000, //       ####      
	0b00000111, 0b11100000, //      ######     
	0b00000100, 0b00100000, //      #    #     
	0b00000001, 0b10000000, //        ##       
	0b00000001, 0b10000000, //        ##       
	0b00000000, 0b00000000, //                 
	0b00000000, 0b00000000, //                 
	0b00000000, 0b00000000, //                 
};

// Glocke: markiert am Sensor-Screen den Wert, fuer den eine Alarmregel
// konfiguriert ist. Format wie wifiicon (1 bpp, MSB links).
const unsigned char alarmicon[] PROGMEM = {
	0b00000001, 0b10000000, //        ##       
	0b00000010, 0b01000000, //       #  #      
	0b00000100, 0b00100000, //      #    #     
	0b00001000, 0b00010000, //     #      #    
	0b00001000, 0b00010000, //     #      #    
	0b00010000, 0b00001000, //    #        #   
	0b00010000, 0b00001000, //    #        #   
	0b00100000, 0b00000100, //   #          #  
	0b00100000, 0b00000100, //   #          #  
	0b01000000, 0b00000010, //  #            # 
	0b01000000, 0b00000010, //  #            # 
	0b11111111, 0b11111111, // ################
	0b00000000, 0b00000000, //                 
	0b00000011, 0b11000000, //       ####      
	0b00000001, 0b10000000, //        ##       
	0b00000000, 0b00000000, //                 
};

// Gefuellte Glocke: dieselbe Silhouette, aber massiv. Zeigt am Sensor-Screen,
// dass die Regel gerade verletzt ist - nicht nur, dass es eine gibt.
const unsigned char alarmiconfull[] PROGMEM = {
	0b00000001, 0b10000000, //        ##       
	0b00000011, 0b11000000, //       ####      
	0b00000111, 0b11100000, //      ######     
	0b00001111, 0b11110000, //     ########    
	0b00001111, 0b11110000, //     ########    
	0b00011111, 0b11111000, //    ##########   
	0b00011111, 0b11111000, //    ##########   
	0b00111111, 0b11111100, //   ############  
	0b00111111, 0b11111100, //   ############  
	0b01111111, 0b11111110, //  ############## 
	0b01111111, 0b11111110, //  ############## 
	0b11111111, 0b11111111, // ################
	0b00000000, 0b00000000, //                 
	0b00000011, 0b11000000, //       ####      
	0b00000001, 0b10000000, //        ##       
	0b00000000, 0b00000000, //                 
};

#ifdef MQTT
String basetopic = "/openhab/in/";
String conftopic = "/openhab/configuration/";
String getconftopic = "/openhab/configuration";
String cmdtopic = "/openhab/configuration/cmd";
String timetopic = "/openhab/Daytime";
String datetopic = "/openhab/DayDate";
String debugtopic = "/openhab/debug/";

IPAddress broker_int(192,168,20,17);          // Address of the MQTT broker SSID=UPC4E87B2D
IPAddress broker_ext(82,165,176,152);         
IPAddress broker_openhab(192,168,1,1);        // Address of the MQTT broker SSID=OpenHAB
IPAddress broker(0,0,0,0);
String ssid = "";
IPAddress ip(0,0,0,0);

WiFiClient wificlient;
PubSubClient client(wificlient);
#endif

String mqttdate = "Mo,00.00.0000";
String mqtttime = "00:00";

#ifndef ALARM_TOPIC
#define ALARM_TOPIC "/openhab/alarm"
#endif
// Per Config-Zeile "alarmtopic:<topic>" ueberschreibbar.
String alarmtopic = ALARM_TOPIC;
// Ein unquittierter Alarm. Die ISR setzt nur alarmAck, quittiert wird im Loop.
volatile bool alarmPending = false;
volatile bool alarmAck = false;
#ifdef EPAPER
// Von der Tasten-ISR gesetzt, im Loop-Task abgearbeitet.
volatile bool screenFwdPending = false;
volatile bool screenBackPending = false;
volatile bool alarmTestPending = false;
#endif
String alarmName = "";
String alarmCondition = "";
String alarmValue = "";

int MQ_COLOR = TFT_WHITE;

u_int64_t espID = 0;
String client_id = "000000";

// Create object "tft"
#ifdef EPAPER
EpdDisplay display;
#else
TFT_eSPI display = TFT_eSPI();
#endif
bool displayON = true;

//Declare BLEScanner
BLEScan* pBLEScan;

#ifdef INTERNAL_SHTC3
Shtc3 internalSensor;
bool internalSensorFound = false;
String internalMac = "";      // wie eine BLE-MAC aufgebaut, aus der ESP-ID
int internalNum = 0;          // Platz in der Sensorliste
#endif

int num = 0;

#define BAT_VOLT 0
#define BAT_PERCENT 1

// Sensor
struct tempSensor {
  std::string mac = "00:00:00:00:00:00";
  String type = "none";
  String name = "none";
  String fullname = "none";
  String device = "none";
  double temp = 0.0;
  double hum = 0.0;
  double bat = 0.0;
  int num = 0;
  int rssi = 0;
  int battype = 0; // 0=Volts, 1=Percent
  String lastupdate = "";

  // Alarmregeln aus der MQTT-Config: alarm:<device>:<temp|hum>:<gt|lt>:<wert>
  // Je Sensor ist eine Regel fuer Temperatur und eine fuer Feuchte moeglich.
  // "raised" merkt sich, dass bereits ausgeloest wurde - scharf wird die Regel
  // erst wieder, wenn der Wert in den gueltigen Bereich zurueckkehrt.
  bool   alTempOn = false;  bool alTempGt = true;  double alTempLimit = 0.0;
  bool   alHumOn  = false;  bool alHumGt  = true;  double alHumLimit  = 0.0;
  bool   alTempRaised = false;
  bool   alHumRaised  = false;
};

std::list<tempSensor> sensors;
#ifdef C11
std::__cxx11::string string_to_hex(const std::__cxx11::string& input, int length = 0)
#else
String string_to_hex(const String& input, int length = 0)
#endif
{
    static const char hex_digits[] = "0123456789ABCDEF:";

    String output = "";
    if (length == 0) length = input.length();
    output.reserve(length * 3);

    for (int i=0; i < length; i++)
    {
        unsigned char c = input[i];
        output = output + hex_digits[c >> 4];
        output = output + hex_digits[c & 15];
        output = output + hex_digits[16];
    }
    return output;
}

/**
 * Print Debug Output to Serial and/or MQTT
**/
#ifdef C11
void debugPrintln(std::__cxx11::string msg) 
#else
int debugPrintln(String msg) 
#endif
{
  //if (client.connected()) {
  //  client.publish(debugtopic.c_str(), msg.c_str());
  //}
  Serial.println(msg.c_str());
  return 1;
}

tempSensor getSensor(std::string mac, String name) {
  for (tempSensor t : sensors) {
    if (t.mac == mac) return t;
  }
  tempSensor t1;
  t1.mac = mac;
  t1.device = (mac.substr(0,2) + mac.substr(3,2) + mac.substr(6,2) + mac.substr(9,2) + mac.substr(12,2) + mac.substr(15,2)).c_str();
  t1.type = "new";
  t1.name = name;
  t1.num = sensors.size() + 1;
  sensors.push_back(t1);
  #ifdef MQTT
  String msg = "getconfig:"+client_id;
  client.publish(getconftopic.c_str(),msg.c_str());
  #endif
  return sensors.back();
}

tempSensor getSensor(std::string mac) {
  tempSensor t1;
  for (tempSensor t : sensors) {
    if (t.mac == mac) return t;
  }
  return t1;
}

tempSensor getSensor(int n) {
  tempSensor t1;
  for (tempSensor t : sensors) {
    if (t.num == n) return t;
  }
  return t1;
}

void setSensor(std::string mac, String type, double temp, double hum, double bat, int rssi, int battype) {
  for (auto it = sensors.rbegin(); it != sensors.rend(); it++) {
    if (it->mac == mac) {
      it->type = type;
      it->temp = temp;
      it->hum = hum;
      it->bat = bat;
      it->rssi = rssi;
      it->battype = battype;
      it->lastupdate = mqttdate + " " + mqtttime;
      return;
    }
  }
}

// alarm:<device>:<temp|hum>:<gt|lt>:<wert>
// Die Config trifft nach jedem getconfig erneut ein, das Setzen muss also
// idempotent sein: eine unveraenderte Regel laesst "raised" in Ruhe, sonst
// wuerde ein laufender Alarm bei jeder Neukonfiguration erneut ausloesen.
void setSensorAlarm(String device, String field, String op, double limit) {
  bool gt = (op == "gt");
  if (!gt && op != "lt") {
    Serial.printf("Alarm config: unbekannter Operator '%s'\n", op.c_str());
    return;
  }
  for (auto it = sensors.rbegin(); it != sensors.rend(); it++) {
    if (it->device != device) continue;

    if (field == "temp") {
      bool same = it->alTempOn && it->alTempGt == gt && it->alTempLimit == limit;
      it->alTempOn = true; it->alTempGt = gt; it->alTempLimit = limit;
      if (!same) it->alTempRaised = false;
    } else if (field == "hum") {
      bool same = it->alHumOn && it->alHumGt == gt && it->alHumLimit == limit;
      it->alHumOn = true; it->alHumGt = gt; it->alHumLimit = limit;
      if (!same) it->alHumRaised = false;
    } else {
      Serial.printf("Alarm config: unbekanntes Feld '%s'\n", field.c_str());
      return;
    }
    Serial.printf("Alarm config: %s %s %s %.2f\n",
                  device.c_str(), field.c_str(), op.c_str(), limit);
    return;
  }
  // Sensor noch nicht entdeckt - die Config kommt beim naechsten getconfig erneut.
}

void setSensorName(String device, String fullname) {
  for (auto it = sensors.rbegin(); it != sensors.rend(); it++) {
    if (it->device == device) {
      it->fullname = fullname;
      return;
    }
  }
}

void displaySensor(int i);

// Hoechster gueltiger Screen-Index: 0 ist Datum/Uhrzeit, 1..n die Sensoren.
// Der interne Sensor steht als regulaerer Eintrag mit in der Liste.
static int maxScreen() { return (int)sensors.size(); }

void display_indicators() {
  //display WiFI & MQTT Connection
  u_int16_t col = TFT_WHITE;
  #ifdef WIFI
  if (WiFi.status() == WL_CONNECTED) {
    col = TFT_GREEN;
  } else {
    col = TFT_RED;
  }
  #endif 
  display.drawBitmap(IND_ICON_X, 2, wifiicon,16,16,col);
  display.setTextColor(MQ_COLOR, TFT_BLACK);
  display.setTextFont(2);
  display.setCursor(IND_MQ_X, 0);
  display.print("MQ");
  display.setTextColor(TFT_WHITE);
  #ifdef EPAPER
  // Monochrom: der Zustand laesst sich nicht ueber die Farbe zeigen,
  // deshalb wird "nicht verbunden" durchgestrichen dargestellt.
  if (col == TFT_RED) display.drawFastHLine(IND_ICON_X, 10, 16, TFT_WHITE);
  if (MQ_COLOR == TFT_RED) display.drawFastHLine(IND_MQ_X, 9, 29, TFT_WHITE);
  #endif
}

void display_indicators(int col) {
  MQ_COLOR = col;
  #ifdef EPAPER
  // Auf e-Paper ist nur ein vollstaendiger Frame mit dem vorherigen
  // vergleichbar - deshalb den ganzen Screen neu aufbauen. flush()
  // unterdrueckt den Refresh, wenn sich das Bild nicht geaendert hat.
  displaySensor(num);
  #else
  display_indicators();
  #endif
}

#ifdef EPAPER
// ---------------------------------------------------------------------------
// Layout fuer das 200x200-e-Paper
// ---------------------------------------------------------------------------

// Kuerzt s so weit, bis es in maxW Pixel passt (Schrift vorher setzen).
static String epdFit(const String& s, int16_t maxW) {
  String out = s;
  while (out.length() > 1 && display.textWidth(out) > maxW) {
    out.remove(out.length() - 1);
  }
  return out;
}

// Kopfzeile: Screen-Punkte links, WLAN/MQTT-Indikatoren rechts, Trennlinie.
static void epdHeader(int active) {
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(0);
  display.setTextSize(2);
  for (int i = 0; i <= maxScreen(); i++) {
    int16_t x = MARGIN_X + i * EPD_DOT_STEP;
    if (x + EPD_DOT_STEP > IND_ICON_X) break;   // nicht in die Indikatoren laufen
    display.setCursor(x, 2);
    display.print(i == active ? "o" : ".");
  }
  display.setTextSize(1);
  display_indicators();
  display.drawFastHLine(0, EPD_Y_RULE, SCREEN_WIDTH, TFT_WHITE);
}

// Gesamtbreite eines Wertes aus epdValue() - fuer rechtsbuendige Platzierung.
static int16_t epdValueWidth(const char* big, const char* small) {
  display.setTextFont(6);
  int16_t w = display.textWidth(big);
  display.setTextFont(4);
  return w + display.textWidth(small);
}

// Grosse Zahl mit kleinerem Nachkomma-/Einheitenteil. Beide Teile werden auf
// der Grundlinie ausgerichtet, nicht oben buendig - sonst klebt die kleine
// Schrift am oberen Rand der grossen Ziffern.
static void epdValue(int16_t x, int16_t y, const char* big, const char* small) {
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(6);
  int16_t baseline = y + display.fontAscent();
  display.setCursor(x, y);
  display.print(big);
  int16_t xs = display.getCursorX();
  display.setTextFont(4);
  display.setCursor(xs, baseline - display.fontAscent());
  display.print(small);
}

// Fusszeile ueber einer Trennlinie, immer in der kleinsten Schrift.
static void epdFooter(const String& text) {
  display.drawFastHLine(0, EPD_Y_FOOT, SCREEN_WIDTH, TFT_WHITE);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(0);
  display.setTextSize(1);
  display.setCursor(MARGIN_X, EPD_Y_FOOT + 6);
  display.print(epdFit(text, SCREEN_WIDTH - 2 * MARGIN_X));
}

// Alarmbild: bewusst anders aufgebaut als die Sensor-Screens, damit es sich
// auf einen Blick unterscheidet - invertierter Kopf statt Punktleiste.
void displayAlarm() {
  display.fillScreen(TFT_BLACK);

  const int16_t maxW = SCREEN_WIDTH - 2 * MARGIN_X;

  // Invertierter Balken: Flaeche mit Tinte fuellen, Text in Papierfarbe
  display.fillRect(0, 0, SCREEN_WIDTH, 34, TFT_WHITE);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextFont(4);
  display.setCursor((SCREEN_WIDTH - display.textWidth("ALARM")) / 2, 6);
  display.print("ALARM");

  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(2);
  display.setCursor(MARGIN_X, 46);
  display.print(epdFit(alarmName, maxW));

  display.setTextFont(4);
  display.setCursor(MARGIN_X, 74);
  display.print(epdFit(alarmCondition, maxW));

  display.setTextFont(6);
  display.setCursor(MARGIN_X, 108);
  display.print(epdFit(alarmValue, maxW));

  display.drawFastHLine(0, EPD_Y_FOOT, SCREEN_WIDTH, TFT_WHITE);
  display.setTextFont(2);
  display.setCursor(MARGIN_X, EPD_Y_FOOT + 4);
  display.print("Taste = quittieren");

  DISPLAY_FLUSH();
}

void displayDateTime() {
  display.fillScreen(TFT_BLACK);

  if (!displayON) {
    display_indicators();
    DISPLAY_FLUSH();
    return;
  }

  epdHeader(0);

  const int16_t maxW = SCREEN_WIDTH - 2 * MARGIN_X;

  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(0);
  display.setTextSize(1);
  display.setCursor(MARGIN_X, 25);
  display.print(epdFit("Device: " + client_id, maxW));

  // Uhrzeit - das groesste Element, zentriert
  display.setTextFont(6);
  display.setCursor((SCREEN_WIDTH - display.textWidth(mqtttime)) / 2, 44);
  display.print(mqtttime);

  // Datum darunter, ebenfalls zentriert
  display.setTextFont(4);
  display.setCursor((SCREEN_WIDTH - display.textWidth(mqttdate)) / 2, 90);
  display.print(mqttdate);

  // Netzwerk - je Angabe eine eigene Zeile, sonst wird es auf 200px zu breit
  display.setTextFont(2);
  display.setCursor(MARGIN_X, 122);
  display.print(epdFit("SSID: " + ssid, maxW));
  display.setCursor(MARGIN_X, 140);
  display.print(epdFit("IP:   " + ip.toString(), maxW));
  display.setCursor(MARGIN_X, 158);
  display.print("Sensoren: " + String((unsigned)sensors.size()));

  epdFooter("BLE2MQTT " + version);

  DISPLAY_FLUSH();
}

void displayScreen(tempSensor t) {
  display.fillScreen(TFT_BLACK);

  if (!displayON) {
    display_indicators();
    DISPLAY_FLUSH();
    return;
  }

  epdHeader(t.num);

  const int16_t maxW = SCREEN_WIDTH - 2 * MARGIN_X;
  char big[16], small[16];

  // Geraetenummer und Sensortyp
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(0);
  display.setTextSize(1);
  display.setCursor(MARGIN_X, 25);
  display.print(epdFit("Device " + String(t.num) + " (" + t.type + ")", maxW));

  // Klarname, ersatzweise die MAC
  display.setTextFont(2);
  display.setCursor(MARGIN_X, 36);
  display.print(epdFit(t.fullname == "none" ? String(t.mac.c_str()) : t.fullname, maxW));

  // Temperatur
  int sign = (t.temp < 0.0) ? -1 : 1;
  double frac = (t.temp - int(t.temp)) * sign;
  snprintf(big, sizeof(big), "%d", (int)t.temp);
  if (t.type == "ThermoBeacon") {
    snprintf(small, sizeof(small), ".%02d C", (int)(frac * 100.0));
  } else {
    snprintf(small, sizeof(small), ".%01d C", (int)(frac * 10.0));
  }
  // Breite vor dem Zeichnen bestimmen, damit die Glocke daneben passt.
  int16_t tw = epdValueWidth(big, small);
  epdValue(MARGIN_X, 54, big, small);
  if (t.alTempOn) {
    // Temperatur steht linksbuendig -> Symbol dahinter.
    // Gefuellt, solange die Regel verletzt ist.
    display.drawBitmap(MARGIN_X + tw + EPD_ICON_GAP, 54 + EPD_ICON_DY,
                       t.alTempRaised ? alarmiconfull : alarmicon,
                       16, 16, TFT_WHITE);
  }

  // Luftfeuchte - rechtsbuendig und tiefer, also diagonal zur Temperatur.
  // Der Platz dafuer kommt aus der zusammengelegten Bat/RSSI-Zeile.
  snprintf(big, sizeof(big), "%d", (int)t.hum);
  snprintf(small, sizeof(small), ".%02u %%", (unsigned)((t.hum - int(t.hum)) * 100));
  int16_t hw = epdValueWidth(big, small);
  int16_t hx = SCREEN_WIDTH - MARGIN_X - hw;
  epdValue(hx, 108, big, small);
  if (t.alHumOn) {
    // Feuchte steht rechtsbuendig -> Symbol davor
    display.drawBitmap(hx - EPD_ICON_GAP - 16, 108 + EPD_ICON_DY,
                       t.alHumRaised ? alarmiconfull : alarmicon,
                       16, 16, TFT_WHITE);
  }

  // Batterie und Empfangsstaerke teilen sich eine Zeile: links bzw. rechts
  // buendig, damit sie bei langen Werten nicht kollidieren.
  display.setTextFont(2);
  String bat;
  switch (t.battype) {
    case BAT_VOLT:    bat = "Bat: " + String(t.bat, 2) + "V"; break;
    case BAT_PERCENT: bat = "Bat: " + String((int)t.bat) + "%"; break;
    default: break;
  }
  // Ohne "db": mit Einheit stossen die beiden Werte bei "Bat: 2.75V" und
  // "RSSI: -100" exakt aneinander. Die Einheit von RSSI ist ohnehin klar.
  String rssi = "RSSI: " + String(t.rssi);
  display.setCursor(MARGIN_X, 154);
  display.print(bat);
  display.setCursor(SCREEN_WIDTH - MARGIN_X - display.textWidth(rssi), 154);
  display.print(rssi);

  epdFooter("Update: " + t.lastupdate);

  DISPLAY_FLUSH();
}

#else   // ------------------------------- TFT-Layout (unveraendert) --------

void displayDateTime() {
  display.fillScreen(TFT_BLACK);
  
  if (!displayON) {
    display_indicators();
    DISPLAY_FLUSH();
    return;
  }
    
  uint8_t y = 0;
  uint8_t d = ROW_STEP;
  uint8_t x = MARGIN_X;

  // display sensor numbers
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(4);
  for (int i=0; i <= sensors.size(); i++) {
    display.setCursor(x + i*20, y);
    if (i == 0) {
      display.print("o");
    } else {
      display.print(".");
    }
  }

  display_indicators();

  // display Time
  x = MARGIN_X;
  y=24;
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(2);
  display.setCursor(x,y);
  display.printf("Device: %s",client_id.c_str());

  y=y+22;
  display.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(6);
  #ifdef S3
  x = (SCREEN_WIDTH - display.textWidth(mqtttime.c_str())) / 2;
  #else
  x = 50;
  #endif
  display.setCursor(x,y);
  display.printf(mqtttime.c_str());

  // display Date
  y=y+d+12;
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(4);
  #ifdef S3
  x = (SCREEN_WIDTH - display.textWidth(mqttdate.c_str())) / 2;
  #else
  x = 30;
  #endif
  display.setCursor(x,y);
  display.printf(mqttdate.c_str());

    // display WiFi
  x = MARGIN_X;
  y=y+26;
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(2);
  display.setCursor(x,y);
  //wifi_power_t tx = WiFi.getTxPower();
  display.printf("%s - %s",ssid.c_str(),ip.toString().c_str());

  DISPLAY_FLUSH();
}

//function that prints the latest sensor readings in the OLED display
void displayScreen(tempSensor t) {
  display.fillScreen(TFT_BLACK);

  if (!displayON) {
    display_indicators();
    DISPLAY_FLUSH();
    return;
  }

  uint8_t y = 0;
  uint8_t d = ROW_STEP;
  uint8_t x = MARGIN_X;

  // display sensor numbers
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(4);
  for (int i=0; i <= sensors.size(); i++) {
    display.setCursor(x + i*20, y);
    if (t.num == i) {
      display.print("o");
    } else {
      display.print(".");
    }
  }

  display_indicators();

  // display device
  x = MARGIN_X;
  y=22;
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(2);
  display.setCursor(x,y);
  display.printf("Device %02u (%s)",t.num ,t.type.c_str());
  display.setTextFont(0);
  display.setTextSize(2);
  display.setCursor(x,y+18);
  if (t.fullname == "none") {
    display.print(t.mac.c_str());
  } else {
    display.print(t.fullname.c_str());
  }
  // display temperature
  y=y+d+9;  
  display.setTextColor(TFT_ORANGE, TFT_BLACK);
  display.setCursor(x,y);
  display.setTextSize(1);
  display.setTextFont(6);
  int sign = 1;
  if (t.temp < 0.0 ) sign = -1;
  display.printf("%d", (int)t.temp);
  display.setTextFont(4);
  // Set Precision
  double value = (t.temp - int(t.temp)) * sign;
  if ( t.type == "ThermoBeacon") {
    display.printf(".%02dC",(int)(value*100.0));
  } else {
    display.printf(".%01dC",(int)(value*10.0));
  }
  
  //display humidity
  x = COL2_X;
  display.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  display.setCursor(x, y);
  display.setTextFont(6);
  display.print(int(t.hum));
  display.setTextFont(4);
  display.printf(".%02u%%",int((t.hum - int(t.hum))*100));

  //display Battery
  x = MARGIN_X;
  y=y+d+5;
  display.setTextColor(TFT_GREEN, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(2);
  display.setCursor(x, y+10);
  display.printf("Bat: ");
  display.setTextFont(4); 
  switch (t.battype)
  {
  case BAT_VOLT:
    display.printf("%.2f", t.bat);
    display.print("V");
    break;
  case BAT_PERCENT:
    display.printf("%.0f", t.bat);
    display.print("%");
    break;
  default:
    break;
  }

  //display RSSI
  x = COL2_X;
  display.setTextColor(TFT_GREEN, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(2);
  display.setCursor(x, y+10);
  display.printf("RSSI: ");
  display.setTextFont(4);
  display.printf("%02ddb", t.rssi);

  #ifdef S3
  // Lastupdate in der gewonnenen unteren Flaeche (volle Breite)
  x = MARGIN_X;
  y = SCREEN_HEIGHT - 18;
  display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(2);
  display.setCursor(x, y);
  display.printf("Update: %s", t.lastupdate.c_str());
  #endif

  display.setTextFont(0);
  display.setTextSize(1);

  DISPLAY_FLUSH();
}

#endif  // EPAPER

void displaySensor(std::string mac) {
  displayScreen(getSensor(mac));
}

void displaySensor(int i){
#ifdef EPAPER
  // Das Alarmbild hat Vorrang, bis quittiert wurde - sonst ueberschreibt es
  // die naechste Uhrzeit-Aktualisierung nach wenigen Sekunden.
  if (alarmPending) return;
#endif
  if ( i == 0 ) { 
    displayDateTime();
  } else {
    displayScreen(getSensor(i));
  }
}

#ifdef EPAPER
// Ein e-Paper-Refresh dauert mehrere hundert Millisekunden und braucht viel
// Stack (Float-printf zieht ueber _dtoa_r einige hundert Byte). Weder der
// Tasten-Interrupt noch der BLE-Callback duerfen deshalb selbst zeichnen:
// der eine laeuft im Interrupt, der andere auf dem knappen Stack des
// Bluetooth-Tasks (BTC_TASK). Beide merken den Wunsch nur vor, gezeichnet
// wird in serviceDisplay() aus dem Loop-Task heraus.
volatile bool displayDirty = false;
#endif

void screenForward();
void screenBackward();
#ifdef EPAPER
void alarmTest();
#endif

// Aufgeschobene Neuzeichnung abarbeiten (nur e-Paper, sonst No-Op).
void serviceDisplay() {
#ifdef EPAPER
  // Langer Druck: Alarmtest fuer den angezeigten Sensor. Vor der Quittierung
  // pruefen, sonst quittiert derselbe Druck den gerade erzeugten Alarm.
  if (alarmTestPending) {
    alarmTestPending = false;
    Serial.println("Langer Tastendruck -> Alarmtest");
    alarmTest();
    return;
  }

  // Quittierten Alarm aufloesen und zum vorherigen Screen zurueck
  if (alarmPending && alarmAck) {
    alarmPending = false;
    alarmAck = false;
    Serial.println("Alarm quittiert.");
    #ifdef MQTT
    // Ueberschreibt die retained Alarmmeldung: ein neu hinzukommender
    // Abonnent sieht dadurch den quittierten Zustand statt eines alten
    // Alarms, der laengst erledigt ist.
    if (client.connected()) {
      client.publish(alarmtopic.c_str(), "alarm bestätigt", true);
    }
    #endif
    displaySensor(num);
    return;
  }
  if (alarmPending) return;      // Alarmbild stehen lassen

  // Kurzer Druck: blaettern. Die ISR hat nur gemerkt, welche Richtung.
  if (screenFwdPending)  { screenFwdPending = false;  screenForward(); }
  if (screenBackPending) { screenBackPending = false; screenBackward(); }

  if (displayDirty) {
    displayDirty = false;
    displaySensor(num);
  }
#endif
}

// Screen dieses Sensors anzeigen - wird aus dem BLE-Callback aufgerufen.
static void requestSensorScreen(const tempSensor& t) {
  num = t.num;
#ifdef EPAPER
  displayDirty = true;
#else
  displaySensor(t.num);
#endif
}

// Einen Alarm ausloesen: melden, anzeigen, Ton. Laeuft ausschliesslich im
// Loop-Task - Publish und Tonausgabe duerfen weder in der ISR noch im
// BLE-Callback passieren.
static void fireAlarm(const String& name, const String& cond, const String& val) {
  alarmName      = name;
  alarmCondition = cond;
  alarmValue     = val;

  Serial.printf("ALARM: %s | %s | ist %s\n",
                alarmName.c_str(), alarmCondition.c_str(), alarmValue.c_str());

  #ifdef MQTT
  if (client.connected()) {
    String msg = alarmName + ": " + alarmCondition + " = " + alarmValue;
    client.publish(alarmtopic.c_str(), msg.c_str(), true);
  }
  #endif

  alarmAck = false;
  alarmPending = true;
  #ifdef EPAPER
  displayAlarm();
  #endif
  #ifdef AUDIO_ALARM
  audioAlarm(&alarmAck);          // bricht ab, sobald eine Taste quittiert
  #endif
}

static void raiseAlarm(const tempSensor& t, const char* field,
                       bool gt, double limit, double value) {
  fireAlarm((t.fullname == "none") ? String(t.mac.c_str()) : t.fullname,
            String(field) + " " + (gt ? "gt" : "lt") + " " + String(limit, 1),
            String(value, 2) + (strcmp(field, "temp") == 0 ? " C" : " %"));
}

// Alarmtest fuer den gerade angezeigten Sensor: geht denselben Weg wie ein
// echter Alarm, also inklusive Publish - damit laesst sich die Kette bis zur
// Benachrichtigung pruefen. Als "Alarmtest" gekennzeichnet.
void alarmTest() {
  if (num == 0) {
    Serial.println("Alarmtest: Screen 0 ist kein Sensor.");
    return;
  }
  tempSensor t = getSensor(num);
  if (t.num == 0) { Serial.println("Alarmtest: kein Sensor auf diesem Screen."); return; }
  fireAlarm((t.fullname == "none") ? String(t.mac.c_str()) : t.fullname,
            "Alarmtest",
            String(t.temp, 2) + " C / " + String(t.hum, 2) + " %");
}

// Nach jedem Messzyklus alle Regeln pruefen. Eine Regel loest nur einmal aus
// und wird erst wieder scharf, wenn der Wert in den gueltigen Bereich
// zurueckkehrt - sonst alarmiert sie im Minutentakt weiter.
void checkAlarms() {
  for (auto it = sensors.begin(); it != sensors.end(); it++) {
    if (it->type == "new") continue;          // noch keine Messwerte

    if (it->alTempOn) {
      bool viol = it->alTempGt ? (it->temp > it->alTempLimit)
                               : (it->temp < it->alTempLimit);
      if (viol && !it->alTempRaised) {
        it->alTempRaised = true;
        raiseAlarm(*it, "temp", it->alTempGt, it->alTempLimit, it->temp);
      } else if (!viol) {
        it->alTempRaised = false;
      }
    }

    if (it->alHumOn) {
      bool viol = it->alHumGt ? (it->hum > it->alHumLimit)
                              : (it->hum < it->alHumLimit);
      if (viol && !it->alHumRaised) {
        it->alHumRaised = true;
        raiseAlarm(*it, "hum", it->alHumGt, it->alHumLimit, it->hum);
      } else if (!viol) {
        it->alHumRaised = false;
      }
    }
  }
}

#ifdef INTERNAL_SHTC3
// Der interne Sensor wird wie ein BLE-Sensor gefuehrt: gleiche Struktur,
// gleicher Screen, gleiche MQTT-Topics. Nur die Werte kommen nicht per
// Advertisement, sondern vom SHTC3 auf derselben Platine.
void registerInternalSensor() {
  byte* idb = (byte*)&espID;
  char buf[18];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
           idb[0], idb[1], idb[2], idb[3], idb[4], idb[5]);
  internalMac = buf;

  String dev = internalMac;
  dev.replace(":", "");

  tempSensor t;
  t.mac      = internalMac.c_str();
  t.device   = dev;
  t.type     = "SHT3";
  t.name     = "SHT3";
  t.fullname = "BLE2MQTT Intern";
  t.num      = sensors.size() + 1;
  sensors.push_back(t);
  internalNum = t.num;

  Serial.printf("Internal sensor registered: #%d %s (%s)\n",
                internalNum, internalMac.c_str(), dev.c_str());
}

// Messwerte uebernehmen. RSSI ist hier die WLAN-Feldstaerke des Gateways
// selbst, die Spannung kommt vom Systemzweig (ADC1_CH3 mit 1:2-Teiler,
// Beschaltung wie im Hersteller-Beispiel 01_ADC_Test).
void updateInternalSensor() {
  if (!internalSensorFound || internalNum == 0) return;
  if (!internalSensor.read()) return;

  double bat = analogReadMilliVolts(INTERNAL_VBAT_ADC) * 2.0 / 1000.0;
  int rssi = 0;
  #ifdef WIFI
  if (WiFi.status() == WL_CONNECTED) rssi = WiFi.RSSI();
  #endif

  setSensor(internalMac.c_str(), "SHT3",
            internalSensor.temp(), internalSensor.hum(), bat, rssi, BAT_VOLT);

  if (num == internalNum) displayDirty = true;   // im Loop gezeichnet
}
#endif  // INTERNAL_SHTC3

void printReadings(double temp, double hum, double bat, int rssi, int battype) {
  Serial.print("Temperature:");
  Serial.print(temp);
  Serial.print("C");
  Serial.print(" Humidity:");
  Serial.print(hum); 
  Serial.print("%");
  Serial.print(" Battery:");
  switch (battype) {
  case BAT_VOLT:
    Serial.printf("%.2f", bat);
    Serial.print("V");
    break;
  case BAT_PERCENT:
    Serial.printf("%.0f", bat);
    Serial.print("%");
    break;
  default:
    break;
  }
  Serial.print(" RSSI:");
  Serial.print(rssi);
  Serial.println("db");
}

bool decrypt_message_(const u_int8_t *crypted_data, const u_int8_t crypted_len,
                                  u_int8_t encrypted_data[VICTRON_ENCRYPTED_DATA_MAX_SIZE],
                                  const u_int8_t data_counter_lsb, const u_int8_t data_counter_msb) {
  esp_aes_context ctx;
  esp_aes_init(&ctx);
  uint8_t bindkey[35];
  memset( bindkey, 0, sizeof( bindkey ) );
  strcpy( (char *) bindkey, SMARTSOLAR_ENCRYPTION_KEY);
  auto status = esp_aes_setkey(&ctx, bindkey, 16 * 8);
  if (status != 0) {
    Serial.printf("Error during esp_aes_setkey operation (%i).", status);
    esp_aes_free(&ctx);
    return false;
  }

  size_t nc_offset = 0;
  u_int8_t nonce_counter[16] = {data_counter_lsb, data_counter_msb, 0};
  u_int8_t stream_block[16] = {0};

  status = esp_aes_crypt_ctr(&ctx, crypted_len, &nc_offset, nonce_counter, stream_block, crypted_data, encrypted_data);
  if (status != 0) {
    //ESP_LOGE(TAG, "[%s] Error during esp_aes_crypt_ctr operation (%i).", this->address_str().c_str(), status);
    esp_aes_free(&ctx);
    return false;
  }

  esp_aes_free(&ctx);
  Serial.printf("Enrypted message: %s",
           string_to_hex((char *)encrypted_data, crypted_len).c_str());
  return true;
}

//Callback function that gets called, when another device's advertisement has been received
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    tempSensor t;
    double temp;
    double hum;
    double bat;
    int battype;

    int rssi = advertisedDevice.getRSSI();

    std::string mac = advertisedDevice.getAddress().toString();
    std::string strname = advertisedDevice.getName();
    std::string strdata = advertisedDevice.getManufacturerData();

    char cname[strname.length() + 1];
    strname.copy(cname, strname.length(), 0);
    cname[strname.length()] = '\0';

    char cdata[strdata.length() + 1];
    strdata.copy(cdata, strdata.length(), 0);
    cdata[strdata.length()] = '\0';

    //String name = String(cname);
    //String data = String(cdata);

    u_int8_t *payload = advertisedDevice.getPayload();
    int plength = advertisedDevice.getPayloadLength() ;
    char spayload[plength + 1];
    memcpy(spayload, payload, plength);



    if (DEBUG) {
      debugPrintln("Found Device Advertisement. Mac-Address=" + String(mac.c_str()));
      debugPrintln("Name lenght=" + String(strname.length()) + ", Data length=" + String(strdata.length()));
      debugPrintln("Name: " + string_to_hex(cname, strname.length()));     
      //debugPrintln("Manufacturer=" + mac.substring(0,8) + " - Device Name: " + name);
      //debugPrintln("Manufacturer Data length=" + strdata.length());
      debugPrintln("Data: " + string_to_hex(cdata, strdata.length()));
      
      spayload[plength] = '\0';

      debugPrintln("Payload Raw - length=" + String(plength));
      //Serial.println(spayload);
      debugPrintln("Payload: " + string_to_hex(spayload, plength));
      //Serial.print("Manufacturer Key: ");
      //Serial.println(string_to_hex(spayload, plength).substring(69,5).c_str());
    }
    /*
    *     ThermoBeacon
    *
    */
    if (strname == "ThermoBeacon") { //Check if the name of the advertiser matches   

      if (strdata.length() == 20) {
        Serial.print("ThermoBeacon Data Received: ");
        Serial.println(mac.c_str());

        t = getSensor(mac,"ThermoBeacon");
        if (t.type == "new") {
          Serial.printf("Found New Sensor: %s Type=ThermoBeacon count=",strname.c_str());
          Serial.println(sensors.size());
        }
        /* 
        // New Method for Temperature
        int16_t btemp = data[12] + 256 * data[13];
        double ntemp = (double)btemp / 16;

        Serial.print("New Temperature=");
        Serial.println(ntemp);
        */
        temp = ((double)cdata[12] + 256 * (double)cdata[13]) / 16;
        if (temp > 4000) temp = temp - 4096;         
        hum = ((double)cdata[14] + 256 * (double)cdata[15]) / 16;
        bat = ((double)cdata[10] + 256 *(double)cdata[11]) / 1000;
        battype = BAT_VOLT;

        setSensor(mac,"ThermoBeacon",temp,hum,bat,rssi,battype);
        printReadings(temp,hum,bat,rssi,battype);

        if (t.type == "new" || t.num == num) {
          requestSensorScreen(t);
        }
      }
      if (strdata.length() == 22) {
 
      }
    }
    /*
    *     Govee H5075
    *
    */
    if (mac.substr(0,8) == GOVEE_BT_mac_OUI_PREFIX && strdata.length() > 15 ) { //data.lengh() == 17 ??
      Serial.print("Govee H5075 Data Received: ");
      Serial.println(mac.c_str());

      // Ist das Paket, die payload 10 Byte lang oder 17 byte ??
      //                                                                09:ff:88:ec:00:03:32:9c:64:00 (value = 5,6,7 und bat = 8)
      //                                           03:03:88:ec:02:01:05:09:ff:88:ec:00:03:32:9c:64:00 (value = 12,13,14 und bat = 15)
      // 0D:09:47:56:48:35:30:37:35:5F:33:32:37:37:03:03:88:EC:02:01:05:09:FF:88:EC:38:03:46:0B:64: (58)
      // 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30

      if (plength < 30) {
        if (DEBUG) debugPrintln("Govee payload too short (" + String(plength) + "), skipping");
        return;
      }

      u_int32_t value = (u_int8_t)spayload[26] * 65536 + (u_int8_t)spayload[27] * 256 + (u_int8_t)spayload[28];

      // Precision für %.1f für temp und hum
      if (value & 0x800000) {
        temp = (double)(value ^ 0x800000) / -10000.0;
      } else {
        temp = (double)value / 10000.0;
      }
      hum = (double)(value % 1000) / 10.0;
      bat = (double)(u_int8_t)spayload[29];
      battype = BAT_PERCENT;

      t = getSensor(mac,"Govee H5075");
      if (t.type == "new") {
        Serial.printf("Found New Sensor: %s Type=H5075 count=", strname.c_str());
        Serial.println(sensors.size());
      }

      setSensor(mac,"Govee H5075",temp,hum,bat,rssi,battype);
      printReadings(temp,hum,bat,rssi,battype);

      if (t.type == "new" || t.num == num) {
        requestSensorScreen(t);
      }
    }
    /*
    *     Victron SmartSolar? 
    *     VICTRON_MANUFACTURER_ID = 0x02E1;
    *
    */
    if (mac.substr(0,8) == VICTRON_BT_mac_OUI_PREFIX) {
      Serial.print("Victron Data Received: ");
      Serial.println(mac.c_str());
      char recordtype = cdata[0];
      u_int16_t noonce = cdata[1] + 256 * cdata[2];
      char byte0 = cdata[3];
      debugPrintln("Received Victron Data: Recordtype=" + recordtype);
      debugPrintln("  noonce=" + String(noonce) + " byte0=" + byte0);

      if (recordtype == 0x01) { // Solar Charger
        u_int8_t *data;

        
      }
    }

    if (DEBUG) Serial.println("--------------------------------------------------------------------------");
  }
};

// Screen-Navigation als normale Funktionen - so koennen Taste und
// MQTT-Kommando denselben Weg nehmen, ohne dass die ISR mitzeichnet.
static void requestRedraw() {
#ifdef EPAPER
  displayDirty = true;
#else
  displaySensor(num);
#endif
}

void screenForward() {
  if (!displayON) { displayON = true; }
  else { num = (num < maxScreen()) ? num + 1 : 0; }
  requestRedraw();
}

void screenBackward() {
  if (!displayON) { displayON = true; }
  else { num = (num > 0) ? num - 1 : maxScreen(); }
  requestRedraw();
}

#ifdef EPAPER
// Die ISR misst nur die Druckdauer und setzt Flags; gezeichnet, gemeldet und
// getoent wird im Loop-Task. Interrupt daher auf CHANGE statt RISING.
#ifndef LONG_PRESS_MS
#define LONG_PRESS_MS 1500
#endif

volatile uint32_t btn1DownMs = 0, btn2DownMs = 0;

static void IRAM_ATTR buttonEdge(uint8_t pin, volatile uint32_t* downMs,
                                 volatile bool* shortPress, bool allowTest) {
  uint32_t now = millis();
  if (digitalRead(pin) == LOW) {        // gedrueckt (Taster gegen GND)
    *downMs = now;
    return;
  }
  if (*downMs == 0) return;             // Loslassen ohne erfasstes Druecken
  uint32_t held = now - *downMs;
  *downMs = 0;
  if (held < 40) return;                // Prellen
  if (alarmPending) { alarmAck = true; return; }        // erste Taste quittiert
  if (allowTest && held >= LONG_PRESS_MS) { alarmTestPending = true; return; }
  *shortPress = true;
}

// Der lange Druck liegt bewusst nur auf Button 2 (BOOT). Button 1 ist der
// PWR-Taster des Boards - laenger gehalten schaltet dessen Hardware-Latch das
// Geraet ab, ein Alarmtest waere dort also nicht zuverlaessig ausloesbar.
void IRAM_ATTR toggleButton1() { buttonEdge(BUTTON1PIN, &btn1DownMs, &screenFwdPending, false); }
void IRAM_ATTR toggleButton2() { buttonEdge(BUTTON2PIN, &btn2DownMs, &screenBackPending, true); }

#else   // TFT-Boards: unveraendert, Aktion direkt beim Loslassen

void IRAM_ATTR toggleButton1() { screenForward(); }
void IRAM_ATTR toggleButton2() { screenBackward(); }

#endif

#ifdef MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char spayload[length + 1];
  memcpy(spayload, payload, length);
  spayload[length] = '\0';
  String msg;
  char delimiter[] = ":";
  char *ptr;
  char temp[50];
  String device;
  String fullname;

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(spayload);

  if (strcmp(topic,conftopic.c_str()) == 0) {
    ptr = strtok(spayload, delimiter);
    if (ptr != NULL) {
      if (strcmp(ptr,"name") == 0) { // name:<device>:<fullname>
        ptr = strtok(NULL, delimiter);
        if (ptr == NULL) return;
        strlcpy(temp,ptr,sizeof(temp));
        device = temp;
        ptr = strtok(NULL, delimiter);
        if (ptr == NULL) return;
        strlcpy(temp,ptr,sizeof(temp));
        fullname = temp;
        setSensorName(device,fullname);
      }

      // alarm:<device>:<temp|hum>:<gt|lt>:<wert>
      if (strcmp(ptr,"alarm") == 0) {
        String field, op;
        ptr = strtok(NULL, delimiter);
        if (ptr == NULL) return;
        strlcpy(temp,ptr,sizeof(temp));
        device = temp;
        ptr = strtok(NULL, delimiter);
        if (ptr == NULL) return;
        strlcpy(temp,ptr,sizeof(temp));
        field = temp;
        ptr = strtok(NULL, delimiter);
        if (ptr == NULL) return;
        strlcpy(temp,ptr,sizeof(temp));
        op = temp;
        ptr = strtok(NULL, delimiter);
        if (ptr == NULL) return;
        setSensorAlarm(device, field, op, atof(ptr));
      }

      // alarmtopic:<topic> - der Rest der Zeile, Topics enthalten kein ':'
      if (strcmp(ptr,"alarmtopic") == 0) {
        ptr = strtok(NULL, delimiter);
        if (ptr == NULL) return;
        alarmtopic = ptr;
        Serial.printf("Alarm topic: %s\n", alarmtopic.c_str());
      }
    }   
  }

  if (strcmp(topic,cmdtopic.c_str()) == 0) {
    if ( strcmp(spayload,"restart") == 0) {
      ESP.restart();
    }

    if ( strcmp(spayload,"getVersion") == 0 ) {
      msg = "Version " + version;
      client.publish(conftopic.c_str() ,msg.c_str(), true);
    }

    if ( strcmp(spayload,"getIP") == 0 ) {
      msg = "IP=" + ip.toString() + " SSID=" + ssid + " Broker=" + broker.toString();
      client.publish(conftopic.c_str() ,msg.c_str(), true);
    }

    if ( strcmp(spayload,"reconfig") == 0 ) {
      msg = "getconfig:"+client_id;
      client.publish(getconftopic.c_str(),msg.c_str());
    }

    // setScreen+
    if ( strcmp(spayload,"setScreen+") == 0 ) {
      if (alarmPending) alarmAck = true; else screenForward();
    }

    // setScreen-
    if ( strcmp(spayload,"setScreen-") == 0 ) {
      if (alarmPending) alarmAck = true; else screenBackward();
    }

    // getMaxSensor
    if ( strcmp(spayload,"getMaxSensor") == 0 ) {
      // String(...) ist noetig: "maxScreen:" + size_t waere Zeigerarithmetik
      // auf dem Literal und hat "Screen:" statt "maxScreen:3" gesendet.
      msg = "maxScreen:" + String(maxScreen());
      client.publish(conftopic.c_str() ,msg.c_str(), true);
    }

    // setScreen: X
    
    // BlinkScreen
    
    // Turn Off Screen
    if ( strcmp(spayload,"blankScreen") == 0 ) {
      displayON = false;
      displaySensor(num);
    }
    // MQTT Debug On
    if ( strcmp(spayload,"debug") == 0 ) {
      if (!DEBUG) {
        DEBUG = true;
        debugPrintln("DEBUG=ON");
      } else {
        debugPrintln("DEBUG=OFF");
        DEBUG = false;
      }
      displaySensor(num);
    }

  } // End Commands

  if (strcmp(topic,timetopic.c_str()) == 0) {
    mqtttime = spayload;
    if (num == 0) {
      displaySensor(0);
    }
  }

  if (strcmp(topic,datetopic.c_str()) == 0) {
    mqttdate = spayload;
    if (num == 0) {
      displaySensor(0);
    }
  }
}

void mqttReconnect() {
  // Set MQ Indikator
  display_indicators(TFT_RED);

  if (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(client_id.c_str())) {
      display_indicators(TFT_GREEN);
      Serial.println("connected..");
      client.subscribe(conftopic.c_str());
      client.subscribe(cmdtopic.c_str());
      client.subscribe(timetopic.c_str());
      client.subscribe(datetopic.c_str());
      String msg = "Online " + version + ": SSID=" + ssid + " IP=" + WiFi.localIP().toString() + " Broker=" + broker.toString();
      client.publish(debugtopic.c_str(),msg.c_str(),true);
      display_indicators(TFT_DARKGREY);
    } else {
      display_indicators(TFT_RED);
      Serial.print("failed, rc=");
      Serial.println(client.state());
    }
  }
}


#endif

String mac2String(byte ar[]) {
  String s;
  for (byte i = 0; i < 6; ++i)
  {
    char buf[3];
    sprintf(buf, "%02X", ar[i]); // J-M-L: slight modification, added the 0 in the format for padding 
    s += buf;
    //if (i < 5) s += ':';
  }
  return s;
}

void setup() {
  //Start serial communication
  Serial.begin(115200);
  Serial.println("BLE2MQTT starting...");
  Serial.println(version);


  // Setup Buttons
  #ifdef EPAPER
  pinMode(BUTTON1PIN, INPUT_PULLUP);   // Taster gegen GND
  pinMode(BUTTON2PIN, INPUT_PULLUP);
  #else
  pinMode(BUTTON1PIN, INPUT);
  pinMode(BUTTON2PIN, INPUT);
  #endif

  // Setup Display
  Serial.println("Setup Display...");
  display.init();
  display.setRotation(1);
  display.setTextSize(1);
  display.setTextFont(4);

  //Welcome Messager
  #ifdef EPAPER
  // e-Paper: schwarz auf Papierweiss (TFT_BLACK = Papier im Wrapper).
  // Font 4 waere auf 200px zu breit, deshalb eine Stufe kleiner.
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE,TFT_BLACK);
  display.setTextFont(2);
  #else
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK,TFT_WHITE);
  #endif
  display.setCursor(0,25);
  display.println(" BLE2MQTT starting");
  display.printf("  Version: %s", version);
  display.println();
  display.println();
  display.println(" ..Nihil fit sine causa..");
  DISPLAY_FLUSH();
  delay(2000);

  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE,TFT_BLACK);
  display.setTextFont(2);
  display.setCursor(0,16);

  #ifdef BOARD_PWR_PIN
  // Versorgungsschiene der Peripherie freigeben. Der Hersteller ruft dafuer
  // VBAT_POWER_ON() beim Board-Start auf; ohne sie bleibt der I2C-Bus tot.
  pinMode(BOARD_PWR_PIN, OUTPUT);
  digitalWrite(BOARD_PWR_PIN, BOARD_PWR_ON_LEVEL);
  delay(50);
  Serial.printf("Board power rail (GPIO %d) on.\n", BOARD_PWR_PIN);
  #endif

  #ifdef AUDIO_PWR_PIN
  // Audio-Rail und Endstufe definiert setzen statt floaten lassen - der
  // Hersteller macht das beim Board-Start ebenso. Beide haengen am selben
  // Versorgungszweig wie die I2C-Pullups; offen gelassen kam der Bus in
  // einen Zustand, in dem SDA dauerhaft LOW blieb.
  pinMode(AUDIO_PWR_PIN, OUTPUT);
  pinMode(AUDIO_PA_PIN, OUTPUT);
  digitalWrite(AUDIO_PA_PIN, LOW);                 // Endstufe stumm
  #ifdef AUDIO_ALARM
  digitalWrite(AUDIO_PWR_PIN, LOW);                // Rail an (aktiv LOW)
  #else
  digitalWrite(AUDIO_PWR_PIN, HIGH);
  #endif
  delay(20);
  #endif

  #ifdef INTERNAL_SHTC3
  Serial.println("Init internal SHTC3...");
  internalSensorFound = internalSensor.begin();
  if (internalSensorFound) {
    Serial.println("Internal SHTC3 ready.");
    display.println("Internal sensor OK");
  } else {
    Serial.println("Internal SHTC3 NOT found!");
    display.println("No internal sensor");
  }
  DISPLAY_FLUSH();
  #endif

  #ifdef AUDIO_ALARM
  Serial.println("Init audio...");
  if (audioBegin()) {
    audioStartupBeep();
    display.println("Audio OK");
  } else {
    display.println("No audio");
  }
  DISPLAY_FLUSH();
  #endif

  Serial.println("Init BLE Device...");
  //Init BLE device
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setInterval(80);
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), FINE_SCAN);
  pBLEScan->setWindow(30);
  pBLEScan->setActiveScan(true);

  espID = ESP.getEfuseMac();
  client_id = "ble2mqtt-" + mac2String((byte*) &espID);

  #ifdef INTERNAL_SHTC3
  // Braucht die ESP-ID, steht deshalb hier und nicht bei der Sensor-Init.
  if (internalSensorFound) {
    registerInternalSensor();
    updateInternalSensor();
  }
  #endif
  conftopic = conftopic + client_id;
  cmdtopic = conftopic + "/cmd";
  debugtopic = debugtopic + client_id;

  #ifdef WIFI
  
  //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
  Serial.println("Creating WiFi Manager...");
  WiFiManager wm;  
  WiFi.mode(WIFI_STA);   

  // Button2 Press on Startup Resets WiFi Settings and starts AP Mode
  Serial.printf("Button pins: BUTTON1PIN(%d)=%d BUTTON2PIN(%d)=%d\n",
                BUTTON1PIN, digitalRead(BUTTON1PIN),
                BUTTON2PIN, digitalRead(BUTTON2PIN));
  bool resetWiFi = ( digitalRead(BUTTON1PIN) == LOW );
  #ifdef EPAPER
  // Button1 ist hier der PWR-Taster, mit dem auch eingeschaltet wird - beim
  // Boot ist er also haeufig noch gedrueckt. Nur bewusstes Weiterhalten ueber
  // das Zeitfenster hinaus loescht die WLAN-Daten.
  if (resetWiFi) {
    Serial.println("Button1 held - keep holding 3s to reset WiFi settings...");
    display.println("Hold 3s to reset WiFi");
    DISPLAY_FLUSH();
    delay(3000);
    resetWiFi = ( digitalRead(BUTTON1PIN) == LOW );
    if (!resetWiFi) Serial.println("Button1 released - WiFi settings kept.");
  }
  #endif
  if ( resetWiFi ) {
    Serial.println("Button1Pin is low - Reset WiFi Settings - Starting in AP Mode.");
    display.println("Reset WiFi Settings...");
    display.println("Starting AP Mode.");
    wm.resetSettings();
  } else {
    Serial.println("Connect to WiFi...");
    display.println("Connect to WIFI...");
  }

  bool res;
  wm.setConnectTimeout(10);
  res = wm.autoConnect("BLE2MQTT");   // offener Konfig-AP, kein Passwort
  if (!res) {
    Serial.println("Failed to connect to WiFi!");
  }
 
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    display.print("Cconnected! IP=");
    display.println(WiFi.localIP());
  } else {
    Serial.println("WiFi Connection Failed!");
  }
  #endif

  display.println("Searching Sensors");
  DISPLAY_FLUSH();

  #ifdef MQTT
  /* Prepare MQTT client */
  ssid = WiFi.SSID();
  ip = WiFi.localIP();

  if (ssid == "UPC4E87B2D" or ssid == "OpenHAB2")  {
    broker = broker_int;
  } else if (ssid == "OpenHAB") {
    broker = broker_openhab;
  } else {
    broker = broker_ext;
  }
  
  Serial.printf("Connect to MQTT at %s", broker.toString().c_str());
  Serial.println();
  client.setServer(broker, 1883);
  client.setCallback(mqttCallback);
  mqttReconnect();
  #endif

  // Attach Button Callbacks
  #ifdef EPAPER
  // CHANGE statt RISING: nur so laesst sich die Druckdauer messen und damit
  // ein langer Druck (Alarmtest) von einem kurzen (blaettern) unterscheiden.
  attachInterrupt(BUTTON1PIN, toggleButton1, CHANGE);
  attachInterrupt(BUTTON2PIN, toggleButton2, CHANGE);
  #else
  attachInterrupt(BUTTON1PIN, toggleButton1, RISING);
  attachInterrupt(BUTTON2PIN, toggleButton2, RISING);
  #endif
}

void loop() {
  String topic = "";
  String msg = "";
  String dev = "";

  #ifdef INTERNAL_SHTC3
  updateInternalSensor();
  #endif
  
  // Scan for Sensors
  Serial.println("Start Scanning...");
  // non Blocking Scan
  pBLEScan->start(0,nullptr,false);
  u_long startmillis = millis();
  while (millis() - startmillis < 60000 && millis() >= startmillis) {
    client.loop();
    serviceDisplay();
  }
  pBLEScan->stop();
  Serial.println("Stop Scanning...");

  // Publsh Sensor Values to MQTT
  #ifdef MQTT
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      mqttReconnect();
      if (client.connected()) {
        msg = "getconfig:"+client_id;
        client.publish(getconftopic.c_str(),msg.c_str());
      }
    } 
    // MQ Indikator anhand der Aktivität ändern
    
    if (client.connected()) {
     
      display_indicators(TFT_GREEN);

      for (tempSensor t : sensors) {
        Serial.print("Publish Sensor: ");
        Serial.print(t.mac.c_str());
        
        dev = t.device.c_str();
        Serial.print(" => ");
        Serial.println(basetopic + dev);

        topic = basetopic + dev + "_temp/state";
        msg = (String)t.temp;
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_hum/state";
        msg = (String)t.hum;
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_bat/state";
        msg = (String)t.bat;
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_battype/state";
        msg = (String)t.battype;
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_type/state";
        msg = t.type.c_str();
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_rssi/state";
        msg = (String)t.rssi;
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_name/state";
        msg = t.name.c_str();
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_fullname/state";
        msg = t.fullname.c_str();
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_gateway/state";
        msg = client_id;
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_lastupdate/state";
        msg = t.lastupdate;
        client.publish(topic.c_str(),msg.c_str(),true);
      }
      display_indicators(TFT_DARKGREY);
    }
  }

  #endif

  // Erst nach dem Publish pruefen, damit openHAB die Werte schon hat.
  // Laeuft im Loop-Task: Publish, Ton und Display sind hier erlaubt.
  checkAlarms();

}
