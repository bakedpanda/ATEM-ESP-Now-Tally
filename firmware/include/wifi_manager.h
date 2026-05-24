#pragma once
#include <stdint.h>

void wifiManagerInit();
bool wifiManagerIsConnected();
void wifiManagerTick();  // call every loop, handles reconnection
