#include <Arduino.h>
#include "config.h"
#include "led_driver.h"
#include "wifi_manager.h"
#include "ws_client.h"
#include "espnow.h"
#include "provisioning.h"
#include "tally_packet.h"

typedef enum {
    STATE_WIFI_CONNECTING,
    STATE_SERVER_CONNECTING,
    STATE_PROVISIONING,
    STATE_BRIDGE,
    STATE_RECEIVER,
} DeviceState;

static DeviceState deviceState = STATE_WIFI_CONNECTING;
static BridgeState bridgeState = {};

// ── Callbacks ────────────────────────────────────────────────────────────────

static void onRole(uint8_t unitId, DeviceRole role) {
    provisioningSet(unitId, role);

    if (role == ROLE_BRIDGE) {
        espnowInit();
        deviceState = STATE_BRIDGE;
        Serial.printf("Bridge unit %d ready\n", unitId);

    } else if (role == ROLE_RECEIVER) {
        wifiManagerStop();
        espnowInit();
        deviceState = STATE_RECEIVER;
        Serial.printf("Receiver unit %d ready\n", unitId);

    } else {
        deviceState = STATE_PROVISIONING;
        Serial.println("Unprovisioned — awaiting assignment in /assign");
    }
}

static void onIdentify(const char* targetMac) {
    if (targetMac == nullptr || targetMac[0] == '\0') {
        ledDriverSetState(LED_STATE_IDENTIFY, nullptr);
    } else {
        espnowSendIdentify(targetMac);
    }
}

// ── Timing ───────────────────────────────────────────────────────────────────

static unsigned long lastBroadcastMs  = 0;
static unsigned long lastHeartbeatMs  = 0;
static const unsigned long BROADCAST_INTERVAL_MS = 2000;
static const unsigned long HEARTBEAT_INTERVAL_MS = 5000;

// ── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    ledDriverInit(LED_PIN, LED_COUNT);
    ledDriverSetState(LED_STATE_AMBER_BREATH, nullptr);

    wifiManagerInit();

    Serial.print("Connecting to WiFi");
    while (!wifiManagerIsConnected()) {
        wifiManagerTick();
        ledDriverTick();
        Serial.print(".");
        delay(100);
    }
    Serial.println(" connected");

    wsClientInit(&bridgeState, onRole, onIdentify);
    deviceState = STATE_SERVER_CONNECTING;
}

// ── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    unsigned long now = millis();

    switch (deviceState) {

        case STATE_WIFI_CONNECTING:
            break;

        case STATE_SERVER_CONNECTING:
            wifiManagerTick();
            wsClientTick();
            ledDriverSetState(LED_STATE_AMBER_BREATH, nullptr);
            ledDriverTick();
            break;

        case STATE_PROVISIONING:
            wifiManagerTick();
            wsClientTick();
            ledDriverSetState(LED_STATE_PAIRING, nullptr);
            ledDriverTick();
            break;

        case STATE_BRIDGE: {
            wifiManagerTick();
            wsClientTick();

            HeartbeatEntry hb;
            while (espnowNextHeartbeat(&hb)) {
                if (wsClientIsConnected()) {
                    char msg[80];
                    snprintf(msg, sizeof(msg),
                        "{\"type\":\"heartbeat_relay\",\"unitId\":%u,\"mac\":\"%s\"}",
                        hb.unitId, hb.mac);
                    wsClientSendText(msg);
                }
            }

            if (wsClientIsConnected() && now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
                lastHeartbeatMs = now;
                char msg[80];
                snprintf(msg, sizeof(msg),
                    "{\"type\":\"heartbeat\",\"unitId\":%u,\"mac\":\"%s\"}",
                    provisioningGetUnitId(), wsClientGetMac());
                wsClientSendText(msg);
            }

            if (bridgeState.updated || now - lastBroadcastMs >= BROADCAST_INTERVAL_MS) {
                espnowBroadcast(bridgeState.atemConnected, bridgeState.states,
                                bridgeState.brightness, bridgeState.standbyBrightness,
                                bridgeState.animSpeedMs);
                bridgeState.updated = false;
                lastBroadcastMs = now;
            }

            LedState ownState;
            if (!wsClientIsConnected()) {
                ownState = LED_STATE_AMBER_BREATH;
            } else if (!bridgeState.atemConnected) {
                ownState = LED_STATE_WHITE_BREATH;
            } else {
                uint8_t uid = provisioningGetUnitId();
                uint8_t s = (uid >= 1 && uid <= 20) ? bridgeState.states[uid-1] : TALLY_STANDBY;
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
            break;
        }

        case STATE_RECEIVER: {
            if (espnowHasIdentify()) {
                ledDriverSetState(LED_STATE_IDENTIFY, nullptr);
            }

            if (espnowIsBridgeMacKnown() && now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
                lastHeartbeatMs = now;
                espnowSendHeartbeat(provisioningGetUnitId());
            }

            TallyData td;
            if (espnowNextTally(&td)) {
                bridgeState.atemConnected = td.atemConnected;
                memcpy(bridgeState.states, td.states, 20);
            }

            LedState ownState;
            unsigned long lastPkt = espnowLastTallyMs();
            if (lastPkt == 0 || now - lastPkt > 10000) {
                ownState = LED_STATE_AMBER_BREATH;
            } else if (!bridgeState.atemConnected) {
                ownState = LED_STATE_WHITE_BREATH;
            } else {
                uint8_t uid = provisioningGetUnitId();
                uint8_t s = (uid >= 1 && uid <= 20) ? bridgeState.states[uid-1] : TALLY_STANDBY;
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
            break;
        }
    }
}
