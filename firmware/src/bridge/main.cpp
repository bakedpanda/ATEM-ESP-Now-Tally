#include <Arduino.h>
#include "config.h"
#include "led_driver.h"
#include "wifi_manager.h"
#include "ws_client.h"
#include "espnow_bridge.h"

static BridgeState bridgeState = {};

// Ring buffer for heartbeat unit IDs received from ESP-NOW callback
static volatile uint8_t hbBuffer[20] = {};
static volatile uint8_t hbWrite = 0;
static uint8_t hbRead = 0;

// Called by ESP-NOW receive task — store heartbeat for main loop to relay
static void onHeartbeat(uint8_t unitId) {
    hbBuffer[hbWrite % 20] = unitId;
    hbWrite++;
}

// Keepalive broadcast interval
static unsigned long lastBroadcast = 0;
static const unsigned long BROADCAST_INTERVAL_MS = 2000;

void setup() {
    Serial.begin(115200);
    ledDriverInit(LED_PIN, LED_COUNT);
    ledDriverSetState(LED_STATE_AMBER_BREATH, nullptr);

    wifiManagerInit();
    // Wait for WiFi before starting WebSocket and ESP-NOW
    Serial.print("Connecting to WiFi");
    while (!wifiManagerIsConnected()) {
        wifiManagerTick();
        ledDriverTick();
        Serial.print(".");
        delay(100);
    }
    Serial.println(" connected");

    wsClientInit(&bridgeState);
    espnowBridgeInit(onHeartbeat);

    Serial.printf("Bridge unit %d ready\n", UNIT_ID);
}

void loop() {
    wifiManagerTick();
    wsClientTick();

    // Relay pending heartbeats to server
    while (hbRead != (uint8_t)hbWrite) {
        uint8_t uid = hbBuffer[hbRead % 20];
        hbRead++;
        if (wsClientIsConnected()) {
            char msg[48];
            snprintf(msg, sizeof(msg), "{\"type\":\"heartbeat\",\"unitId\":%u}", uid);
            wsClientSendText(msg);
        }
    }

    // Broadcast on new tally data or keepalive interval
    unsigned long now = millis();
    if (bridgeState.updated || now - lastBroadcast >= BROADCAST_INTERVAL_MS) {
        espnowBridgeBroadcast(&bridgeState);
        bridgeState.updated = false;
        lastBroadcast = now;
    }

    // Determine own LED state
    LedState ownState;
    if (!wsClientIsConnected()) {
        ownState = LED_STATE_AMBER_BREATH;
    } else if (!bridgeState.atemConnected) {
        ownState = LED_STATE_WHITE_BREATH;
    } else {
        uint8_t s = bridgeState.states[UNIT_ID - 1];
        switch (s) {
            case TALLY_PROGRAM: ownState = LED_STATE_PROGRAM; break;
            case TALLY_PREVIEW: ownState = LED_STATE_PREVIEW; break;
            default:            ownState = LED_STATE_STANDBY; break;
        }
    }

    LedSettings settings = {
        bridgeState.brightness,
        bridgeState.standbyBrightness,
        bridgeState.animSpeedMs,
    };
    ledDriverSetState(ownState, &settings);
    ledDriverTick();
}
