#!/usr/bin/env python3
"""
Prueft das e-Paper-Layout (200x200) rechnerisch gegen die echten Font-Metriken,
ohne das Board zu flashen.

Hintergrund: Auf dem 1.54"-Panel faellt ein zu breiter Text nicht auf - er wird
umgebrochen oder abgeschnitten, und bis man das sieht, sind Build, Flash und ein
Refresh vergangen. Das Skript liest die xAdvance-Werte direkt aus den
Adafruit-GFX-Font-Headern und rechnet fuer jede Zeile aus displayScreen() und
displayDateTime() nach, ob sie mit den laengsten realistischen Werten in 200 px
passt und ob sich zwei Zeilen vertikal ueberschneiden.

    python3 tools/layout_check.py        # Exit 0 = alles passt, 1 = Problem

WICHTIG: Die Koordinaten und Texte hier sind eine Nachbildung von
src/main.cpp (Abschnitt "#ifdef EPAPER"). Wird das Layout dort geaendert,
muss es hier mitgezogen werden - sonst prueft das Skript etwas anderes,
als das Geraet zeichnet.

Voraussetzung: 'pio run -e esp32-s3-epaper154' wurde mindestens einmal
ausgefuehrt, damit die Font-Header unter .pio/libdeps/ liegen.
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def font_dir():
    hits = glob.glob(os.path.join(ROOT, ".pio", "libdeps", "*", "Adafruit GFX Library", "Fonts"))
    if not hits:
        sys.exit("Font-Header nicht gefunden. Erst 'pio run -e esp32-s3-epaper154' ausfuehren.")
    return hits[0]


FDIR = font_dir()


def load(name):
    """Parst einen GFXfont-Header: xAdvance/Hoehe/yOffset je Glyph."""
    with open(os.path.join(FDIR, name + ".h")) as fh:
        src = fh.read()
    gl = src.split("Glyphs[] PROGMEM = {")[1].split("};")[0]
    gl = re.sub(r"//.*", "", gl)
    glyphs = []
    for row in re.findall(r"\{([^}]*)\}", gl):
        v = [int(x.strip(), 0) for x in row.split(",")[:6]]
        glyphs.append({"w": v[1], "h": v[2], "xAdv": v[3], "xo": v[4], "yo": v[5]})
    st = src.split("const GFXfont")[1].split("};")[0]
    nums = re.findall(r"0x[0-9A-Fa-f]+|(?<![\w.])\d+(?![\w])", st)
    return {"g": glyphs, "first": int(nums[-3], 0), "last": int(nums[-2], 0)}


def adv(f, s):
    """Vorschubbreite eines Strings. f=None ist die GLCD-Schrift (6 px/Zeichen)."""
    if f is None:
        return 6 * len(s)
    return sum(f["g"][ord(c) - f["first"]]["xAdv"]
               for c in s if f["first"] <= ord(c) <= f["last"])


def ascent(f):
    """Oberkante -> Grundlinie; EpdDisplay ermittelt das per getTextBounds("Ag")."""
    return 0 if f is None else max(-f["g"][ord(c) - f["first"]]["yo"] for c in "Ag")


def height(f):
    return 8 if f is None else max(f["g"][ord(c) - f["first"]]["h"] for c in "Ag0")


# Schriftstufen wie in EpdDisplay::setTextFont()
F0 = None                            # Font 0 - GLCD 5x7
F2 = load("FreeSans9pt7b")           # Font 2 - Fliesstext
F4 = load("FreeSansBold12pt7b")      # Font 4 - Einheiten, Nachkomma
F6 = load("FreeSansBold24pt7b")      # Font 6 - grosse Messwerte

W, H, M = 200, 200, 4                # SCREEN_WIDTH, SCREEN_HEIGHT, MARGIN_X
maxW = W - 2 * M
fail = 0


def problem(msg):
    global fail
    fail += 1
    print("       <== %s" % msg)


def fit(font, text, maxw):
    """Bildet epdFit() aus main.cpp nach."""
    while len(text) > 1 and adv(font, text) > maxw:
        text = text[:-1]
    return text


def line(label, y, font, text, x=M, size=1, fitted=False):
    if fitted:
        text = fit(font, text, maxW)
    w = adv(font, text) * size
    bot = y + (ascent(font) if font else height(font)) * size
    print("  %-7s y=%3d..%3d  x=%3d..%3d  %s" % (label, y, bot, x, x + w, repr(text)))
    if x + w > W:
        problem("ZU BREIT (%d px ueber)" % (x + w - W))
    if bot > H:
        problem("ZU HOCH")
    return bot


def gap(label, upper_bottom, lower_top, minimum=8):
    d = lower_top - upper_bottom
    print("       -> vertikaler Abstand %s: %d px" % (label, d))
    if d < minimum:
        problem("UEBERLAPPUNG")


print("=== displayScreen (laengste realistische Werte) ===")
line("dots", 2, F0, "o" + "." * 9, size=2)
line("wifi+MQ", 0, F2, "MQ", x=W - 30)
line("device", 25, F0, "Device 10 (ThermoBeacon)", fitted=True)
line("name", 36, F2, "a4:c1:38:aa:bb:cc", fitted=True)

# Temperatur links oben, Feuchte rechtsbuendig darunter - diagonal versetzt
tw = adv(F6, "-12") + adv(F4, ".94 C")
t_bot = 54 + ascent(F6)
print("  temp    y= 54..%3d  x=%3d..%3d  '-12' + '.94 C'" % (t_bot, M, M + tw))
if M + tw > W:
    problem("ZU BREIT (%d px ueber)" % (M + tw - W))

hw = adv(F6, "100") + adv(F4, ".00 %")
h_bot = 108 + ascent(F6)
print("  hum     y=108..%3d  x=%3d..%3d  '100' + '.00 %%' (rechtsbuendig)"
      % (h_bot, W - M - hw, W - M))
if W - M - hw < M:
    problem("ZU BREIT")
gap("temp/hum", t_bot, 108)

# Batterie und RSSI teilen sich eine Zeile: links bzw. rechts buendig
bw, rw = adv(F2, "Bat: 2.75V"), adv(F2, "RSSI: -100")
b_bot = 154 + ascent(F2)
print("  bat     y=154..%3d  x=%3d..%3d  'Bat: 2.75V'" % (b_bot, M, M + bw))
print("  rssi    y=154..%3d  x=%3d..%3d  'RSSI: -100' (rechtsbuendig)"
      % (b_bot, W - M - rw, W - M))
free = (W - M - rw) - (M + bw)
print("       -> horizontale Luecke: %d px" % free)
if free < 4:
    problem("KOLLISION")
gap("hum/bat", h_bot, 154)

line("footer", 184, F0, "Update: Mo,23.09.2026 14:35", fitted=True)

print("\n=== displayDateTime ===")
line("device", 25, F0, "Device: ble2mqtt-AC276ED304DC", fitted=True)
for label, y, font, text in (("clock", 44, F6, "14:35"), ("date", 90, F4, "Mo,23.09.2026")):
    w = adv(font, text)
    print("  %-7s y=%3d..%3d  x=%3d..%3d  %s (zentriert)"
          % (label, y, y + ascent(font), (W - w) // 2, (W + w) // 2, repr(text)))
    if w > maxW:
        problem("ZU BREIT (%d px ueber)" % (w - maxW))
line("ssid", 122, F2, "SSID: MeinWLAN-2.4GHz", fitted=True)
line("ip", 140, F2, "IP:   192.168.100.100", fitted=True)
line("count", 158, F2, "Sensoren: 12")
line("footer", 184, F0, "BLE2MQTT V2.5", fitted=True)

print("\n%s" % ("ALLES PASST" if fail == 0 else "%d PROBLEM(E)" % fail))
sys.exit(1 if fail else 0)
