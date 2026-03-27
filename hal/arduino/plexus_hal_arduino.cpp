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

/* ISRG Root X1 — Let's Encrypt root CA, valid until 2035-06-04.
 * Used for TLS certificate verification against api.plexus.dev. */
static const char PLEXUS_ISRG_ROOT_X1[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogZiUvsNG4YetYd5LLjv0kne/e1Oqh1GlR/IQkaBNR2Bmxh
xCz+d3BXZAXA6YEAIXmMFX8cXg7YqeTJuhhr42cBGS8KPXM7FjBQ2IA0mBtJ3RP
ILNz8BeTkJB/aBPuQEypG5LPn2UaMwVzyMW0ld2K/JA44ICryT+J48VhEOT6pMLe
nw8d/BVHn5HBtBJE3tVOA2j2Q+09r6t2KyJQbUC2kqtCJ7X0MnIHMnJFmm0NOR6P
gJB7t+6UlK3oSficb0rW9bJmNy2iYOYgb48Mnci4JyEVem5eFNuPt5AqHTGR18vA
0UU5bRHPanWIHT5e5VUOW/QGrJFkUvRMHX3sJxecY2d9AlXt/36FPheqalqg9Irr
5caPT+5VWJzWT5eB7SILG/zuxCGr6UNPYhwWui5NO6CdaGUGxalZ/Mih4D7bVfWP
tSVRF2G0F0y5FtBPkfQ1DLEC2FLXBRs9MWIsmJZ9xOtqkPCbE/O0kNeG2Q0=
-----END CERTIFICATE-----
)EOF";

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

#ifndef PLEXUS_INSECURE_TLS
    client.setCACert(PLEXUS_ISRG_ROOT_X1);
#else
    #warning "PLEXUS_INSECURE_TLS: cert verification disabled. Dev only."
    client.setInsecure();
#endif

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

#if PLEXUS_ENABLE_AUTO_REGISTER

plexus_err_t plexus_hal_http_post_response(
    const char* url, const char* api_key, const char* user_agent,
    const char* body, size_t body_len,
    char* response_buf, size_t response_buf_size, size_t* response_len) {
    if (!url || !api_key || !body || !response_buf || !response_len) {
        return PLEXUS_ERR_NULL_PTR;
    }

    *response_len = 0;

#if defined(ESP32) || defined(ESP8266)
    HTTPClient http;
    WiFiClientSecure client;
#ifndef PLEXUS_INSECURE_TLS
    client.setCACert(PLEXUS_ISRG_ROOT_X1);
#else
    #warning "PLEXUS_INSECURE_TLS: cert verification disabled. Dev only."
    client.setInsecure();
#endif

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
        String payload = http.getString();
        size_t len = payload.length();
        if (len >= response_buf_size) len = response_buf_size - 1;
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
    (void)body_len;
    (void)response_buf_size;
    return PLEXUS_ERR_HAL;
#endif
}

#endif /* PLEXUS_ENABLE_AUTO_REGISTER */

#if PLEXUS_ENABLE_SENSOR_DISCOVERY

#include <Wire.h>

plexus_err_t plexus_hal_i2c_init(uint8_t bus_num) {
    (void)bus_num;
    Wire.begin();
    return PLEXUS_OK;
}

bool plexus_hal_i2c_probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

plexus_err_t plexus_hal_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t* out) {
    if (!out) return PLEXUS_ERR_NULL_PTR;

    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return PLEXUS_ERR_I2C;

    if (Wire.requestFrom(addr, (uint8_t)1) != 1) return PLEXUS_ERR_I2C;
    *out = Wire.read();
    return PLEXUS_OK;
}

plexus_err_t plexus_hal_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0) ? PLEXUS_OK : PLEXUS_ERR_I2C;
}

#endif /* PLEXUS_ENABLE_SENSOR_DISCOVERY */

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
#ifndef PLEXUS_INSECURE_TLS
    client.setCACert(PLEXUS_ISRG_ROOT_X1);
#else
    client.setInsecure();
#endif

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

/* ========================================================================= */
/* Thread safety: FreeRTOS mutex for ESP32/8266, no-op for others            */
/* ========================================================================= */

#if PLEXUS_ENABLE_THREAD_SAFE

#if defined(ESP32)
#include "freertos/semphr.h"

void* plexus_hal_mutex_create(void) {
    return (void*)xSemaphoreCreateRecursiveMutex();
}

void plexus_hal_mutex_lock(void* mutex) {
    if (mutex) xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex, portMAX_DELAY);
}

void plexus_hal_mutex_unlock(void* mutex) {
    if (mutex) xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex);
}

void plexus_hal_mutex_destroy(void* mutex) {
    if (mutex) vSemaphoreDelete((SemaphoreHandle_t)mutex);
}

#else /* Non-FreeRTOS Arduino: no-op stubs */

void* plexus_hal_mutex_create(void) { return (void*)1; }
void  plexus_hal_mutex_lock(void* mutex) { (void)mutex; }
void  plexus_hal_mutex_unlock(void* mutex) { (void)mutex; }
void  plexus_hal_mutex_destroy(void* mutex) { (void)mutex; }

#endif /* ESP32 */

#endif /* PLEXUS_ENABLE_THREAD_SAFE */

} /* extern "C" */

/* PlexusClient C++ wrapper is defined in plexus.h (inside #ifdef __cplusplus) */

#endif /* ARDUINO */
