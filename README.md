# ESP32BLE2MQTT

BLE-zu-MQTT-Bridge für ESP32 mit TFT- oder e-Paper-Display. Das Gerät scannt per Bluetooth
Low Energy nach Sensoren, zeigt deren Werte auf dem Display an und publiziert
sie an einen MQTT-Broker (Anbindung an openHAB).

Aktuelle Version: **V3.0**

## Funktionen

- **BLE-Scan** nach unterstützten Sensoren (siehe unten), inkl. Auswertung von
  Temperatur, Luftfeuchte, Batterie und RSSI.
- **Display** mit zwei Ansichten, per Hardware-Buttons umblätterbar:
  - Datum/Uhrzeit-Übersicht (Screen 0)
  - je ein Detail-Screen pro Sensor
- **MQTT-Publish** der Messwerte unter `/openhab/in/<device>/...`.
- **Alarme** pro Sensor auf Temperatur oder Feuchte, per MQTT konfigurierbar;
  auf der e-Paper-Variante mit Ton und Alarmbild, quittierbar per Tastendruck.
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
| **SHT3** (intern) | fest verbaut, kein BLE | Temp, Feuchte, Systemspannung, WLAN-RSSI |

Der interne Sensor der e-Paper-Variante wird wie ein BLE-Sensor behandelt:
gleiche Struktur, gleicher Screen, gleiche MQTT-Topics. MAC und `device` werden
aus der ESP-ID gebildet, `name` ist `SHT3`, `fullname` `BLE2MQTT Intern`. Ein
`getconfig` löst er nicht aus – sein Name steht fest. Die sonst leeren Felder
sind sinnvoll belegt: **RSSI** ist die WLAN-Feldstärke des Gateways selbst,
**Batterie** die Systemspannung (ADC1_CH3 an GPIO 4, 1:2-Teiler).

> **Eigenerwärmung beachten.** Der SHTC3 sitzt neben dem ESP32 auf derselben
> Platine. Bei aktivem WLAN und BLE wurden **33–34 °C** gemessen, während der
> Raum deutlich kühler war. Der Wert taugt als Geräte-, nicht als
> Raumtemperatur. `SHTC3_TEMP_OFFSET` korrigiert ihn; der Default ist bewusst
> `0`, damit die Messung nicht still geschönt wird.

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
| `INTERNAL_SHTC3` | schaltet den verbauten SHTC3 als zusätzlichen Sensor frei |
| `SHTC3_SDA` / `SHTC3_SCL` | I²C-Pins (47 / 48), Bus geteilt mit RTC `0x51` und Codec `0x18` |
| `SHTC3_TEMP_OFFSET` | Korrektur in Kelvin gegen Eigenerwärmung, Default `0.0f` |
| `INTERNAL_VBAT_ADC` | ADC-Pin der Systemspannung (GPIO 4) |
| `BOARD_PWR_PIN` / `BOARD_PWR_ON_LEVEL` | Versorgungsschiene der Peripherie (GPIO 17, aktiv HIGH) |
| `AUDIO_PWR_PIN` / `AUDIO_PA_PIN` | Audio-Rail und Endstufe, beim Start definiert gesetzt |
| `AUDIO_ALARM` | schaltet Alarm- und Startton über ES8311 + NS4150 frei |
| `AUDIO_ALARM_REPEATS` | Anzahl der Doppeltöne je Alarm (Standard 10) |
| `AUDIO_VOLUME` | ES8311-Register 0x32, dB = −95,5 + 0,5 × Wert |
| `ALARM_TOPIC` | Standard-Topic, per Config-Zeile `alarmtopic:` überschreibbar |
| `LONG_PRESS_MS` | Schwelle für den langen Druck (Standard 1500 ms) |

> **I²C-Bus beim Start.** Bleiben `AUDIO_PWR_PIN` und `AUDIO_PA_PIN` offen, kann
> der gemeinsame Bus in einen Zustand geraten, in dem SDA dauerhaft LOW bleibt
> und kein Gerät mehr antwortet. Deshalb werden beide beim Start definiert
> gesetzt – der Hersteller macht das ebenso. Zusätzlich löst der Treiber einen
> hängenden Bus per Taktflanken und setzt `Wire.setTimeOut()`, damit ein toter
> Bus die Firmware nicht blockiert.

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
| Button 2 | GPIO 0 | GPIO 0 | GPIO 0 (BOOT) | Screen rückwärts; **lang drücken → Alarmtest** (nur e-Paper) |

