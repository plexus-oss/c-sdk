/**
 * @file plexus_hal_arduino.cpp
 * @brief Arduino HAL implementation for Plexus C SDK
 *
 * Supports:
 * - ESP32 (WiFi built-in)
 * - ESP8266 (WiFi built-in)
 * - Arduino with WiFi shield/module
 */

#include "plexus.h"

#if defined(ARDUINO)

#include <Arduino.h>

/* Platform-specific includes */
#if defined(ESP32)
    #include <WiFi.h>
    #include <HTTPClient.h>
    #include <WiFiClientSecure.h>
#elif defined(ESP8266)
    #include <ESP8266WiFi.h>
    #include <ESP8266HTTPClient.h>
    #include <WiFiClientSecure.h>
#else
    /* Generic Arduino - requires WiFi library */
    #include <WiFi.h>
    #if __has_include(<ArduinoHttpClient.h>)
        #include <ArduinoHttpClient.h>
        #define USE_ARDUINO_HTTP_CLIENT 1
    #endif
#endif

extern "C" {

plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
                                   const char* body, size_t body_len) {
    if (!url || !api_key || !body) {
        return PLEXUS_ERR_NULL_PTR;
    }

#if defined(ESP32) || defined(ESP8266)
    /* ESP32/ESP8266 with HTTPClient */
    HTTPClient http;
    WiFiClientSecure client;

    /* Skip certificate verification (for simplicity) */
    /* In production, add proper certificate pinning */
    client.setInsecure();

    if (!http.begin(client, url)) {
        return PLEXUS_ERR_NETWORK;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key", api_key);
    http.setTimeout(PLEXUS_HTTP_TIMEOUT_MS);

    int httpCode = http.POST((uint8_t*)body, body_len);

    plexus_err_t result;
    if (httpCode < 0) {
        /* Connection failed */
        result = PLEXUS_ERR_NETWORK;
    } else if (httpCode >= 200 && httpCode < 300) {
        result = PLEXUS_OK;
    } else if (httpCode == 401) {
        result = PLEXUS_ERR_AUTH;
    } else if (httpCode == 429) {
        result = PLEXUS_ERR_RATE_LIMIT;
    } else if (httpCode >= 500) {
        result = PLEXUS_ERR_SERVER;
    } else {
        result = PLEXUS_ERR_NETWORK;
    }

    http.end();
    return result;

#elif defined(USE_ARDUINO_HTTP_CLIENT)
    /* Generic Arduino with ArduinoHttpClient */
    /* Parse URL to get host and path */
    /* This is a simplified implementation */

    /* TODO: Implement URL parsing and HTTP request */
    (void)body_len;
    return PLEXUS_ERR_HAL;

#else
    /* No HTTP library available */
    (void)body_len;
    return PLEXUS_ERR_HAL;
#endif
}

#if PLEXUS_ENABLE_COMMANDS

plexus_err_t plexus_hal_http_get(const char* url, const char* api_key,
                                  char* response_buf, size_t buf_size,
                                  size_t* response_len) {
    if (!url || !api_key || !response_buf || !response_len) {
        return PLEXUS_ERR_NULL_PTR;
    }

    *response_len = 0;

#if defined(ESP32) || defined(ESP8266)
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();

    if (!http.begin(client, url)) {
        return PLEXUS_ERR_NETWORK;
    }

    http.addHeader("x-api-key", api_key);
    http.setTimeout(PLEXUS_HTTP_TIMEOUT_MS);

    int httpCode = http.GET();

    plexus_err_t result;
    if (httpCode < 0) {
        result = PLEXUS_ERR_NETWORK;
    } else if (httpCode >= 200 && httpCode < 300) {
        String payload = http.getString();
        size_t len = payload.length();
        if (len >= buf_size) len = buf_size - 1;
        memcpy(response_buf, payload.c_str(), len);
        response_buf[len] = '\0';
        *response_len = len;
        result = PLEXUS_OK;
    } else if (httpCode == 401) {
        result = PLEXUS_ERR_AUTH;
    } else if (httpCode == 429) {
        result = PLEXUS_ERR_RATE_LIMIT;
    } else if (httpCode >= 500) {
        result = PLEXUS_ERR_SERVER;
    } else {
        result = PLEXUS_ERR_NETWORK;
    }

    http.end();
    return result;
#else
    (void)buf_size;
    return PLEXUS_ERR_HAL;
#endif
}

#endif /* PLEXUS_ENABLE_COMMANDS */

uint64_t plexus_hal_get_time_ms(void) {
#if defined(ESP32) || defined(ESP8266)
    /* ESP devices have NTP support */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
#else
    /* No RTC - return 0 to use server timestamp */
    return 0;
#endif
}

uint32_t plexus_hal_get_tick_ms(void) {
    return (uint32_t)millis();
}

void plexus_hal_delay_ms(uint32_t ms) {
    delay(ms);
}

void plexus_hal_log(const char* fmt, ...) {
#if PLEXUS_DEBUG
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
#else
    (void)fmt;
#endif
}

} /* extern "C" */

/* Arduino-specific helper class */
class PlexusClient {
public:
    PlexusClient(const char* apiKey, const char* sourceId)
        : _client(nullptr) {
        _client = plexus_init(apiKey, sourceId);
    }

    ~PlexusClient() {
        if (_client) {
            plexus_free(_client);
        }
    }

    bool isValid() const { return _client != nullptr; }

    plexus_err_t sendNumber(const char* metric, double value) {
        return plexus_send_number(_client, metric, value);
    }

    plexus_err_t sendString(const char* metric, const char* value) {
#if PLEXUS_ENABLE_STRING_VALUES
        return plexus_send_string(_client, metric, value);
#else
        (void)metric; (void)value;
        return PLEXUS_ERR_HAL;
#endif
    }

    plexus_err_t sendBool(const char* metric, bool value) {
#if PLEXUS_ENABLE_BOOL_VALUES
        return plexus_send_bool(_client, metric, value);
#else
        (void)metric; (void)value;
        return PLEXUS_ERR_HAL;
#endif
    }

    plexus_err_t flush() {
        return plexus_flush(_client);
    }

    uint16_t pendingCount() const {
        return plexus_pending_count(_client);
    }

    void clear() {
        plexus_clear(_client);
    }

    plexus_err_t setEndpoint(const char* endpoint) {
        return plexus_set_endpoint(_client, endpoint);
    }

    plexus_err_t setFlushInterval(uint32_t interval_ms) {
        return plexus_set_flush_interval(_client, interval_ms);
    }

    plexus_err_t setFlushCount(uint16_t count) {
        return plexus_set_flush_count(_client, count);
    }

    plexus_err_t tick() {
        return plexus_tick(_client);
    }

    plexus_client_t* handle() { return _client; }

private:
    plexus_client_t* _client;
};

#endif /* ARDUINO */
