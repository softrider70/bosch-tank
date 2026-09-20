# Copilot-Instruktionen – bosch-tank

**Projekt:** Automatisches Wassertank-Management fuer eine Kaffeemaschine (Bosch).
ESP32-Firmware mit Fuellstandsmessung per ToF-Sensor (VL6150X/VL6180X),
Ventilsteuerung ueber ein ILN44Z-Relaismodul (12 V, Schaltung gegen Masse,
1 Ohm zur Strombegrenzung), WLAN-Weboberflaeche, NVS-Konfiguration und Notaus.

## Wichtig fuer die Zusammenarbeit

- **Sprache ist Deutsch** - Antworten, Kommentare und Doku.
- Kurze Saetze, Fachwoerter erklaeren, keine Vermutungen: pruefen statt raten.
- Nach jeder Aenderung **bauen und flashen**, dann die Ausgabe im Log pruefen.

## Bauen und flashen

ESP-IDF (6.1) vorher aktivieren, PATH danach aufraeumen, dann im Projektordner:
`idf.py -p COMx flash`  (COM-Nummer kann sich beim Umstecken aendern)

## Wo was steht

- `include/config.h` - alle Pins und Parameter
- `PROJECT.md` - Projektspezifikation
- `README.md` - Bauen, OTA-Ablauf, Fehlersuche
- `flash.md` - Anleitung zum Flashen per USB
- `README_OTA_SAFETY.md` - Sicherheit bei Firmware-Updates

Schwesterprojekt: `../delonghi-tank` (gleiche Grundarchitektur, andere Hardware).
