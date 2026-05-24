#pragma once
#include <stdint.h>

typedef enum {
    LED_STATE_AMBER_BREATH,   // no connection to bridge
    LED_STATE_WHITE_BREATH,   // bridge ok, no ATEM
    LED_STATE_STANDBY,        // connected, not on any bus
    LED_STATE_PREVIEW,        // on preview
    LED_STATE_PROGRAM,        // on program (on air)
} LedState;

typedef struct {
    uint8_t brightness;         // 0–100 global brightness %
    uint8_t standbyBrightness;  // 0–100 standby brightness %
    uint8_t animSpeedMs;        // breathing period ms / 10 (slow=400, med=250, fast=150)
} LedSettings;

void ledDriverInit(uint8_t pin, uint8_t count);
void ledDriverSetState(LedState state, const LedSettings* settings);
void ledDriverTick();  // call every loop() iteration
