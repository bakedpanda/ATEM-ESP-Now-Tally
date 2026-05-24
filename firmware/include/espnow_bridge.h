#pragma once
#include "ws_client.h"

void espnowBridgeInit(void (*onHeartbeat)(uint8_t unitId));
void espnowBridgeBroadcast(const BridgeState* state);
