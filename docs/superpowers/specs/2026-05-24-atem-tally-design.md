# ATEM Tally Light System — Design Spec
**Date:** 2026-05-24  
**Status:** Complete

---

## Overview

A wireless tally light system for the Blackmagic ATEM Mini Extreme ISO G2 (and compatible ATEM models). A Dockerised base station reads tally state from the ATEM and distributes it to ESP32-C3 camera units via a bridge device using ESP-NOW. Units display tally state on 6x WS2812 LED strips.

---

## Hardware

| Component | Hardware | Count |
|---|---|---|
| Base station | Mac Mini or Raspberry Pi (Docker) | 1 |
| Bridge unit | ESP32-C3 (external antenna) | 1 |
| Camera units | ESP32-C3 (external antenna) | Up to 19 |
| LEDs | WS2812 strip, 6 LEDs per unit | 1 per unit |

**Power:** All ESP32 units powered via USB.  
**ATEM:** Up to 8 camera inputs in use; system supports up to 20 tally units.

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                  Docker Container                    │
│                                                     │
│  ┌─────────────┐    ┌──────────────────────────┐   │
│  │   atem-     │───▶│   Node.js Server          │   │
│  │ connection  │    │  - Socket.io (bridge+UI)  │   │
│  │  (UDP 9910) │    │  - Express (web UI)       │   │
│  └─────────────┘    └──────────────────────────┘   │
└────────────────────────────┬────────────────────────┘
                             │ WiFi / Socket.io
                    ┌────────▼────────┐
                    │  Bridge ESP32-C3 │  ← unit ID 20
                    │  WiFi + ESP-NOW  │
                    │  WS2812 (6 LEDs) │
                    └────────┬────────┘
                             │ ESP-NOW broadcast
          ┌──────────────────┼──────────────────┐
   ┌──────▼──────┐   ┌───────▼──────┐   ┌───────▼──────┐
   │  Unit ID 1  │   │  Unit ID 2   │   │  Unit ID N   │
   │ ESP-NOW only│   │ ESP-NOW only │   │ ESP-NOW only │
   │ WS2812 LEDs │   │ WS2812 LEDs  │   │ WS2812 LEDs  │
   └─────────────┘   └──────────────┘   └──────────────┘
```

### Data Flow

1. `atem-connection` subscribes to tally state from the ATEM over local network (UDP port 9910), auto-connecting on startup with persistent retry.
2. Node.js server maps unit IDs to ATEM inputs via web UI config (stored in JSON, persisted via Docker volume).
3. On tally change, server builds a tally state object indexed by unit ID and pushes it to the bridge and web UI clients via Socket.io.
4. Bridge broadcasts a compact ESP-NOW packet containing tally state for all 20 unit ID slots.
5. Each camera unit reads only its own unit ID slot and updates its LEDs accordingly.
6. Camera units send ESP-NOW heartbeats to the bridge every 5 seconds.
7. Bridge forwards heartbeat data to base station via Socket.io — shown as online/offline in dashboard.

### ESP-NOW Packet Format

The bridge broadcasts a single packet to all units on every tally change, plus a keepalive every 2 seconds:

```
Byte 0:     flags — bit 0 = ATEM connected (1) / disconnected (0)
Bytes 1–2:  tally states for unit IDs 1–20, packed as 2 bits per slot
            00 = standby, 01 = preview, 10 = program, 11 = reserved
