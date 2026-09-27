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

## Zugangsschutz

Weboberflaeche und API sind mit HTTP Basic Auth geschuetzt (`API_AUTH_ENABLED`
in `include/config.h`). Benutzername beliebig, Passwort: `API_PASSWORD_DEFAULT`
in `include/config.h` (aktuell `boschtank`); bei leerem Wert wird ein
zufaelliges Passwort erzeugt und im NVS (`api_pass`) gespeichert. Aktives
Passwort steht im seriellen Startprotokoll
(`🔑 Weboberflaeche: ... Passwort: ...`). Skripte nutzen `$env:BOSCH_TANK_PASS`.

Notaus-Meldungen gehen per Telegram raus (`components/main/telegram.c`,
Vorbild katzenbrunnen). Token und Chat-ID liegen im NVS (`tg_token`,
`tg_chat`) und werden in den Einstellungen der Weboberflaeche gesetzt.

## Hardware-Regeln (am Geraet belegt)

- **TOF-Sensor (VL6150X): Gehaeuse/Metall darf ihn nicht beruehren** - beim
  Zusammenbau am 2026-09-27 bekam er Metallkontakt, Folge waren dauerhaft
  "0 cm"/keine Messung. Mit Isolierung (Klebeband) sofort wieder sauber.
  Schwellen nur 1..17 cm; ab ~18 cm misst er unzuverlaessig, 25 cm ist der
  Festwert fuer "keine Messung". Details: Skill `vl6180x-tof`.
- **Sensorausfall darf keine Firmware zurueckrollen**: Der OTA-Gesundheitscheck
  prueft nur noch Tasks (Sensor wird nur gemeldet, nicht bestraft) - sonst
  rollte ein streikender Sensor jede neue Firmware zurueck (belegt 2026-09-27).

Schwesterprojekt: `../delonghi-tank` (gleiche Grundarchitektur, andere Hardware).
