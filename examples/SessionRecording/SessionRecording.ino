/**
 * SessionRecording - Group telemetry into recording sessions
 *
 * Demonstrates Plexus recording sessions. Press the BOOT button
 * to start/stop a recording session. All telemetry during the session
 * is tagged with a session_id, making it easy to compare test runs
 * in the Plexus dashboard.
 *
 * Hardware:
 *   - ESP32 (uses BOOT button on GPIO 0)
 *   - Any analog sensor on pin 34 (optional)
 *
 * Use cases:
 *   - Motor test runs
 *   - Environmental monitoring windows
 *   - Before/after comparison tests
 *   - Benchmark recordings
 */

#include <WiFi.h>
#include "plexus.hpp"

// ── Configuration ──────────────────────────────────────────────────────────

const char* WIFI_SSID      = "YourWiFiSSID";
const char* WIFI_PASSWORD  = "YourWiFiPassword";
const char* PLEXUS_API_KEY = "plx_your_api_key_here";
const char* SOURCE_ID      = "esp32-sessions";

const int BUTTON_PIN = 0;  // BOOT button on most ESP32 boards
const unsigned long READ_INTERVAL_MS = 1000;
const unsigned long DEBOUNCE_MS = 300;

// ── Globals ────────────────────────────────────────────────────────────────

PlexusClient plexus(PLEXUS_API_KEY, SOURCE_ID);

bool sessionActive = false;
unsigned long sessionCount = 0;
unsigned long lastReadMs = 0;
unsigned long lastButtonMs = 0;

// ── Setup ──────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n--- Plexus Session Recording ---");

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

    if (!plexus.isValid()) {
        Serial.println("ERROR: Plexus init failed");
        return;
    }

    Serial.println("Ready. Press BOOT button to start/stop recording.");
}

// ── Session Toggle ─────────────────────────────────────────────────────────

void toggleSession() {
    plexus_client_t* px = plexus.handle();

    if (sessionActive) {
        // End session — flush remaining data first
        plexus.flush();
        plexus_session_end(px);
        sessionActive = false;
        Serial.println("Session stopped");
    } else {
        // Start new session with incrementing ID
        sessionCount++;
        char sessionId[32];
        snprintf(sessionId, sizeof(sessionId), "test-run-%03lu", sessionCount);

        plexus_session_start(px, sessionId);
        sessionActive = true;
        Serial.printf("Session started: %s\n", sessionId);
    }
}

// ── Loop ───────────────────────────────────────────────────────────────────

void loop() {
    unsigned long now = millis();

    // Check button with debounce
    if (digitalRead(BUTTON_PIN) == LOW && now - lastButtonMs > DEBOUNCE_MS) {
        lastButtonMs = now;
        toggleSession();
    }

    // Send telemetry
    if (now - lastReadMs >= READ_INTERVAL_MS) {
        lastReadMs = now;

        // Replace with your sensor readings
        float value1 = analogRead(34) * (3.3 / 4095.0);
        float value2 = 25.0 + sin(now / 5000.0) * 5.0;

        plexus.send("sensor_voltage", value1);
        plexus.send("temperature", value2);

        const char* status = sessionActive ? "RECORDING" : "idle";
        Serial.printf("[%s] voltage=%.2fV  temp=%.1f°C\n",
                       status, value1, value2);
    }

    plexus_err_t err = plexus.tick();
    if (err != PLEXUS_OK && err != PLEXUS_ERR_NO_DATA) {
        Serial.printf("Error: %s\n", plexus_strerror(err));
    }

    delay(50);
}
