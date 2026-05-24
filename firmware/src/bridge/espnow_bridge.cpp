#include "espnow_bridge.h"
#include "tally_packet.h"
#include "config.h"
#include <esp_now.h>
#include <WiFi.h>

// Forward declaration of the heartbeat relay function (set by main)
static void (*heartbeatCallback)(uint8_t unitId) = nullptr;
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void onDataRecv(const uint8_t* mac_addr, const uint8_t* data, int len) {
    if (len != sizeof(HeartbeatPacket)) return;
    const HeartbeatPacket* hb = (const HeartbeatPacket*)data;
    if (hb->type != 0x01) return;
    if (heartbeatCallback) heartbeatCallback(hb->unitId);
}

void espnowBridgeInit(void (*onHeartbeat)(uint8_t unitId)) {
    heartbeatCallback = onHeartbeat;
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    esp_now_register_recv_cb(onDataRecv);
    // Add broadcast peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

void espnowBridgeBroadcast(const BridgeState* state) {
    TallyPacket pkt = {};
    pkt.flags = state->atemConnected ? 0x01 : 0x00;
    for (uint8_t i = 1; i <= 20; i++) {
        tallyPacketSetState(&pkt, i, state->states[i - 1]);
    }
    esp_now_send(BROADCAST_MAC, (uint8_t*)&pkt, sizeof(pkt));
}
