#pragma once
#include <stdint.h>

typedef enum {
    ROLE_UNKNOWN,        // not yet received from server
    ROLE_UNPROVISIONED,  // server doesn't know this device
    ROLE_RECEIVER,       // ESP-NOW receiver (WiFi off after role received)
    ROLE_BRIDGE,         // WiFi + WebSocket + ESP-NOW broadcaster
} DeviceRole;

// Set role and unit ID received from server
void provisioningSet(uint8_t unitId, DeviceRole role);

uint8_t    provisioningGetUnitId();
DeviceRole provisioningGetRole();
void       provisioningReset();  // clear to ROLE_UNKNOWN
