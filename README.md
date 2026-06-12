# ESP32BLE2MQTT

BLE-zu-MQTT-Bridge für ESP32 mit TFT-Display. Das Gerät scannt per Bluetooth
Low Energy nach Sensoren, zeigt deren Werte auf dem Display an und publiziert
sie an einen MQTT-Broker (Anbindung an openHAB).

Aktuelle Version: **V2.4**

## Funktionen

- **BLE-Scan** nach unterstützten Sensoren (siehe unten), inkl. Auswertung von
  Temperatur, Luftfeuchte, Batterie und RSSI.
- **TFT-Display** mit zwei Ansichten, per Hardware-Buttons umblätterbar:
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
liegt vollständig in den `build_flags` (`USER_SETUP_LOADED`), eine manuelle
Anpassung von `User_Setup_Select.h` im TFT_eSPI-libdeps-Ordner ist **nicht**
nötig.

| Env | Board | Display | Besonderheit |
|---|---|---|---|
| `lilygo-t-display-s3` | LilyGo T-Display-S3 (ESP32-S3) | ST7789 320×170, 8-Bit parallel (Setup206) | Upload über USB-JTAG mit `--no-stub` |
| `ttgo-t1` | TTGO T-Display (ESP32) | ST7789 240×135 SPI (Setup25) | — |

Das Display-Layout passt sich je Board an die Auflösung an (der S3 nutzt die
größere Fläche inkl. zentrierter Datum/Uhrzeit-Anzeige und Lastupdate-Zeile).

## Bauen & Flashen

[PlatformIO](https://platformio.org/) vorausgesetzt:

```sh
# Bauen
pio run -e lilygo-t-display-s3      # bzw. -e ttgo-t1

# Bauen und flashen (Board angeschlossen)
pio run -e lilygo-t-display-s3 -t upload

# Seriellen Monitor öffnen (115200 Baud)
pio device monitor -e lilygo-t-display-s3
```

## Buttons

| Button | T-Display-S3 | TTGO T-Display | Funktion |
|---|---|---|---|
| Button 1 | GPIO 14 | GPIO 35 | Screen vorwärts; **beim Boot gedrückt halten → WLAN-Einstellungen zurücksetzen** |
| Button 2 | GPIO 0 | GPIO 0 | Screen rückwärts |

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

Python-Abhängigkeit: `paho-mqtt`.

## Lizenz

Siehe Repository.
