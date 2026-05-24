#include "espnow.h"
#include "tally_packet.h"
#include <esp_now.h>
#include <WiFi.h>
#include <string.h>
#include <stdio.h>

static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// Bridge: unitId→MAC for unicast (populated from received heartbeats)
static uint8_t unitMacs[20][6]  = {};
static bool    unitMacKnown[20] = {};

// Receiver: bridge MAC (learned from first TallyPacket)
static uint8_t bridgeMac[6]  = {};
static bool    bridgeMacKnown = false;

// Heartbeat ring buffer (ISR-safe: volatile write index, non-volatile data)
static HeartbeatEntry hbBuffer[20]   = {};
static volatile uint8_t hbWrite      = 0;
static uint8_t          hbRead       = 0;

// Tally data (receiver mode)
static volatile TallyData pendingTally  = {};
static volatile bool   hasTally      = false;
static unsigned long   lastTallyMs   = 0;

// Identify flag (receiver mode)
static volatile bool identifyFlag = false;

static void addPeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

static bool parseMacStr(const char* str, uint8_t* out) {
    if (!str || !out) return false;
    int n = sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &out[0], &out[1], &out[2], &out[3], &out[4], &out[5]);
    return n == 6;
}

// Legacy ESP-IDF v4 callback signature (Arduino ESP32 framework)
static void onDataRecv(const uint8_t* mac_addr, const uint8_t* data, int len) {
    if (len == (int)sizeof(TallyPacket)) {
        // Receiver: learn bridge MAC, store tally
        if (!bridgeMacKnown) {
            memcpy(bridgeMac, mac_addr, 6);
            bridgeMacKnown = true;
            addPeer(bridgeMac);
        }
        const TallyPacket* pkt = (const TallyPacket*)data;
        pendingTally.atemConnected = (pkt->flags & 0x01) != 0;
        for (uint8_t i = 1; i <= 20; i++) {
            pendingTally.states[i-1] = tallyPacketGetState(pkt, i);
        }
        hasTally   = true;
        lastTallyMs = millis();

    } else if (len == (int)sizeof(HeartbeatPacket)) {
        // Bridge: relay heartbeat via ring buffer
        const HeartbeatPacket* hb = (const HeartbeatPacket*)data;
        if (hb->type != 0x01) return;
        uint8_t uid = hb->unitId;
        if (uid < 1 || uid > 20) return;

        uint8_t idx = hbWrite % 20;
        hbBuffer[idx].unitId = uid;
        snprintf(hbBuffer[idx].mac, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac_addr[0], mac_addr[1], mac_addr[2],
            mac_addr[3], mac_addr[4], mac_addr[5]);
        hbWrite++;

        // Track MAC for unicast identify
        if (!unitMacKnown[uid-1] || memcmp(unitMacs[uid-1], mac_addr, 6) != 0) {
            memcpy(unitMacs[uid-1], mac_addr, 6);
            unitMacKnown[uid-1] = true;
            addPeer(mac_addr);
        }

    } else if (len == (int)sizeof(IdentifyPacket)) {
        const IdentifyPacket* ip = (const IdentifyPacket*)data;
        if (ip->type == 0x03) identifyFlag = true;
    }
}

void espnowInit() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    esp_now_register_recv_cb(onDataRecv);
    addPeer(BROADCAST_MAC);
}

void espnowBroadcast(bool atemConnected, const uint8_t states[20]) {
    TallyPacket pkt = {};
    pkt.flags = atemConnected ? 0x01 : 0x00;
    for (uint8_t i = 1; i <= 20; i++) tallyPacketSetState(&pkt, i, states[i-1]);
    esp_now_send(BROADCAST_MAC, (uint8_t*)&pkt, sizeof(pkt));
}

void espnowSendIdentify(const char* targetMacStr) {
    if (!targetMacStr) return;
    uint8_t mac[6];
    if (!parseMacStr(targetMacStr, mac)) return;
    addPeer(mac);
    IdentifyPacket pkt = { 0x03 };
    esp_now_send(mac, (uint8_t*)&pkt, sizeof(pkt));
}

void espnowSendHeartbeat(uint8_t unitId) {
    if (!bridgeMacKnown) return;
    HeartbeatPacket hb = { 0x01, unitId };
    esp_now_send(bridgeMac, (uint8_t*)&hb, sizeof(hb));
}

bool espnowIsBridgeMacKnown() { return bridgeMacKnown; }

bool espnowHasIdentify() {
    bool v = identifyFlag;
    identifyFlag = false;
    return v;
}

bool espnowNextHeartbeat(HeartbeatEntry* out) {
    if (hbRead == (uint8_t)hbWrite) return false;
    uint8_t idx = hbRead % 20;
    *out = hbBuffer[idx];
    hbRead++;
    return true;
}

bool espnowNextTally(TallyData* out) {
    if (!hasTally) return false;
    *out = *(const TallyData*)&pendingTally;  // cast away volatile for copy
    hasTally = false;
    return true;
}

unsigned long espnowLastTallyMs() { return lastTallyMs; }
