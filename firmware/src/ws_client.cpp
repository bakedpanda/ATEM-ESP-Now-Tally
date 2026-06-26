#include "ws_client.h"
#include "config.h"
#include "provisioning.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>

static WebSocketsClient ws;
static BridgeState*     bridgeState     = nullptr;
static RoleCallback     roleCallback    = nullptr;
static IdentifyCallback identifyCallback = nullptr;
static bool             connected       = false;
static char             cachedMac[18]   = {};  // "AA:BB:CC:DD:EE:FF\0"

static uint8_t animSpeedToMs(const char* speed) {
    if (strcmp(speed, "fast") == 0)   return 15;
    if (strcmp(speed, "medium") == 0) return 25;
    return 40;  // slow / default
}

static void onMessage(uint8_t* payload, size_t length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length) != DeserializationError::Ok) return;

    const char* type = doc["type"];
    if (!type) return;

    if (strcmp(type, "role") == 0) {
        const char* status = doc["status"];
        if (status && strcmp(status, "unprovisioned") == 0) {
            if (roleCallback) roleCallback(0, ROLE_UNPROVISIONED);
        } else {
            uint8_t     uid  = doc["unitId"] | 0;
            const char* role = doc["role"]   | "receiver";
            DeviceRole  r    = (strcmp(role, "bridge") == 0) ? ROLE_BRIDGE : ROLE_RECEIVER;
            if (roleCallback) roleCallback(uid, r);
        }
        return;
    }

    if (strcmp(type, "tally") == 0 && bridgeState) {
        bridgeState->atemConnected = doc["atemConnected"] | false;
        JsonObject states = doc["states"].as<JsonObject>();
        for (JsonPair kv : states) {
            int uid = atoi(kv.key().c_str());
            if (uid >= 1 && uid <= 20)
                bridgeState->states[uid - 1] = kv.value().as<uint8_t>();
        }
        bridgeState->updated = true;
        return;
    }

    if (strcmp(type, "settings") == 0 && bridgeState) {
        bridgeState->brightness        = doc["brightness"]        | 80;
        bridgeState->standbyBrightness = doc["standbyBrightness"] | 20;
        bridgeState->animSpeedMs       = animSpeedToMs(doc["animSpeed"] | "slow");
        return;
    }

    if (strcmp(type, "identify") == 0) {
        const char* targetMac = doc["targetMac"] | "";
        if (identifyCallback) identifyCallback(targetMac);
        return;
    }
}

static void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            connected = true;
            // Cache and send MAC in hello message
            uint8_t macBytes[6];
            WiFi.macAddress(macBytes);
            snprintf(cachedMac, sizeof(cachedMac),
                "%02X:%02X:%02X:%02X:%02X:%02X",
                macBytes[0], macBytes[1], macBytes[2],
                macBytes[3], macBytes[4], macBytes[5]);
            char msg[64];
            snprintf(msg, sizeof(msg), "{\"type\":\"hello\",\"mac\":\"%s\"}", cachedMac);
            ws.sendTXT(msg);
            Serial.printf("WS connected — MAC %s\n", cachedMac);
            break;
        }
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

void wsClientInit(BridgeState* state, RoleCallback onRole, IdentifyCallback onIdentify) {
    bridgeState      = state;
    roleCallback     = onRole;
    identifyCallback = onIdentify;
    if (state) {
        state->brightness        = 80;
        state->standbyBrightness = 20;
        state->animSpeedMs       = 40;
    }
    // Initialise mDNS so the stack can resolve .local hostnames.
    // Each device advertises a unique tally-XXYY.local using the last 2 MAC bytes.
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String mdnsHostname = "tally-" + mac.substring(8);
    if (!MDNS.begin(mdnsHostname.c_str())) {
        Serial.println("mDNS init failed — .local resolution may not work");
    }
    ws.begin("atem-tally.local", 8259, "/bridge");
    ws.onEvent(wsEvent);
    ws.setReconnectInterval(3000);
}

void        wsClientTick()             { ws.loop(); }
bool        wsClientIsConnected()      { return connected; }
void        wsClientSendText(const char* msg) { ws.sendTXT(msg); }
const char* wsClientGetMac()           { return cachedMac; }
