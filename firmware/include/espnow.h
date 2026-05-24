#pragma once
#include <stdint.h>
#include "tally_packet.h"

// Heartbeat entry from a receiver — produced by ESP-NOW recv callback
typedef struct {
    uint8_t unitId;
    char    mac[18];  // "AA:BB:CC:DD:EE:FF\0"
} HeartbeatEntry;

// Tally data received from bridge — for receiver mode
typedef struct {
    bool    atemConnected;
    uint8_t states[20];
} TallyData;

// Call after WiFi is initialised (uses current WiFi channel)
void espnowInit();

// Bridge: broadcast tally to all receivers
void espnowBroadcast(bool atemConnected, const uint8_t states[20]);

// Bridge: send unicast IdentifyPacket to a receiver by MAC string
void espnowSendIdentify(const char* targetMacStr);

// Receiver: send HeartbeatPacket to bridge
void espnowSendHeartbeat(uint8_t unitId);

// Receiver: true once bridge MAC is learned from first TallyPacket
bool espnowIsBridgeMacKnown();

// Receiver: returns true (and clears flag) if an IdentifyPacket was received
bool espnowHasIdentify();

// Bridge: drain one heartbeat from ring buffer — returns false when empty
bool espnowNextHeartbeat(HeartbeatEntry* out);

// Receiver: drain one tally update — returns false when none pending
bool espnowNextTally(TallyData* out);

// Receiver: millis() timestamp of last TallyPacket (0 if never received)
unsigned long espnowLastTallyMs();
