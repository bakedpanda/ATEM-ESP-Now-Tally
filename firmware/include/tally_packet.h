#pragma once
#include <stdint.h>

// Tally state values
#define TALLY_STANDBY  0
#define TALLY_PREVIEW  1
#define TALLY_PROGRAM  2

// Broadcast packet: bridge → camera units (3 bytes)
typedef struct __attribute__((packed)) {
    uint8_t flags;      // bit 0: 1=ATEM connected
    uint8_t states[2];  // 20 slots × 2 bits, LSB-first per byte
} TallyPacket;

// Heartbeat: camera → bridge (2 bytes)
typedef struct __attribute__((packed)) {
    uint8_t type;    // always 0x01
    uint8_t unitId;  // 1–20
} HeartbeatPacket;

// Identify: bridge → receiver (unicast) (1 byte)
typedef struct __attribute__((packed)) {
    uint8_t type;  // always 0x03
} IdentifyPacket;

// Pack a state value (0–2) into a TallyPacket for a given unitId (1–20)
inline void tallyPacketSetState(TallyPacket* pkt, uint8_t unitId, uint8_t state) {
    uint8_t offset = (unitId - 1) * 2;
    uint8_t byteIdx = offset / 8;
    uint8_t bitPos  = offset % 8;
    pkt->states[byteIdx] &= ~(0x03 << bitPos);
    pkt->states[byteIdx] |= (state & 0x03) << bitPos;
}

// Extract state value for a given unitId from a TallyPacket
inline uint8_t tallyPacketGetState(const TallyPacket* pkt, uint8_t unitId) {
    uint8_t offset = (unitId - 1) * 2;
    uint8_t byteIdx = offset / 8;
    uint8_t bitPos  = offset % 8;
    return (pkt->states[byteIdx] >> bitPos) & 0x03;
}
