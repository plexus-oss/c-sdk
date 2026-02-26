/**
 * @file plexus_example.ino
 * @brief Minimal Arduino example for Plexus C SDK
 *
 * Supports ESP32 and ESP8266 boards with built-in WiFi.
 *
 * Quick start:
 *   1. Set WIFI_SSID, WIFI_PASSWORD, and PLEXUS_API_KEY below
 *   2. Upload 
 *   3. Watch telemetry appear in your Plexus dashboard
 */

#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#else
  #include <WiFi.h>
#endif

#include "plexus.hpp"

/* ========================================================================= */
/* Configuration - Update these values                                       */
/* ========================================================================= */

#define WIFI_SSID      "YourWiFiSSID"
#define WIFI_PASSWORD  "YourWiFiPassword"

#define PLEXUS_API_KEY   "plx_your_api_key_here"
#define PLEXUS_SOURCE_ID "arduino-sensor-001"

/* ========================================================================= */
/* Globals                                                                   */
/* ========================================================================= */

PlexusClient plexus(PLEXUS_API_KEY, PLEXUS_SOURCE_ID);

/* ========================================================================= */
/* Setup                                                                     */
/* ========================================================================= */

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("Plexus Arduino Example");

    /* Connect to WiFi */
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    if (!plexus.isValid()) {
        Serial.println("ERROR: Failed to initialize Plexus client");
    }
}

/* ========================================================================= */
/* Loop                                                                      */
/* ========================================================================= */

/* Track when to read sensors */
unsigned long lastReadMs = 0;
#define READ_INTERVAL_MS 5000

void loop() {
    /* Read sensors at a fixed interval */
    unsigned long now = millis();
    if (now - lastReadMs >= READ_INTERVAL_MS) {
        lastReadMs = now;

        float temperature = 25.0 + (random(0, 1000) / 100.0) - 5.0;
        float humidity    = 50.0 + (random(0, 2000) / 100.0) - 10.0;

        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.print("C  Humidity: ");
        Serial.print(humidity);
        Serial.println("%");

        /* Queue metrics */
        plexus.send("temperature", temperature);
        plexus.send("humidity", humidity);
    }

    /* Call tick() every loop iteration to handle time-based auto-flush.
     * This sends queued metrics when the flush interval elapses,
     * so you don't need to call flush() manually. */
    plexus_err_t err = plexus.tick();
    if (err != PLEXUS_OK) {
        Serial.print("Tick error: ");
        Serial.println(plexus_strerror(err));
    }

    delay(100);
}
