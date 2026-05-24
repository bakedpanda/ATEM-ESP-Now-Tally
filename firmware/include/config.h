#pragma once

// ── EDIT THIS FILE BEFORE FLASHING ──────────────────────────────────────────
// Unit ID: 1–20. Must be unique across all units.
// Bridge unit: set UNIT_ID to 20 (or any unused ID)
#define UNIT_ID 1

// WS2812 LED strip — GPIO pin and LED count
#define LED_PIN   4
#define LED_COUNT 6

// ── Bridge only (ignored by camera units) ────────────────────────────────────
#define WIFI_SSID     "your-network-name"
#define WIFI_PASSWORD "your-password"
#define SERVER_HOST   "192.168.1.100"  // base station IP
#define SERVER_PORT   8259
