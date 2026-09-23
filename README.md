# ESP32BLE2MQTT

BLE-zu-MQTT-Bridge für ESP32 mit TFT- oder e-Paper-Display. Das Gerät scannt per Bluetooth
Low Energy nach Sensoren, zeigt deren Werte auf dem Display an und publiziert
sie an einen MQTT-Broker (Anbindung an openHAB).

Aktuelle Version: **V2.5**

## Funktionen

- **BLE-Scan** nach unterstützten Sensoren (siehe unten), inkl. Auswertung von
  Temperatur, Luftfeuchte, Batterie und RSSI.
- **Display** mit zwei Ansichten, per Hardware-Buttons umblätterbar:
  - Datum/Uhrzeit-Übersicht (Screen 0)
  - je ein Detail-Screen pro Sensor
- **MQTT-Publish** der Messwerte unter `/openhab/in/<device>/...`.
- **Fernsteuerung** per MQTT-Kommandos (Neustart, Screen wechseln, Display
  aus, Debug, …).
- **WiFi-Einrichtung** über einen WiFiManager-Konfig-Accesspoint (kein
  Hardcoding der Zugangsdaten).

## Unterstützte Sensoren

| Sensor | Erkennung | Werte |
|---|---|---|
| ThermoBeacon | Gerätename `ThermoBeacon` | Temp, Feuchte, Batterie (V) |
| Govee H5075 | MAC-Prefix `a4:c1:38` | Temp, Feuchte, Batterie (%) |
| Victron SmartSolar | MAC-Prefix `60:a4:23` | begonnen, noch unvollständig |

## Hardware / Build-Targets

Konfiguriert in [`platformio.ini`](platformio.ini). Die Display-Konfiguration
liegt vollständig in den `build_flags` (`USER_SETUP_LOADED` bzw. `EPD_*`), eine
manuelle Anpassung von `User_Setup_Select.h` im TFT_eSPI-libdeps-Ordner ist
**nicht** nötig.

| Env | Board | Display | Besonderheit |
|---|---|---|---|
| `lilygo-t-display-s3` | LilyGo T-Display-S3 (ESP32-S3) | ST7789 320×170, 8-Bit parallel (Setup206) | Upload über USB-JTAG mit `--no-stub` |
| `ttgo-t1` | TTGO T-Display (ESP32) | ST7789 240×135 SPI (Setup25) | — |
| `esp32-s3-epaper154` | Waveshare ESP32-S3-ePaper-1.54 (V1/V2) | fest verbautes 1.54"-Panel, SSD1681, 200×200 s/w | Display-Spannung muss per GPIO freigegeben werden |

Das Display-Layout passt sich je Board an die Auflösung an (die S3-Varianten
nutzen die größere Fläche inkl. zentrierter Datum/Uhrzeit-Anzeige und
Lastupdate-Zeile).

### e-Paper-Variante (`esp32-s3-epaper154`)

Gezeichnet wird über [`src/epd_display.h`](src/epd_display.h) – einen
TFT_eSPI-kompatiblen Wrapper um **GxEPD2**, der die technischen Unterschiede
kapselt:

- **Monochrom** – `TFT_BLACK` wird zu Papierweiß, jede andere Farbe zu Tinte.
  Zustände, die sonst über Farbe laufen (WLAN/MQTT getrennt), werden
  stattdessen durchgestrichen dargestellt.
- **Framebuffer statt Direktausgabe** – ein Refresh läuft erst, wenn ein Screen
  fertig gezeichnet ist, und **nur wenn sich der Inhalt geändert hat**. Das
  verhindert, dass der zyklische MQTT-Publish alle 60 s das Panel auffrischt.
- **Teil-Refresh** mit periodischem Voll-Refresh gegen Ghosting
  (`EPD_FULL_REFRESH_EVERY`, Standard: jeder 12. Refresh).
- **Tasten-ISR zeichnet nicht** – ein Refresh blockiert mehrere hundert
  Millisekunden, der Interrupt setzt deshalb nur ein Flag und das Neuzeichnen
  passiert im Loop.
- **Grundlinien-Ausrichtung** – Adafruit_GFX zeichnet ab der Grundlinie,
  TFT_eSPI ab der Oberkante; der Wrapper rechnet den Ascent dazu und gibt ihn
  über `fontAscent()` heraus, damit unterschiedlich große Textteile (große Zahl
  + kleine Einheit) auf einer Linie stehen.

#### Layout

