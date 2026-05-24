#include "espnow_camera.h"
#include "tally_packet.h"
#include "led_driver.h"
#include "config.h"
#include <esp_now.h>
#include <WiFi.h>

static uint8_t bridgeMac[6] = {};
static bool bridgeMacKnown = false;
static uint8_t lastState = 0;
static bool atemConnected = false;
static unsigned long lastPacketMs = 0;
static LedSettings ledSettings = { 80, 20, 40 };

// Legacy ESP-IDF v4 callback signature: (mac_addr, data, data_len)
static void onDataRecv(const uint8_t* mac_addr, const uint8_t* data, int len) {
    if (len != sizeof(TallyPacket)) return;

    // Learn bridge MAC from first packet
    if (!bridgeMacKnown) {
        memcpy(bridgeMac, mac_addr, 6);
        bridgeMacKnown = true;
        // Register bridge as peer for heartbeat replies
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, bridgeMac, 6);
        peer.channel = 0;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    const TallyPacket* pkt = (const TallyPacket*)data;
    lastPacketMs = millis();
    atemConnected = (pkt->flags & 0x01) != 0;
    lastState = tallyPacketGetState(pkt, UNIT_ID);
}

void espnowCameraInit() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        // Fatal — flash amber forever
        while (true) { ledDriverTick(); delay(10); }
    }
    esp_now_register_recv_cb(onDataRecv);
}

void espnowCameraTick() {
    unsigned long now = millis();

    // Determine LED state
    LedState state;
    if (now - lastPacketMs > 10000 || lastPacketMs == 0) {
        state = LED_STATE_AMBER_BREATH;  // no connection to bridge
    } else if (!atemConnected) {
        state = LED_STATE_WHITE_BREATH;  // bridge ok, ATEM disconnected
    } else {
        switch (lastState) {
            case TALLY_PROGRAM: state = LED_STATE_PROGRAM; break;
            case TALLY_PREVIEW: state = LED_STATE_PREVIEW; break;
            default:            state = LED_STATE_STANDBY; break;
        }
    }
    ledDriverSetState(state, &ledSettings);

    // Send heartbeat every 5 seconds
    static unsigned long lastHeartbeat = 0;
    if (bridgeMacKnown && now - lastHeartbeat >= 5000) {
        lastHeartbeat = now;
        HeartbeatPacket hb = { 0x01, UNIT_ID };
        esp_now_send(bridgeMac, (uint8_t*)&hb, sizeof(hb));
    }
}
