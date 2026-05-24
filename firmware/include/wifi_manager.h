#pragma once

void wifiManagerInit();
bool wifiManagerIsConnected();
void wifiManagerTick();
void wifiManagerStop();  // disconnect WiFi, keep radio on for ESP-NOW channel
