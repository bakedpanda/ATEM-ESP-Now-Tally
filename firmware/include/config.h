#pragma once

// ── SITE CONFIG — same for every device ──────────────────────────────────────
// Flash every device with this identical file. Unit ID and role are assigned
// from the web UI after first boot.

// WS2812 LED strip
#define LED_PIN   4    // GPIO pin for data line
#define LED_COUNT 6    // Number of LEDs per unit

// WiFi (2.4 GHz only — ESP32-C3 does not support 5 GHz)
#define WIFI_SSID     "your-network"
#define WIFI_PASSWORD "your-password"

// Base station (machine running Docker)
#define SERVER_HOST   "192.168.1.100"
#define SERVER_PORT   8259
