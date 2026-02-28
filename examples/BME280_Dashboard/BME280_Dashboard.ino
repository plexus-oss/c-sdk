/**
 * BME280_Dashboard - Environmental monitoring with Plexus
 *
 * Reads temperature, humidity, and pressure from a BME280 sensor
 * and streams to your Plexus dashboard. Uses the Adafruit BME280
 * library for the sensor and Plexus C SDK for telemetry.
 *
 * Hardware:
 *   - ESP32 or ESP8266
 *   - BME280 sensor (I2C)
 *
 * Wiring (I2C):
 *   BME280 SDA → ESP32 GPIO 21 (or your board's SDA)
 *   BME280 SCL → ESP32 GPIO 22 (or your board's SCL)
 *   BME280 VIN → 3.3V
 *   BME280 GND → GND
 *
 * Dependencies:
 *   - Adafruit BME280 Library (install via Arduino Library Manager)
 *   - Adafruit Unified Sensor (installed automatically)
 *
 * Setup:
 *   1. Install "Adafruit BME280 Library" from Arduino Library Manager
 *   2. Set WiFi credentials and API key below
 *   3. Upload and open Serial Monitor at 115200 baud
 */

#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "plexus.hpp"

// ── Configuration ──────────────────────────────────────────────────────────

const char* WIFI_SSID      = "YourWiFiSSID";
const char* WIFI_PASSWORD  = "YourWiFiPassword";
const char* PLEXUS_API_KEY = "plx_your_api_key_here";
const char* SOURCE_ID      = "esp32-bme280";

const unsigned long READ_INTERVAL_MS = 2000;   // Read sensor every 2 seconds

// ── Globals ────────────────────────────────────────────────────────────────

PlexusClient plexus(PLEXUS_API_KEY, SOURCE_ID);
Adafruit_BME280 bme;

bool bmeFound = false;
unsigned long lastReadMs = 0;

// ── Setup ──────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n--- Plexus BME280 Dashboard ---");

    // Initialize I2C and BME280
    Wire.begin();
    if (bme.begin(0x76) || bme.begin(0x77)) {
        bmeFound = true;
        Serial.println("BME280 found!");
    } else {
        Serial.println("WARNING: BME280 not found — sending simulated data");
    }

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
        return;
    }

    Serial.printf("Plexus SDK v%s ready — streaming to dashboard\n", plexus_version());
}

// ── Loop ───────────────────────────────────────────────────────────────────

void loop() {
    unsigned long now = millis();

    if (now - lastReadMs >= READ_INTERVAL_MS) {
        lastReadMs = now;

        float temperature, humidity, pressure;

        if (bmeFound) {
            temperature = bme.readTemperature();       // °C
            humidity    = bme.readHumidity();           // %
            pressure    = bme.readPressure() / 100.0F;  // hPa
        } else {
            // Simulated data for testing without hardware
            temperature = 22.0 + random(-20, 20) / 10.0;
            humidity    = 55.0 + random(-50, 50) / 10.0;
            pressure    = 1013.0 + random(-10, 10) / 10.0;
        }

        Serial.printf("T=%.1f°C  H=%.1f%%  P=%.1f hPa\n",
                       temperature, humidity, pressure);

        plexus.send("temperature", temperature);
        plexus.send("humidity", humidity);
        plexus.send("pressure", pressure);
    }

    plexus_err_t err = plexus.tick();
    if (err != PLEXUS_OK && err != PLEXUS_ERR_NO_DATA) {
        Serial.printf("Send error: %s\n", plexus_strerror(err));
    }

    delay(100);
}
