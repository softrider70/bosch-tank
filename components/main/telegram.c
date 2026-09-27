// Telegram-Benachrichtigungen (Bot-API).
//
// Abgeleitet vom Projekt "katzenbrunnen" (dort im Einsatz). Vereinfacht:
// kein Nacht-Modus, kein separates Ein/Aus - aktiv, sobald Token und Chat-ID
// gespeichert sind.
//
// Token und Chat-ID liegen im NVS (Namensraum NVS_NAMESPACE) und werden ueber
// die Weboberflaeche gesetzt. Der Versand laeuft in eigener Aufgabe, damit
// Notaus-Ausloeser (Taster, Weboberflaeche) nie auf das Netz warten.

#include "telegram.h"
#include "config.h"

#include "nvs.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "telegram";

#define TG_API_URL              "https://api.telegram.org/bot"
#define TG_SEND_ENDPOINT        "/sendMessage"
#define TG_MSG_TEXT_LEN         192     // Nutztext einer Nachricht
#define TG_QUEUE_LEN            4       // wartende Nachrichten
#define TG_RETRY_DELAY_MS       2000    // Pause vor dem zweiten Versuch

static char tg_bot_token[TG_TOKEN_MAX_LEN] = {0};
static char tg_chat_id[TG_CHAT_MAX_LEN] = {0};
static SemaphoreHandle_t tg_mutex = NULL;
static QueueHandle_t tg_queue = NULL;

/**
 * @brief Escaped einen Text fuer die JSON-Nutzlast.
 */
static void tg_json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 2 < dst_size; i++) {
        switch (src[i]) {
            case '"':  dst[j++] = '\\'; dst[j++] = '"';  break;
            case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
            case '\n': dst[j++] = '\\'; dst[j++] = 'n';  break;
            case '\r': dst[j++] = '\\'; dst[j++] = 'r';  break;
            case '\t': dst[j++] = '\\'; dst[j++] = 't';  break;
            default:   dst[j++] = src[i];                break;
        }
    }
    dst[j] = '\0';
}

static esp_err_t tg_http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ERROR) {
        ESP_LOGW(TAG, "HTTP-Fehler beim Senden");
    }
    return ESP_OK;
}

/**
 * @brief Laedt Token und Chat-ID aus dem NVS.
 */
static void tg_load_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "NVS nicht lesbar - Telegram bleibt unkonfiguriert");
        return;
    }

    size_t len = sizeof(tg_bot_token);
    if (nvs_get_str(handle, NVS_KEY_TG_TOKEN, tg_bot_token, &len) != ESP_OK) {
        tg_bot_token[0] = '\0';
    }

    len = sizeof(tg_chat_id);
    if (nvs_get_str(handle, NVS_KEY_TG_CHAT, tg_chat_id, &len) != ESP_OK) {
        tg_chat_id[0] = '\0';
    }

    nvs_close(handle);
}

