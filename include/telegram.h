#ifndef TELEGRAM_H
#define TELEGRAM_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * Telegram-Benachrichtigungen ueber die Bot-API.
 *
 * Bot-Token und Chat-ID liegen im NVS (gleicher Namensraum wie das Projekt)
 * und werden ueber die Weboberflaeche gesetzt. Der Versand laeuft in einer
 * eigenen Aufgabe - die aufrufende Stelle wird nie blockiert.
 *
 * Vorbild: Projekt "katzenbrunnen" (dort seit laengerem im Einsatz).
 */

/** Laedt Token und Chat-ID aus dem NVS und legt Puffer/Mutex an. */
esp_err_t telegram_init(void);

/** Startet die Sendeaufgabe (einmal nach telegram_init aufrufen). */
esp_err_t telegram_start(void);

/** true, wenn Token und Chat-ID gesetzt sind. */
bool telegram_is_configured(void);

/** true, wenn ein Token gespeichert ist (Token wird nie ausgegeben). */
bool telegram_has_token(void);

/** Kopiert die Chat-ID zum Anzeigen (Token bleibt geheim). */
void telegram_get_chat_id(char *buf, size_t size);

/** Speichert Token und/oder Chat-ID im NVS (NULL = unveraendert lassen). */
esp_err_t telegram_save_config(const char *token, const char *chat_id);

/** Reiht eine Nachricht ein. Blockiert nicht. false = nicht angenommen. */
bool telegram_notify(const char *message);

/** Sendet sofort und wartet auf die Antwort (fuer Testnachricht). */
esp_err_t telegram_send_now(const char *message);

#endif // TELEGRAM_H