Das 200×200-Panel ist **quadratisch und hochkant**, die TFT-Boards sind quer
(320×170 bzw. 240×135). Ein gemeinsames Layout ginge sich nicht aus, deshalb
haben `displayScreen()` und `displayDateTime()` in
[`main.cpp`](src/main.cpp) eine eigene `#ifdef EPAPER`-Variante:

- **Temperatur und Feuchte diagonal versetzt** – Temperatur links oben,
  Feuchte rechtsbündig darunter. Beide in FreeSansBold 24 pt; zweispaltig
  nebeneinander wären sie auf 200 px nicht lesbar unterzubringen.
- **Batterie und RSSI teilen sich eine Zeile** (links bzw. rechts bündig). Der
  dadurch gewonnene vertikale Platz geht als Abstand an die beiden Messwerte.
- Datum/Uhrzeit und Netzwerkangaben bekommen je eine eigene Zeile – kombiniert
  passten sie nicht in 200 px Breite.
- Variable Texte (Name, MAC, SSID, IP) werden über `epdFit()` auf die
  verfügbare Breite gekürzt statt umgebrochen.
- Kopf- und Fußzeile sind durch Trennlinien abgesetzt.

```
┌──────────────────────────────┐
│ o . . .            [WiFi] MQ │
├──────────────────────────────┤
│ Device 1 (ThermoBeacon)      │
│ Wohnzimmer                   │
│  22.94 C                     │
│                              │
│                     44.94 %  │
│                              │
│ Bat: 2.75V         RSSI: -79 │
├──────────────────────────────┤
│ Update: Mo,23.09.2026 14:35  │
└──────────────────────────────┘
```

Die TFT-Boards nutzen unverändert den bisherigen zweispaltigen Code.

Ob das Layout passt, lässt sich **ohne Flashen** prüfen –
[`tools/layout_check.py`](tools/layout_check.py) liest die Metriken direkt aus
den Adafruit-GFX-Font-Headern und rechnet jede Zeile mit den längsten
realistischen Werten durch:

```sh
python3 tools/layout_check.py      # Exit 0 = passt, 1 = Problem
```

Wird das Layout in `main.cpp` geändert, müssen die Koordinaten im Skript
mitgezogen werden.

#### Zeichnen nur aus dem Loop-Task

Ein Refresh dauert mehrere hundert Millisekunden und braucht viel Stack.
Weder der Tasten-Interrupt noch der BLE-Callback dürfen deshalb selbst
zeichnen – beide setzen nur `displayDirty`, gezeichnet wird in
`serviceDisplay()` aus dem Loop. Zeichnet der BLE-Callback direkt, stirbt das
Gerät reproduzierbar mit `Stack canary watchpoint triggered (BTC_TASK)`,
sobald ein Float über `printf` formatiert wird.

#### Pinbelegung

