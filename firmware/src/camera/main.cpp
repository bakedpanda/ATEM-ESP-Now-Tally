#include <Arduino.h>
#include "config.h"
#include "led_driver.h"
#include "espnow_camera.h"

void setup() {
    Serial.begin(115200);
    ledDriverInit(LED_PIN, LED_COUNT);
    ledDriverSetState(LED_STATE_AMBER_BREATH, nullptr);  // boot state
    espnowCameraInit();
    Serial.printf("Camera unit %d ready\n", UNIT_ID);
}

void loop() {
    espnowCameraTick();
    ledDriverTick();
}
