/**
 * BasicTelemetry - Send sensor data to Plexus
 *
 * The simplest possible Plexus example. Reads an analog sensor
 * and streams the value to your dashboard every 5 seconds.
 *
 * Hardware: ESP32 or ESP8266 with WiFi
 * Wiring:   Potentiometer or sensor on pin 34 (optional — works with random data too)
 *
 * Setup:
 *   1. Set WIFI_SSID and WIFI_PASSWORD below
 *   2. Get an API key from https://app.plexus.company → Add Device
 *   3. Set PLEXUS_API_KEY
 *   4. Upload and open Serial Monitor at 115200 baud
 *   5. Watch data appear in your Plexus dashboard
 */

#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif

#include "plexus.hpp"

// ── Configuration ──────────────────────────────────────────────────────────

const char* WIFI_SSID     = "YourWiFiSSID";
const char* WIFI_PASSWORD = "YourWiFiPassword";
const char* PLEXUS_API_KEY = "plx_your_api_key_here";
const char* SOURCE_ID      = "esp32-basic";

// ── Globals ────────────────────────────────────────────────────────────────

PlexusClient plexus(PLEXUS_API_KEY, SOURCE_ID);

unsigned long lastReadMs = 0;
const unsigned long READ_INTERVAL_MS = 5000;

// ── Setup ──────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n--- Plexus Basic Telemetry ---");

    // Connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

    if (!plexus.isValid()) {
        Serial.println("ERROR: Failed to initialize Plexus client");
    } else {
        Serial.printf("Plexus SDK v%s ready\n", plexus_version());
    }
}

// ── Loop ───────────────────────────────────────────────────────────────────

void loop() {
    unsigned long now = millis();

    if (now - lastReadMs >= READ_INTERVAL_MS) {
        lastReadMs = now;

        // Replace with your actual sensor reading
        float temperature = 20.0 + random(0, 100) / 10.0;
        float humidity    = 40.0 + random(0, 200) / 10.0;

        Serial.printf("Sending: temp=%.1f°C  humidity=%.1f%%\n", temperature, humidity);

        plexus.send("temperature", temperature);
        plexus.send("humidity", humidity);
    }

    // tick() handles auto-flush every 5 seconds
    plexus_err_t err = plexus.tick();
    if (err != PLEXUS_OK && err != PLEXUS_ERR_NO_DATA) {
        Serial.printf("Error: %s\n", plexus_strerror(err));
    }

    delay(100);
}
