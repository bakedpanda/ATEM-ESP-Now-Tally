# ATEM ESP-NOW Tally

A wireless tally light system for Blackmagic ATEM switchers using ESP32-C3 microcontrollers and ESP-NOW for low-latency, long-range wireless communication.

## Overview

A Dockerised base station reads tally state from the ATEM switcher and distributes it to camera units via a bridge device. The bridge relays tally data over ESP-NOW to all camera units, each of which drives a 6-LED WS2812 strip showing program, preview, or standby state.

## Hardware

- **Base station:** Mac Mini, Raspberry Pi, or any Docker host
- **Bridge unit:** ESP32-C3 (with external antenna) — WiFi + ESP-NOW
- **Camera units:** ESP32-C3 (with external antenna) — ESP-NOW only
- **LEDs:** WS2812 strip, 6 LEDs per unit, powered via USB

## Features

- Supports up to 20 tally units
- Web UI for live dashboard, unit-to-camera assignment, and settings
- ATEM input names pulled live from the switcher for easy assignment
- Units identified by flashed ID — remappable in web UI without reflashing
- Auto-connects to ATEM on startup with persistent retry
- Breathing animation when connection is lost
- Config persists across Docker restarts via volume mount
- Cross-platform: runs on Mac, Linux, Raspberry Pi

## LED States

| State | Effect | Colour |
|---|---|---|
| Program (on air) | Solid | Red |
| Preview | Solid | Green |
| Standby (connected) | Solid dim | White |
| No connection to bridge | Breathing | Amber |
| Bridge connected, no ATEM | Breathing | White |

## Setup

See **[SETUP.md](SETUP.md)** for the full installation guide — base station, bridge, camera units, wiring, and web UI configuration.

---

## Related Projects

These projects were researched during design and may suit different use cases:

- **[esp-now-atem-tally](https://github.com/jkowalk/esp-now-atem-tally)** — ESP-NOW tally using a WT32-ETH01 Ethernet board as the bridge controller (Arduino/PlatformIO)
- **[ATEM Tally Light with ESP8266](https://github.com/AronHetLam/ATEM_tally_light_with_ESP8266)** — Direct WiFi tally lights using ESP8266, with optional tally server relay mode
- **[wifi-tally](https://wifi-tally.github.io/index.html)** — Hub-based tally system with Node.js hub, supports ATEM and other mixers, Lua firmware on ESP8266
- **[Tally Arbiter](https://tallyarbiter.com/)** — Full-featured, multi-switcher tally management platform (Node.js), supports a wide range of output devices and protocols
