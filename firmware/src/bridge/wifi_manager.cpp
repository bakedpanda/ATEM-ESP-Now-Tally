#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>

static bool connected = false;
static unsigned long lastAttempt = 0;
static const unsigned long RETRY_INTERVAL_MS = 5000;

void wifiManagerInit() {
    WiFi.mode(WIFI_AP_STA);  // AP_STA so ESP-NOW works on same channel as AP
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastAttempt = millis();
}

bool wifiManagerIsConnected() { return connected; }

void wifiManagerTick() {
    bool nowConnected = WiFi.status() == WL_CONNECTED;
    if (nowConnected && !connected) {
        connected = true;
        Serial.printf("WiFi connected. IP: %s  Channel: %d\n",
            WiFi.localIP().toString().c_str(), WiFi.channel());
    }
    if (!nowConnected && connected) {
        connected = false;
        Serial.println("WiFi disconnected — retrying...");
    }
    if (!nowConnected && millis() - lastAttempt > RETRY_INTERVAL_MS) {
        lastAttempt = millis();
        WiFi.reconnect();
    }
}
