#include "ws_client.h"
#include "config.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

static WebSocketsClient ws;
static BridgeState* bridgeState = nullptr;
static bool connected = false;

static uint8_t animSpeedToMs(const char* speed) {
    if (strcmp(speed, "fast") == 0) return 15;
    if (strcmp(speed, "medium") == 0) return 25;
    return 40;  // slow
}

static void onMessage(uint8_t* payload, size_t length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length) != DeserializationError::Ok) return;

    const char* type = doc["type"];
    if (!type) return;

    if (strcmp(type, "tally") == 0) {
        bridgeState->atemConnected = doc["atemConnected"] | false;
        JsonObject states = doc["states"].as<JsonObject>();
        for (JsonPair kv : states) {
            int uid = atoi(kv.key().c_str());
            if (uid >= 1 && uid <= 20)
                bridgeState->states[uid - 1] = kv.value().as<uint8_t>();
        }
        bridgeState->updated = true;
    }
    if (strcmp(type, "settings") == 0) {
        bridgeState->brightness        = doc["brightness"] | 80;
        bridgeState->standbyBrightness = doc["standbyBrightness"] | 20;
        bridgeState->animSpeedMs       = animSpeedToMs(doc["animSpeed"] | "slow");
    }
}

static void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            connected = true;
            Serial.println("WS connected to server");
            // Register this bridge unit
            ws.sendTXT("{\"type\":\"register\",\"unitId\":" + String(UNIT_ID) + "}");
            break;
        case WStype_DISCONNECTED:
            connected = false;
            Serial.println("WS disconnected");
            break;
        case WStype_TEXT:
            onMessage(payload, length);
            break;
        default: break;
    }
}

void wsClientInit(BridgeState* state) {
    bridgeState = state;
    bridgeState->brightness = 80;
    bridgeState->standbyBrightness = 20;
    bridgeState->animSpeedMs = 40;
    ws.begin(SERVER_HOST, SERVER_PORT, "/bridge");
    ws.onEvent(wsEvent);
    ws.setReconnectInterval(3000);
}

void wsClientTick() { ws.loop(); }
bool wsClientIsConnected() { return connected; }
void wsClientSendText(const char* msg) { ws.sendTXT(msg); }
