# Unified Firmware & Dynamic Role Assignment — Design Spec

**Date:** 2026-05-24
**Status:** Approved

---

## Overview

Replace the separate camera/bridge firmware builds with a single unified firmware binary. All devices are identical hardware running identical firmware. Role (bridge or receiver) and unit ID are assigned from the web UI and delivered by the server at boot time. `config.h` becomes site-wide config with no per-device values.

---

## Goals

- One firmware binary flashed to every device, no per-device `config.h` edits
- Role (bridge / receiver) and unit ID assigned and changed entirely from the web UI
- Unprovisioned devices self-identify and appear in `/assign` for first-time setup
- Identify button in `/assign` flashes a unit's LEDs for physical identification
- Role changes take effect immediately on next boot (always authoritative from server)

---

## Architecture

Every device connects to WiFi on boot, registers with the server via WebSocket, and receives its role. Receivers then disable WiFi and operate ESP-NOW only.

```
Boot (all devices)
 └─ WiFi connect (amber breath)
    └─ WebSocket → server: {"type":"hello","mac":"AA:BB:CC:DD:EE:FF"}
       ├─ Server: unprovisioned → blue pulse (await assignment in /assign)
       ├─ Server: role=bridge   → stay connected, start ESP-NOW broadcast
       └─ Server: role=receiver → WiFi off, ESP-NOW receiver mode
```

---

## config.h (identical for every device)

```cpp
#define LED_PIN       4       // GPIO pin for WS2812 strip
#define LED_COUNT     6       // Number of LEDs
#define WIFI_SSID     "your-network"
#define WIFI_PASSWORD "your-password"
#define SERVER_HOST   "192.168.1.100"  // Base station IP
#define SERVER_PORT   8259
```

No `UNIT_ID`. No per-device values.

---

## Firmware

### PlatformIO

Single `[env:tally]` build environment. All libraries included: NeoPixel, WebSockets, ArduinoJson.

```ini
[env:tally]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
monitor_speed = 115200
lib_deps =
    adafruit/Adafruit NeoPixel @ ^1.12.3
    Links2004/WebSockets @ ^2.4.1
    bblanchon/ArduinoJson @ ^7.2.1

[env:native_test]
platform = native
build_src_filter = -<*>
```

### Source structure

```
firmware/src/
  main.cpp           ← unified entry point, state machine
  provisioning.cpp   ← role/id state (in-memory, from server)
  wifi_manager.cpp   ← unchanged
  ws_client.cpp      ← sends MAC on hello, receives role message
  espnow.cpp         ← merged send+receive
  led_driver.cpp     ← adds PAIRING and IDENTIFY states
```

### Device state machine

| State | LED | Exits when |
|---|---|---|
| `WIFI_CONNECTING` | Amber breath | WiFi connects |
| `SERVER_CONNECTING` | Amber breath | WebSocket connects |
| `PROVISIONING` | Blue pulse | Server sends role |
| `BRIDGE` | Normal tally | — |
| `RECEIVER` | Normal tally | — |
| `IDENTIFY` | Fast white flash | 5 second timeout → previous state |

### LED states (additions)

| State | Colour | Effect |
|---|---|---|
| `LED_STATE_PAIRING` | Blue | Breathing (same sine wave as amber/white) |
| `LED_STATE_IDENTIFY` | White | Rapid flash, 5s self-clearing |

---

## WebSocket Protocol

Endpoint: `ws://[server]:8259/bridge` (unchanged)

### Device → Server

| Message | When |
|---|---|
| `{"type":"hello","mac":"AA:BB:CC:DD:EE:FF"}` | On connect |
| `{"type":"heartbeat","mac":"...","unitId":3}` | Every 5s (bridge or provisioning device) |
| `{"type":"heartbeat_relay","unitId":3,"mac":"..."}` | Bridge relaying a receiver's ESP-NOW heartbeat |

### Server → Device

| Message | When |
|---|---|
| `{"type":"role","unitId":3,"role":"receiver"}` | Response to hello, or on assignment change |
| `{"type":"role","unitId":20,"role":"bridge"}` | Same |
| `{"type":"role","status":"unprovisioned"}` | Unknown MAC |
| `{"type":"tally",...}` | Tally update (bridge only) |
| `{"type":"settings",...}` | Settings update (bridge only) |
| `{"type":"identify"}` | Identify this device (sent to bridge device directly) |
| `{"type":"identify","targetMac":"AA:BB:CC:DD:EE:FF"}` | Bridge: relay identify to receiver via ESP-NOW |

---

## ESP-NOW Packets

| Type byte | Name | Size | Direction |
|---|---|---|---|
| `0x00` | `TallyPacket` | 3 bytes | Bridge → all (broadcast) |
| `0x01` | `HeartbeatPacket` | 2 bytes | Receiver → bridge |
| `0x03` | `IdentifyPacket` | 1 byte | Bridge → receiver (unicast) |

The bridge maintains a `unitId → MAC` map from received heartbeats, used for:
1. Including MAC when relaying heartbeats to server
2. Sending unicast `IdentifyPacket` to a specific receiver

---

## Server

### Assignment store

Keyed by MAC address (not unit ID):

```js
{
  "AA:BB:CC:DD:EE:FF": {
    unitId: 3,
    role: "receiver",   // "bridge" | "receiver"
    atemInput: 2,       // receiver only; 0 = unassigned
    lastSeen: 1716000000000
  }
}
```

### /assign page

Each row shows: MAC (short), Unit ID, Role dropdown, ATEM Input dropdown (hidden for bridge), Identify button, Last Seen.

- Role dropdown: `Receiver` / `Bridge`
- ATEM Input dropdown: greyed out when role = bridge
- Warning banner if more than one device is assigned bridge role
- Identify button: emits `identifyUnit` Socket.io event

### Socket.io events (browser → server)

| Event | Payload |
|---|---|
| `saveAssignments` | `[{mac, unitId, role, atemInput}]` |
| `identifyUnit` | `{unitId}` |

When `saveAssignments` is received, the server immediately pushes updated `role` messages to any connected devices whose assignment changed.

### Identify routing

- Target is bridge: server sends `{"type":"identify"}` directly over its WebSocket
- Target is receiver: server sends `{"type":"identify","targetMac":"..."}` to the bridge, which sends a unicast `IdentifyPacket` via ESP-NOW

---

## Testing

- Native unit tests: update `test_packet` to cover `IdentifyPacket`; add `test_provisioning` for state machine transitions
- Server Jest tests: extend for MAC-keyed config, `role` message handling, `identifyUnit` event routing

---

## Migration from current firmware

1. Existing bridge unit: reflash with unified firmware — will connect to server, get bridge role
2. Existing camera units: reflash, appear as unprovisioned in `/assign`, assign receiver role + ATEM input
3. Config volume on Docker host: server migration converts `unitId`-keyed records to MAC-keyed on first run (or manual re-assignment via UI)

---

## Out of scope

- Multiple simultaneous bridge devices (single bridge only; UI warns on duplicate)
- BLE or captive-portal WiFi provisioning (WiFi creds remain in `config.h`)
- OTA firmware updates