Das Board ist das [Waveshare
ESP32-S3-ePaper-1.54](https://docs.waveshare.com/ESP32-S3-ePaper-1.54) – ein
Integrationsboard mit fest verlötetem Panel, nichts zu verdrahten. Die Belegung
stammt aus dem Hersteller-Beispiel
([`epaper_config.h`](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54/blob/main/02_Example/ESP-IDF/V2/11_FactoryProgram/components/port_bsp/epaper_config.h));
V1 und V2 sind für das Display identisch belegt.

| Signal | GPIO | Flag |
|---|---|---|
| DC | 10 | `EPD_DC` |
| CS | 11 | `EPD_CS` |
| SCK | 12 | `EPD_SCK` |
| MOSI | 13 | `EPD_MOSI` |
| RST | 9 | `EPD_RST` |
| BUSY | 8 | `EPD_BUSY` |
| **Panel-Spannung** | **6** (aktiv LOW) | `EPD_PWR_PIN` / `EPD_PWR_ON_LEVEL` |

> **Die Spannungsfreigabe ist nicht optional.** Ohne sie bleibt das Panel
> stromlos: GxEPD2 sendet zwar, der BUSY-Pin meldet sofort „fertig", und das
> Display bleibt leer – ohne jede Fehlermeldung. Erkennbar ist das an den
> Refresh-Dauern, die `EPD_DIAG_BAUD` ausgibt (siehe unten).

Weitere Versorgungspfade des Boards, die diese Firmware nicht benötigt:
GPIO 17 = VBAT-Freigabe (aktiv HIGH, für Akkubetrieb), GPIO 42 = Audio-Freigabe
(aktiv LOW).

#### Weitere Flags

| Flag | Bedeutung |
|---|---|
| `EPD_ROTATION` | 0–3; dreht das quadratische Bild je nach Einbaulage |
| `EPD_FULL_REFRESH_EVERY` | nach wie vielen Teil-Refreshes voll aufgefrischt wird |
| `EPD_SPI_HZ` | SPI-Takt (Standard 4 MHz) |
| `EPD_DIAG_BAUD` | ≠0 schaltet die GxEPD2-Diagnose auf Serial frei: Refresh-Dauern in µs und `Busy Timeout!`. Plausibel sind ~1.380.000 µs voll und ~362.000 µs partiell – einstellige Werte heißen, dass das Panel nicht erreicht wird |
| `EPD_PANEL_GDEY0154D67` | Fallback auf die neuere Panel-Revision, falls das Bild stark geistert |

## Bauen & Flashen

[PlatformIO](https://platformio.org/) vorausgesetzt:

```sh
# Bauen
pio run -e lilygo-t-display-s3      # bzw. -e ttgo-t1 / -e esp32-s3-epaper154

# Bauen und flashen (Board angeschlossen)
pio run -e lilygo-t-display-s3 -t upload

# Seriellen Monitor öffnen (115200 Baud)
pio device monitor -e lilygo-t-display-s3
```

## Buttons

| Button | T-Display-S3 | TTGO T-Display | e-Paper-S3 | Funktion |
|---|---|---|---|---|
| Button 1 | GPIO 14 | GPIO 35 | GPIO 18 (PWR) | Screen vorwärts; **beim Boot gedrückt halten → WLAN-Einstellungen zurücksetzen** |
| Button 2 | GPIO 0 | GPIO 0 | GPIO 0 (BOOT) | Screen rückwärts |

Bei der e-Paper-Variante sind das die beiden Taster des Boards, beide aktiv LOW
mit internem Pullup. Die Pegel werden beim Start auf der seriellen Konsole
ausgegeben.

> **Achtung, WLAN-Reset:** Button 1 ist hier der PWR-Taster, mit dem das Board
> auch eingeschaltet wird – beim Boot ist er also häufig noch gedrückt. Damit
> nicht bei jedem Einschalten die WLAN-Zugangsdaten gelöscht werden, verlangt
> die e-Paper-Variante ein bewusstes Weiterhalten: Erst wenn der Taster drei
> Sekunden nach dem Boot *immer noch* gedrückt ist, wird zurückgesetzt.

## WLAN-Einrichtung

Sind keine WLAN-Zugangsdaten gespeichert (oder wurden per Button 1 beim Boot
zurückgesetzt), startet das Gerät einen **offenen Konfig-Accesspoint**:

1. Mit dem WLAN **`BLE2MQTT`** verbinden (kein Passwort).
2. Das Captive-Portal öffnet sich automatisch; falls nicht, im Browser
   **`192.168.4.1`** aufrufen.
3. Ziel-WLAN auswählen, Passwort eintragen, speichern. Das Gerät verbindet
   sich danach automatisch und startet BLE-Scan + MQTT.

## MQTT-Schnittstelle

Der Broker wird in `main.cpp` anhand der verbundenen SSID gewählt (Port 1883).
Die Client-ID lautet `ble2mqtt-<MAC>`.

### Veröffentlichte Werte

Pro Sensor unter `/openhab/in/<device>/`:

`*_temp/state`, `*_hum/state`, `*_bat/state`, `*_battype/state`,
`*_type/state`, `*_rssi/state`, `*_name/state`, `*_fullname/state`,
`*_gateway/state`, `*_lastupdate/state`

### Kommandos

Senden an `/openhab/configuration/<client-id>/cmd`:

| Kommando | Wirkung |
|---|---|
| `restart` | Gerät neu starten |
| `getVersion` | Firmware-Version melden |
| `getIP` | IP / SSID / Broker melden |
| `reconfig` | Sensor-Konfiguration neu anfordern |
| `setScreen+` / `setScreen-` | Screen vor/zurück |
| `getMaxSensor` | Anzahl Sensoren melden |
| `blankScreen` | Display ausschalten |
| `debug` | Debug-Ausgabe umschalten |

## Tools (`tools/`)

- **`BLE2MQTT_gui_oo.py`** – Tkinter-Desktop-GUI zum Überwachen und Steuern der
  Geräte über MQTT.
- **`espconfig.py`** – liefert auf `getconfig`-Anfrage Konfigurationsdateien per
  MQTT aus.
- **`calprint.py`** – Hilfsskript.
- **`layout_check.py`** – prüft das e-Paper-Layout (200×200) rechnerisch gegen
  die echten Font-Metriken, ohne das Board zu flashen.

Python-Abhängigkeit: `paho-mqtt`.

## Lizenz

Siehe Repository.
