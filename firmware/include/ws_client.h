#pragma once
#include <stdint.h>
#include "tally_packet.h"

typedef struct {
    bool atemConnected;
    uint8_t states[20];  // state per unit ID (0=standby,1=preview,2=program), index = unitId-1
    uint8_t brightness;
    uint8_t standbyBrightness;
    uint8_t animSpeedMs;
    bool updated;        // set true when new tally data arrives
} BridgeState;

void wsClientInit(BridgeState* state);
void wsClientTick();
bool wsClientIsConnected();
