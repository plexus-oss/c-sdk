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
                                   const char* user_agent,
                                   const char* body, size_t body_len) {
    if (!url || !api_key || !body) {
        return PLEXUS_ERR_NULL_PTR;
    }

#if defined(ESP32) || defined(ESP8266)
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
    if (user_agent) {
        http.addHeader("User-Agent", user_agent);
    }
    http.setTimeout(PLEXUS_HTTP_TIMEOUT_MS);

    int httpCode = http.POST((uint8_t*)body, body_len);

    plexus_err_t result;
    if (httpCode < 0) {
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
    (void)user_agent;
    (void)body_len;
    return PLEXUS_ERR_HAL;

#else
    (void)user_agent;
    (void)body_len;
    return PLEXUS_ERR_HAL;
#endif
}

#if PLEXUS_ENABLE_COMMANDS

plexus_err_t plexus_hal_http_get(const char* url, const char* api_key,
                                  const char* user_agent,
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
    if (user_agent) {
        http.addHeader("User-Agent", user_agent);
    }
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
    (void)user_agent;
    (void)buf_size;
    return PLEXUS_ERR_HAL;
#endif
}

#endif /* PLEXUS_ENABLE_COMMANDS */

uint64_t plexus_hal_get_time_ms(void) {
#if defined(ESP32) || defined(ESP8266)
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
#else
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

/* PlexusClient C++ wrapper is defined in plexus.h (inside #ifdef __cplusplus) */

#endif /* ARDUINO */
