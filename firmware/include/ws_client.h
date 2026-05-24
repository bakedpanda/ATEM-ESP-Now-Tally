#pragma once
#include <stdint.h>
#include "provisioning.h"

// State populated by server messages (bridge uses fully; receiver uses defaults)
typedef struct {
    bool    atemConnected;
    uint8_t states[20];      // index = unitId-1; 0=standby 1=preview 2=program
    uint8_t brightness;
    uint8_t standbyBrightness;
    uint8_t animSpeedMs;
    bool    updated;         // set true on new tally data
} BridgeState;

// onRole: called when server sends role assignment
typedef void (*RoleCallback)(uint8_t unitId, DeviceRole role);
// onIdentify: targetMac="" → identify self; non-empty → relay to that MAC via ESP-NOW
typedef void (*IdentifyCallback)(const char* targetMac);

void        wsClientInit(BridgeState* state, RoleCallback onRole, IdentifyCallback onIdentify);
void        wsClientTick();
bool        wsClientIsConnected();
void        wsClientSendText(const char* msg);
const char* wsClientGetMac();  // returns cached MAC string (set on first connect)
