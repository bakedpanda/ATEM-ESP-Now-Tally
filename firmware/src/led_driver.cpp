#include "led_driver.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

static Adafruit_NeoPixel* strip = nullptr;
static uint8_t numLeds = 0;
static LedState currentState = LED_STATE_AMBER_BREATH;
static LedState previousState = LED_STATE_AMBER_BREATH;  // state before IDENTIFY
static LedSettings currentSettings = { 80, 20, 40 };
static unsigned long lastTick = 0;
static unsigned long identifyStartMs = 0;

static uint32_t applyBrightness(uint8_t r, uint8_t g, uint8_t b, uint8_t pct) {
    float scale = pct / 100.0f;
    return strip->Color((uint8_t)(r * scale), (uint8_t)(g * scale), (uint8_t)(b * scale));
}

static uint8_t breathValue(uint8_t speedMs10) {
    unsigned long period = (unsigned long)speedMs10 * 10;
    float phase = (float)(millis() % period) / period;
    float sine = (sinf(phase * 2.0f * M_PI - M_PI / 2.0f) + 1.0f) / 2.0f;
    return (uint8_t)(sine * 255);
}

void ledDriverInit(uint8_t pin, uint8_t count) {
    numLeds = count;
    strip = new Adafruit_NeoPixel(count, pin, NEO_GRB + NEO_KHZ800);
    strip->begin();
    strip->show();
}

void ledDriverSetState(LedState state, const LedSettings* settings) {
    if (state == LED_STATE_IDENTIFY) {
        if (currentState != LED_STATE_IDENTIFY) {
            previousState = currentState;  // save so we can revert
        }
        identifyStartMs = millis();
    }
    currentState = state;
    if (settings) currentSettings = *settings;
}

void ledDriverTick() {
    if (!strip) return;
    unsigned long now = millis();
    if (now - lastTick < 20) return;  // ~50Hz
    lastTick = now;

    // Auto-clear IDENTIFY after 5 seconds
    if (currentState == LED_STATE_IDENTIFY && now - identifyStartMs >= 5000) {
        currentState = previousState;
    }

    uint32_t colour = 0;
    uint8_t bright = breathValue(currentSettings.animSpeedMs);

    switch (currentState) {
        case LED_STATE_PROGRAM:
            colour = applyBrightness(255, 0, 0, currentSettings.brightness);
            break;
        case LED_STATE_PREVIEW:
            colour = applyBrightness(0, 255, 0, currentSettings.brightness);
            break;
        case LED_STATE_STANDBY:
            colour = applyBrightness(255, 255, 255, currentSettings.standbyBrightness);
            break;
        case LED_STATE_AMBER_BREATH: {
            uint8_t s = (uint8_t)((bright / 255.0f) * currentSettings.brightness * 2.55f);
            colour = strip->Color(s, (uint8_t)(s * 0.376f), 0);
            break;
        }
        case LED_STATE_WHITE_BREATH: {
            uint8_t s = (uint8_t)((bright / 255.0f) * currentSettings.brightness * 2.55f);
            colour = strip->Color(s, s, s);
            break;
        }
        case LED_STATE_PAIRING: {
            uint8_t s = (uint8_t)((bright / 255.0f) * currentSettings.brightness * 2.55f);
            colour = strip->Color(0, 0, s);  // blue only
            break;
        }
        case LED_STATE_IDENTIFY: {
            // Rapid white flash: toggle every 100ms
            uint8_t on = ((now / 100) % 2 == 0) ? 255 : 0;
            colour = strip->Color(on, on, on);
            break;
        }
    }

    for (uint8_t i = 0; i < numLeds; i++) strip->setPixelColor(i, colour);
    strip->show();
}