Bei der e-Paper-Variante hängen die Interrupts auf `CHANGE` statt `RISING` –
nur so lässt sich die Druckdauer messen und ein langer Druck von einem kurzen
unterscheiden. Die ISR misst ausschließlich und setzt Flags; geblättert,
gemeldet und getönt wird im Loop-Task. Steht ein Alarm an, quittiert jeder
Tastendruck, ohne den Screen zu wechseln.

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

### Alarme

Alarme werden über dieselbe `getconfig`-Antwort konfiguriert wie die
Sensornamen – eine Zeile je Regel:

```
name:910f00000121:Kuehlschrank
alarm:910f00000121:temp:gt:10
```

| Feld | Werte |
|---|---|
| Größe | `temp` oder `hum` |
| Operator | `gt` (größer) oder `lt` (kleiner) |
| Grenzwert | Zahl, z. B. `10` oder `-5.5` |

Je Sensor ist eine Regel für Temperatur **und** eine für Feuchte möglich. Die
Auswertung läuft einmal pro Messzyklus, nach dem MQTT-Publish.

**Eine Regel löst nur einmal aus.** Scharf wird sie erst wieder, wenn der Wert
in den gültigen Bereich zurückkehrt – sonst würde sie im Minutentakt weiter
alarmieren. Das Anwenden ist idempotent: Die Config trifft nach jedem
`getconfig` erneut ein, eine unveränderte Regel lässt einen laufenden Alarm
deshalb in Ruhe.

Das Ziel-Topic ist über eine Config-Zeile änderbar, Standard `/openhab/alarm`:

```
alarmtopic:/openhab/meinalarm
```

Veröffentlicht wird `<fullname>: <größe> <operator> <grenzwert> = <istwert>`,
also z. B. `Kuehlschrank: temp gt 10.0 = 12.34`. Beim Quittieren geht
**`alarm bestätigt`** auf dasselbe Topic. Beide Meldungen sind `retained`: Ein
neu hinzukommender Abonnent sieht dadurch den aktuellen Zustand und nicht einen
längst erledigten Alarm als vermeintlich offenen.

Auf dem Sensor-Screen markiert eine **Glocke** den Wert, für den eine Regel
konfiguriert ist – hinter der linksbündigen Temperatur, vor der rechtsbündigen
Feuchte. Sie ist als **Umriss** gezeichnet, solange der Wert im Rahmen liegt,
und **ausgefüllt**, solange die Regel verletzt ist. Damit sieht man auch ohne
Alarmbild, welcher Wert gerade aus dem Rahmen läuft.

#### Alarmtest

Ein **langer Druck (≥ 1,5 s) auf Button 2 (BOOT)** löst einen Testalarm für den
gerade angezeigten Sensor aus. Er nimmt denselben Weg wie ein echter Alarm –
inklusive MQTT-Publish –, sodass sich die Kette bis zur Benachrichtigung prüfen
lässt. Gemeldet wird `<fullname>: Alarmtest = <temp> C / <hum> %`.

> Der lange Druck liegt bewusst **nicht** auf Button 1: Das ist der PWR-Taster
> des Boards, dessen Hardware-Latch beim Halten abschaltet.

Auf Screen 0 (Datum/Uhrzeit) passiert nichts, dort ist kein Sensor. Die Dauer
ist über `LONG_PRESS_MS` einstellbar.

Auf der e-Paper-Variante zusätzlich:

- **Alarmton**, standardmäßig 10 Doppeltöne (`AUDIO_ALARM_REPEATS`)
- **Alarmbild** mit invertiertem Kopf, damit es sich von den Sensor-Screens
  unterscheidet; es bleibt stehen und wird von Uhrzeit-Updates nicht
  überschrieben
- **Quittieren mit einer beliebigen Taste** – der erste Druck bestätigt und
  bricht den laufenden Ton sofort ab, ohne den Screen zu wechseln. Die
  MQTT-Kommandos `setScreen+` / `setScreen-` quittieren ebenfalls, da sie
  denselben Pfad nutzen.
- **Startton** beim Hochfahren

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
