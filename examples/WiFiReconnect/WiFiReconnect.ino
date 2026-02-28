/**
 * WiFiReconnect - Production-grade telemetry with WiFi recovery
 *
 * A robust example that handles WiFi disconnections, tracks connection
 * status, and uses tags to annotate telemetry with device metadata.
 * Use this as a starting point for production deployments.
 *
 * Hardware: ESP32 (WiFi reconnect uses ESP32-specific APIs)
 *
 * Features:
 *   - Automatic WiFi reconnection
 *   - Connection status reporting
 *   - Tagged metrics (device metadata)
 *   - Uptime and free heap monitoring
 *   - Error counting and diagnostics
 */

#include <WiFi.h>
#include "plexus.hpp"

// ── Configuration ──────────────────────────────────────────────────────────

const char* WIFI_SSID      = "YourWiFiSSID";
const char* WIFI_PASSWORD  = "YourWiFiPassword";
const char* PLEXUS_API_KEY = "plx_your_api_key_here";
const char* SOURCE_ID      = "esp32-prod";

const unsigned long TELEMETRY_INTERVAL_MS = 5000;
const unsigned long WIFI_CHECK_INTERVAL_MS = 10000;

// ── Globals ────────────────────────────────────────────────────────────────

PlexusClient plexus(PLEXUS_API_KEY, SOURCE_ID);

unsigned long lastTelemetryMs = 0;
unsigned long lastWifiCheckMs = 0;
bool wasConnected = false;

// ── WiFi Management ────────────────────────────────────────────────────────

bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;

    Serial.print("Connecting to WiFi");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nConnected! IP: %s  RSSI: %d dBm\n",
                       WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }

    Serial.println("\nWiFi connection failed");
    return false;
}

// ── Setup ──────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n--- Plexus Production Example ---");

    WiFi.setAutoReconnect(true);
    connectWiFi();

    if (!plexus.isValid()) {
        Serial.println("ERROR: Plexus init failed");
        return;
    }

    // Increase flush interval for less frequent but more efficient sends
    plexus.setFlushInterval(10000);

    Serial.printf("Plexus SDK v%s ready\n", plexus_version());
}

// ── Loop ───────────────────────────────────────────────────────────────────

void loop() {
    unsigned long now = millis();

    // Check WiFi periodically
    if (now - lastWifiCheckMs >= WIFI_CHECK_INTERVAL_MS) {
        lastWifiCheckMs = now;
        bool connected = (WiFi.status() == WL_CONNECTED);

        if (!connected && wasConnected) {
            Serial.println("WiFi lost — reconnecting...");
            connectWiFi();
        } else if (connected && !wasConnected) {
            Serial.println("WiFi restored");
        }
        wasConnected = connected;
    }

    // Send telemetry
    if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryMs = now;

        // System health metrics
        float uptimeMin = millis() / 60000.0;
        float freeHeapKB = ESP.getFreeHeap() / 1024.0;
        int rssi = WiFi.RSSI();

        plexus.send("uptime_min", uptimeMin);
        plexus.send("free_heap_kb", freeHeapKB);
        plexus.send("wifi_rssi", (double)rssi);

        // Your sensor readings here
        float sensorValue = analogRead(34) * (3.3 / 4095.0);
        plexus.send("sensor_voltage", sensorValue);

        Serial.printf("Sent: uptime=%.1fm  heap=%.1fKB  rssi=%d  pending=%d\n",
                       uptimeMin, freeHeapKB, rssi, plexus.pendingCount());
    }

    plexus_err_t err = plexus.tick();
    if (err != PLEXUS_OK && err != PLEXUS_ERR_NO_DATA) {
        Serial.printf("Error: %s (total errors: %u)\n",
                       plexus_strerror(err), plexus_total_errors(plexus.handle()));
    }

    delay(100);
}