esp_err_t telegram_init(void)
{
    if (tg_mutex == NULL) {
        tg_mutex = xSemaphoreCreateMutex();
        if (tg_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (tg_queue == NULL) {
        tg_queue = xQueueCreate(TG_QUEUE_LEN, TG_MSG_TEXT_LEN);
        if (tg_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    tg_load_from_nvs();

    if (tg_bot_token[0] != '\0' && tg_chat_id[0] != '\0') {
        ESP_LOGI(TAG, "Telegram bereit (Chat-ID: %s)", tg_chat_id);
    } else {
        ESP_LOGW(TAG, "Telegram nicht eingerichtet (Token und Chat-ID in der Weboberflaeche eintragen)");
    }
    return ESP_OK;
}

bool telegram_is_configured(void)
{
    bool ready = false;
    if (tg_mutex != NULL && xSemaphoreTake(tg_mutex, portMAX_DELAY) == pdTRUE) {
        ready = (tg_bot_token[0] != '\0' && tg_chat_id[0] != '\0');
        xSemaphoreGive(tg_mutex);
    }
    return ready;
}

bool telegram_has_token(void)
{
    bool has = false;
    if (tg_mutex != NULL && xSemaphoreTake(tg_mutex, portMAX_DELAY) == pdTRUE) {
        has = (tg_bot_token[0] != '\0');
        xSemaphoreGive(tg_mutex);
    }
    return has;
}

void telegram_get_token(char *buf, size_t size)
{
    if (buf == NULL || size == 0) {
        return;
    }
    buf[0] = '\0';
    if (tg_mutex != NULL && xSemaphoreTake(tg_mutex, portMAX_DELAY) == pdTRUE) {
        strncpy(buf, tg_bot_token, size - 1);
        buf[size - 1] = '\0';
        xSemaphoreGive(tg_mutex);
    }
}

void telegram_get_chat_id(char *buf, size_t size)
{
    if (buf == NULL || size == 0) {
        return;
    }
    buf[0] = '\0';
    if (tg_mutex != NULL && xSemaphoreTake(tg_mutex, portMAX_DELAY) == pdTRUE) {
        strncpy(buf, tg_chat_id, size - 1);
        buf[size - 1] = '\0';
        xSemaphoreGive(tg_mutex);
    }
}

esp_err_t telegram_save_config(const char *token, const char *chat_id)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS nicht beschreibbar: %s", esp_err_to_name(ret));
        return ret;
    }

    if (token != NULL && token[0] != '\0') {
        if (strlen(token) >= TG_TOKEN_MAX_LEN) {
            nvs_close(handle);
            ESP_LOGE(TAG, "Token zu lang (max %d Zeichen)", TG_TOKEN_MAX_LEN - 1);
            return ESP_ERR_INVALID_ARG;
        }
        ret = nvs_set_str(handle, NVS_KEY_TG_TOKEN, token);
        if (ret == ESP_OK) {
            xSemaphoreTake(tg_mutex, portMAX_DELAY);
            strncpy(tg_bot_token, token, sizeof(tg_bot_token) - 1);
            tg_bot_token[sizeof(tg_bot_token) - 1] = '\0';
            xSemaphoreGive(tg_mutex);
            ESP_LOGI(TAG, "Bot-Token gespeichert");
        }
    }

    if (ret == ESP_OK && chat_id != NULL && chat_id[0] != '\0') {
        if (strlen(chat_id) >= TG_CHAT_MAX_LEN) {
            nvs_close(handle);
            ESP_LOGE(TAG, "Chat-ID zu lang (max %d Zeichen)", TG_CHAT_MAX_LEN - 1);
            return ESP_ERR_INVALID_ARG;
        }
        ret = nvs_set_str(handle, NVS_KEY_TG_CHAT, chat_id);
        if (ret == ESP_OK) {
            xSemaphoreTake(tg_mutex, portMAX_DELAY);
            strncpy(tg_chat_id, chat_id, sizeof(tg_chat_id) - 1);
            tg_chat_id[sizeof(tg_chat_id) - 1] = '\0';
            xSemaphoreGive(tg_mutex);
            ESP_LOGI(TAG, "Chat-ID gespeichert");
        }
    }

    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t telegram_send_now(const char *message)
{
    char token[TG_TOKEN_MAX_LEN];
    char chat_id[TG_CHAT_MAX_LEN];

    if (tg_mutex == NULL || message == NULL || message[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    // Kopie unter Mutex - waehrend des Sendens bleibt der Mutex frei.
    xSemaphoreTake(tg_mutex, portMAX_DELAY);
    strncpy(token, tg_bot_token, sizeof(token) - 1);
    token[sizeof(token) - 1] = '\0';
    strncpy(chat_id, tg_chat_id, sizeof(chat_id) - 1);
    chat_id[sizeof(chat_id) - 1] = '\0';
    xSemaphoreGive(tg_mutex);

    if (token[0] == '\0' || chat_id[0] == '\0') {
        ESP_LOGW(TAG, "Nicht eingerichtet - Nachricht nicht gesendet");
        return ESP_ERR_INVALID_STATE;
    }

    char url[192];
    snprintf(url, sizeof(url), "%s%s%s", TG_API_URL, token, TG_SEND_ENDPOINT);

    char escaped[TG_MSG_TEXT_LEN * 2];
    tg_json_escape(message, escaped, sizeof(escaped));

    char json_body[TG_MSG_TEXT_LEN * 2 + TG_CHAT_MAX_LEN + 32];
    int json_len = snprintf(json_body, sizeof(json_body),
                            "{\"chat_id\":\"%s\",\"text\":\"%s\"}", chat_id, escaped);
    if (json_len >= (int)sizeof(json_body)) {
        ESP_LOGE(TAG, "Nachricht zu lang");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = tg_http_event_handler,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_body, json_len);

    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            ESP_LOGI(TAG, "Nachricht gesendet");
        } else {
            ESP_LOGE(TAG, "Telegram meldet HTTP %d (Token oder Chat-ID falsch?)", status);
            ret = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Senden fehlgeschlagen: %s", esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    return ret;
}

bool telegram_notify(const char *message)
{
    if (tg_queue == NULL || message == NULL || message[0] == '\0') {
        return false;
    }
    if (!telegram_is_configured()) {
        ESP_LOGW(TAG, "Nicht eingerichtet - Meldung nicht gesendet");
        return false;
    }

    char buf[TG_MSG_TEXT_LEN];
    strncpy(buf, message, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    if (xQueueSend(tg_queue, buf, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Sendewarteschlange voll - Meldung verworfen");
        return false;
    }
    return true;
}

/**
 * @brief Sendeaufgabe: holt Nachrichten aus der Warteschlange und sendet sie.
 * Bei Fehlschlag ein zweiter Versuch, danach wird die Meldung verworfen.
 */
static void telegram_task(void *pvParameters)
{
    char msg[TG_MSG_TEXT_LEN];

    for (;;) {
        if (xQueueReceive(tg_queue, msg, portMAX_DELAY) == pdTRUE) {
            if (telegram_send_now(msg) != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(TG_RETRY_DELAY_MS));
                telegram_send_now(msg);
            } else {
                vTaskDelay(pdMS_TO_TICKS(500));  // Telegram begrenzt die Rate
            }
        }
    }
}

esp_err_t telegram_start(void)
{
    if (tg_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t created = xTaskCreatePinnedToCore(
        telegram_task,
        "telegram_task",
        TASK_STACK_TELEGRAM,
        NULL,
        TASK_PRIO_TELEGRAM,
        NULL,
        TASK_CORE_NETWORK
    );
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Sendeaufgabe konnte nicht gestartet werden");
        return ESP_FAIL;
    }
    return ESP_OK;
}
