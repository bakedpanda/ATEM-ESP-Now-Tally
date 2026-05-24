#include "provisioning.h"

static uint8_t    storedUnitId = 0;
static DeviceRole storedRole   = ROLE_UNKNOWN;

void provisioningSet(uint8_t unitId, DeviceRole role) {
    storedUnitId = unitId;
    storedRole   = role;
}

uint8_t    provisioningGetUnitId() { return storedUnitId; }
DeviceRole provisioningGetRole()   { return storedRole; }
void       provisioningReset()     { storedUnitId = 0; storedRole = ROLE_UNKNOWN; }