```

Total: 3 bytes. Each camera unit extracts its 2-bit slot by unit ID.

Camera units learn the bridge MAC address from the sender field of the first ESP-NOW broadcast they receive — no pre-configuration needed. This MAC is then used for heartbeat replies.

### ESP-NOW Notes

- ESP-NOW is **not a mesh network** — all camera units communicate directly with the bridge; packets do not hop.
- The bridge is positioned near the router/switcher for maximum coverage.
- Range: ~200m line-of-sight with external antennas; less through walls.
- Future extension: add a second bridge as a repeater if range becomes an issue.

---

## Unit ID System

- Each unit has a **unit ID** (1–20) set at flash time in `config.h` — this is a unique hardware identifier, not a fixed camera assignment.
- The mapping from **unit ID → ATEM input** is configured in the web UI and stored in the base station config.
- Replacing a unit: flash replacement with the same unit ID (no config changes) or assign a new ID and remap in the web UI.
- One ATEM input can be mapped to multiple unit IDs (e.g. two operators sharing a camera feed).
- Units can be left unmapped (show white standby permanently).
- The bridge (unit ID 20) can be mapped to any ATEM input via the web UI.

---

## LED States

All 6 LEDs on a unit show the same colour/effect.

| State | Effect | Colour |
|---|---|---|
| **Program** (on air) | Solid | Red `#FF0000` |
| **Preview** | Solid | Green `#00FF00` |
| **Standby** (connected, neither) | Solid dim | White `#FFFFFF` @ ~20% brightness |
| **No connection to bridge** | Breathing | Amber `#FF6000` |
| **Bridge connected, no ATEM** | Breathing | White — signalled via ATEM flag in ESP-NOW packet |

**Breathing animation:** Slow sine-wave fade in/out. Speed configurable (slow / medium / fast) via web UI.  
**Brightness:** Configurable globally (0–100%) with per-state overrides. Defaults: 80% program/preview, 20% standby.  
**Boot:** Amber breathing immediately on power-up until first ESP-NOW packet received.  
**Timeout:** If no ESP-NOW packet received for 10 seconds, unit enters amber breathing autonomously.

---

## Base Station Software

**Runtime:** Node.js in Docker  
**Cross-platform:** Runs on Mac Mini, Raspberry Pi, or any Docker host  
**ATEM library:** [`atem-connection`](https://github.com/nrkno/sofie-atem-connection) npm package (broadest ATEM model compatibility)

### Config Persistence

Config stored in `config.json`, mounted as a Docker volume:
- ATEM IP address
- Unit ID → ATEM input mappings
- LED brightness settings
- Animation speed

---

## Web Interface

Served by Express with Socket.io for live updates. Three pages:

### Dashboard (`/`)
- Live grid of all registered units: unit ID, assigned ATEM input name, current tally state (colour-coded), online/offline status
- ATEM connection status indicator (connecting / connected / retrying)
- View only — no controls

### Assignment (`/assign`)
- Table of all unit IDs seen (populated by heartbeats)
- Per unit: unit ID, last seen timestamp, ATEM input dropdown (by name, pulled live from switcher, e.g. "Camera 1", "Interview Cam") or "Unassigned"
- Save commits the mapping

### Settings (`/settings`)
- ATEM IP address (auto-connect on save, persistent retry)
- Global LED brightness slider
- Per-state brightness overrides
- Breathing animation speed

---

## Firmware

**Build tool:** PlatformIO — one project, two environments (`bridge` and `camera`)  
**Shared code:** WS2812 LED driver, tally state definitions, ESP-NOW packet format  
**Per-unit config:** `config.h` set at flash time (unit ID, WiFi credentials for bridge)

### Bridge Firmware
- Connects to WiFi → starts Socket.io client → connects to base station
- Receives tally state object from base station (indexed by unit ID)
- Broadcasts compact ESP-NOW packet: all 20 unit ID slots in one message
- Receives heartbeat packets from camera units, forwards online status via Socket.io
- Drives own WS2812 LEDs for its mapped tally state

### Camera Unit Firmware
- No WiFi stack — boots straight into ESP-NOW
- Listens for broadcast packets from the bridge MAC address
- Reads own unit ID slot, updates LEDs
- Sends heartbeat to bridge MAC every 5 seconds
- 10-second ESP-NOW timeout → amber breathing animation autonomously

---

## Docker

Single container, single `docker-compose.yml`:
- Node.js base image
- Config volume for persistence
- `atem-connection` uses unicast UDP to the configured ATEM IP (port 9910) — no multicast discovery. Bridge networking mode with explicit port mapping is sufficient; host networking is optional but simplifies setup on Linux.
- Web UI exposed on port 8259 (T-A-L-Y on a phone keypad)

---

## Future Scope (not in initial build)

- OTA firmware updates via web UI
- Second bridge unit as range extender/repeater
- Audio tally support
- Per-unit brightness override
