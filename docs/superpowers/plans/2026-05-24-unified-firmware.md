# Unified Firmware & Dynamic Role Assignment — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the separate camera/bridge firmware builds with a single unified binary; role (bridge/receiver) and unit ID assigned from the web UI at runtime.

**Architecture:** Every device connects to WiFi on boot, sends a `hello` with its MAC to the server, receives its role and ID, then either stays connected (bridge) or drops WiFi and enters ESP-NOW receiver mode. The server's assignment store is keyed by MAC address. The `/assign` page gains Role and Identify columns.

**Tech Stack:** Node.js ESM, Socket.io, ws, Jest (server) · ESP32-C3 Arduino, WebSockets, ArduinoJson, Adafruit NeoPixel, ESP-NOW (firmware) · PlatformIO

> ⚠ **Firmware build note:** After Task 5 (platformio.ini), old source directories still exist alongside new flat files. PlatformIO would see duplicate symbols if built early. Do NOT run `pio run` until Task 15 — the cleanup in Task 14 removes the old files first.

---

## File Map

**Server — modified:**
- `server/src/tally.js` — update `buildUnitStates` for MAC-keyed units
- `server/src/config.js` — add migration from old unitId-keyed format
- `server/src/socket.js` — full rewrite: `connectedDevices` map, hello/role, heartbeat_relay, identifyUnit
- `server/views/assign.html` — add Role dropdown, Identify button, multi-bridge warning
- `server/tests/tally.test.js` — update for new unit shape
- `server/tests/config.test.js` — add migration tests

**Firmware — new files (flat `firmware/src/`):**
- `firmware/include/provisioning.h` + `firmware/src/provisioning.cpp` — in-memory role/id state
- `firmware/include/espnow.h` + `firmware/src/espnow.cpp` — merged send/receive, identify, heartbeat relay

**Firmware — modified:**
- `firmware/platformio.ini` — merge camera/bridge into single `tally` env
- `firmware/include/tally_packet.h` — add `IdentifyPacket`
- `firmware/include/led_driver.h` — add `LED_STATE_PAIRING`, `LED_STATE_IDENTIFY`
- `firmware/include/config.h` — remove `UNIT_ID`
- `firmware/include/wifi_manager.h` — add `wifiManagerStop()`
- `firmware/include/ws_client.h` — new callbacks, `wsClientGetMac()`
- `firmware/src/ws_client.cpp` — hello+MAC, role message, identify handling (moved from bridge/)
- `firmware/src/wifi_manager.cpp` — add `wifiManagerStop()` (moved from bridge/)
- `firmware/src/led_driver.cpp` — add PAIRING + IDENTIFY rendering (moved from shared/)
- `firmware/src/main.cpp` — unified state machine (replaces bridge/main.cpp + camera/main.cpp)
- `firmware/test/native/test_packet/test_packet.cpp` — add IdentifyPacket tests

**Firmware — deleted:**
- `firmware/src/bridge/` — entire directory
- `firmware/src/camera/` — entire directory
- `firmware/src/shared/` — entire directory
- `firmware/include/espnow_camera.h`
- `firmware/include/espnow_bridge.h`

---

## Task 1 — Update `tally.js`: MAC-keyed `buildUnitStates`

**Files:**
- Modify: `server/src/tally.js`
- Modify: `server/tests/tally.test.js`

Units are now `{ [mac]: { role, unitId, atemInput } }`. Only `receiver` role entries with a `unitId` produce tally states. Result is still keyed by `unitId` string.

- [ ] **Step 1: Update the failing tests first**

Replace the contents of `server/tests/tally.test.js`:

```js
import { buildUnitStates } from '../src/tally.js'

// units shape: { [mac]: { role: 'receiver'|'bridge', unitId: number, atemInput: number } }

test('receiver mapped to program input returns state 2', () => {
  const states = buildUnitStates(
    { 1: { program: true, preview: false } },
    { 'AA:BB:CC:DD:EE:01': { role: 'receiver', unitId: 3, atemInput: 1 } }
  )
  expect(states['3']).toBe(2)
})

test('receiver mapped to preview input returns state 1', () => {
  const states = buildUnitStates(
    { 2: { program: false, preview: true } },
    { 'AA:BB:CC:DD:EE:02': { role: 'receiver', unitId: 5, atemInput: 2 } }
  )
  expect(states['5']).toBe(1)
})

test('receiver mapped to inactive input returns state 0', () => {
  const states = buildUnitStates(
    { 1: { program: false, preview: false } },
    { 'AA:BB:CC:DD:EE:03': { role: 'receiver', unitId: 3, atemInput: 1 } }
  )
  expect(states['3']).toBe(0)
})

test('receiver with atemInput 0 returns state 0', () => {
  const states = buildUnitStates(
    { 1: { program: true, preview: false } },
    { 'AA:BB:CC:DD:EE:04': { role: 'receiver', unitId: 7, atemInput: 0 } }
  )
  expect(states['7']).toBe(0)
})

test('bridge role entry is skipped', () => {
  const states = buildUnitStates(
    { 1: { program: true, preview: false } },
    { 'AA:BB:CC:DD:EE:05': { role: 'bridge', unitId: 20, atemInput: 1 } }
  )
  expect(states['20']).toBeUndefined()
})

test('entry with no unitId is skipped', () => {
  const states = buildUnitStates(
    { 1: { program: true, preview: false } },
    { 'AA:BB:CC:DD:EE:06': { role: 'receiver', unitId: null, atemInput: 1 } }
  )
  expect(Object.keys(states).length).toBe(0)
})

test('multiple receivers can share an ATEM input', () => {
  const states = buildUnitStates(
    { 3: { program: true, preview: false } },
    {
      'AA:BB:CC:DD:EE:07': { role: 'receiver', unitId: 1, atemInput: 3 },
      'AA:BB:CC:DD:EE:08': { role: 'receiver', unitId: 2, atemInput: 3 },
    }
  )
  expect(states['1']).toBe(2)
  expect(states['2']).toBe(2)
})

test('program takes precedence over preview', () => {
  const states = buildUnitStates(
    { 1: { program: true, preview: true } },
    { 'AA:BB:CC:DD:EE:09': { role: 'receiver', unitId: 1, atemInput: 1 } }
  )
  expect(states['1']).toBe(2)
})
```

- [ ] **Step 2: Run tests — expect failures**

```bash
cd server && npm test -- --testPathPattern=tally
```

Expected: FAIL (old `buildUnitStates` doesn't understand `role` field)

- [ ] **Step 3: Update `server/src/tally.js`**

```js
// buildUnitStates: returns { [unitId: string]: 0|1|2 }
// units: { [mac]: { role: 'receiver'|'bridge', unitId: number, atemInput: number } }
export function buildUnitStates(atemTallys, units) {
  const states = {}
  for (const [, cfg] of Object.entries(units)) {
    if (cfg.role !== 'receiver' || !cfg.unitId) continue
    const id = String(cfg.unitId)
    const input = cfg.atemInput ?? 0
    if (!input) { states[id] = 0; continue }
    const tally = atemTallys[input]
    if (!tally) { states[id] = 0; continue }
    states[id] = tally.program ? 2 : tally.preview ? 1 : 0
  }
  return states
}
```

- [ ] **Step 4: Run tests — expect all pass**

```bash
cd server && npm test -- --testPathPattern=tally
```

Expected: PASS (8 tests)

- [ ] **Step 5: Commit**

```bash
git add server/src/tally.js server/tests/tally.test.js
git commit -m "feat(server): buildUnitStates accepts MAC-keyed units with role field"
```

---

## Task 2 — Update `config.js`: MAC-keyed units + old-format migration

**Files:**
- Modify: `server/src/config.js`
- Modify: `server/tests/config.test.js`

Old config files have `units` keyed by unitId string (e.g. `"3": { atemInput: 1 }`). On load, if units are NOT MAC-keyed, clear them so re-assignment is required.

- [ ] **Step 1: Add migration tests to `server/tests/config.test.js`**

Append to the existing file:

```js
test('old unitId-keyed units are cleared on load (migration)', () => {
  writeFileSync(TMP, JSON.stringify({
    units: { '3': { atemInput: 2 }, '5': { atemInput: 1 } }
  }))
  const c = readConfig(TMP)
  expect(c.units).toEqual({})
})

test('new MAC-keyed units are preserved on load', () => {
  const units = {
    'AA:BB:CC:DD:EE:01': { unitId: 3, role: 'receiver', atemInput: 2 },
  }
  writeFileSync(TMP, JSON.stringify({ units }))
  const c = readConfig(TMP)
  expect(c.units['AA:BB:CC:DD:EE:01'].unitId).toBe(3)
  expect(c.units['AA:BB:CC:DD:EE:01'].role).toBe('receiver')
})
```

- [ ] **Step 2: Run tests — expect 2 new failures**

```bash
cd server && npm test -- --testPathPattern=config
```

Expected: 2 new tests FAIL

- [ ] **Step 3: Update `server/src/config.js`**

```js
import { readFileSync, writeFileSync, existsSync } from 'fs'

export const DEFAULT_CONFIG = {
  atem: { ip: '' },
  leds: {
    brightness: 80,
    programBrightness: 80,
    previewBrightness: 80,
    standbyBrightness: 20,
    animSpeed: 'slow',
  },
  units: {},
}

// MAC addresses contain colons; old unitId keys do not
function isMacKeyed(units) {
  const keys = Object.keys(units)
  return keys.length === 0 || keys.every(k => k.includes(':'))
}

export function readConfig(path) {
  if (!existsSync(path)) return structuredClone(DEFAULT_CONFIG)
  try {
    const saved = JSON.parse(readFileSync(path, 'utf8'))
    const units = saved.units ?? {}
    return {
      atem: { ...DEFAULT_CONFIG.atem, ...saved.atem },
      leds: { ...DEFAULT_CONFIG.leds, ...saved.leds },
      units: isMacKeyed(units) ? units : {},  // clear old format
    }
  } catch {
    return structuredClone(DEFAULT_CONFIG)
  }
}

export function writeConfig(path, config) {
  writeFileSync(path, JSON.stringify(config, null, 2))
}
```

- [ ] **Step 4: Run all server tests — expect all pass**

```bash
cd server && npm test
```

Expected: PASS (all existing + 2 new)

- [ ] **Step 5: Commit**

```bash
git add server/src/config.js server/tests/config.test.js
git commit -m "feat(server): MAC-keyed units store with auto-migration from old format"
```

---

## Task 3 — Rewrite `server/src/socket.js`

**Files:**
- Modify: `server/src/socket.js`

Key changes: track all device WebSocket connections by MAC; handle `hello`/`role` protocol; route `identifyUnit` to bridge directly or via bridge for receivers; `saveAssignments` accepts new array shape and pushes role updates to connected devices.

- [ ] **Step 1: Replace `server/src/socket.js` entirely**

```js
import { Server as SocketIO } from 'socket.io'
import { WebSocketServer } from 'ws'
import { buildUnitStates } from './tally.js'

export function createSocketServer(httpServer, atemManager, getConfig, saveConfig) {
  const io = new SocketIO(httpServer, { cors: { origin: '*' } })
  const wss = new WebSocketServer({ server: httpServer, path: '/bridge' })

  // knownUnits: { [mac]: { lastSeen: number } } — populated by hello/heartbeat
  let knownUnits = {}
  // connectedDevices: Map<WebSocket, { mac, role, unitId }> — all active WS connections
  let connectedDevices = new Map()
  // bridgeWs: the WebSocket connection of the device with role=bridge
  let bridgeWs = null
  let lastTallys = {}
  let lastInputNames = {}
  let atemConnected = false

  // ── ATEM events ────────────────────────────────────────────────────────────
  atemManager.on('status', (status) => {
    atemConnected = status === 'connected'
    io.emit('atemStatus', status)
    pushTallyToBridge()
  })

  atemManager.on('tally', (tallys, inputNames) => {
    lastTallys = tallys
    lastInputNames = inputNames
    const cfg = getConfig()
    const states = buildUnitStates(tallys, cfg.units)
    io.emit('tally', { atemConnected, states })
    io.emit('inputNames', inputNames)
    pushTallyToBridge(states)
  })

  function pushTallyToBridge(states) {
    if (!bridgeWs || bridgeWs.readyState !== 1) return
    const cfg = getConfig()
    if (!states) states = buildUnitStates(lastTallys, cfg.units)
    bridgeWs.send(JSON.stringify({ type: 'tally', atemConnected, states }))
  }

  function pushSettingsToBridge() {
    if (!bridgeWs || bridgeWs.readyState !== 1) return
    const { leds } = getConfig()
    bridgeWs.send(JSON.stringify({
      type: 'settings',
      brightness: leds.brightness,
      standbyBrightness: leds.standbyBrightness,
      animSpeed: leds.animSpeed,
    }))
  }

  // ── Device WebSocket (/bridge) ─────────────────────────────────────────────
  wss.on('connection', (ws) => {
    connectedDevices.set(ws, { mac: null, role: null, unitId: null })

    ws.on('message', (data) => {
      try {
        const msg = JSON.parse(data.toString())
        const deviceInfo = connectedDevices.get(ws)

        if (msg.type === 'hello') {
          const mac = msg.mac
          deviceInfo.mac = mac
          const cfg = getConfig()
          const assignment = cfg.units[mac]

          if (!assignment) {
            ws.send(JSON.stringify({ type: 'role', status: 'unprovisioned' }))
          } else {
            deviceInfo.role = assignment.role
            deviceInfo.unitId = assignment.unitId
            ws.send(JSON.stringify({
              type: 'role',
              unitId: assignment.unitId,
              role: assignment.role,
            }))
            if (assignment.role === 'bridge') {
              bridgeWs = ws
              io.emit('bridgeStatus', 'connected')
              pushSettingsToBridge()
              pushTallyToBridge()
            }
          }

          knownUnits[mac] = { lastSeen: Date.now() }
          io.emit('units', formatUnits(knownUnits, getConfig()))
        }

        if (msg.type === 'heartbeat') {
          // Bridge sending its own heartbeat
          const mac = msg.mac || deviceInfo.mac
          if (mac) {
            knownUnits[mac] = { lastSeen: Date.now() }
            io.emit('units', formatUnits(knownUnits, getConfig()))
          }
        }

        if (msg.type === 'heartbeat_relay') {
          // Bridge relaying a receiver's heartbeat
          const mac = msg.mac
          if (mac) {
            knownUnits[mac] = { lastSeen: Date.now() }
            io.emit('units', formatUnits(knownUnits, getConfig()))
          }
        }
      } catch { /* ignore malformed */ }
    })

    ws.on('close', () => {
      if (bridgeWs === ws) {
        bridgeWs = null
        io.emit('bridgeStatus', 'disconnected')
      }
      connectedDevices.delete(ws)
    })
  })

  // ── Browser Socket.io ──────────────────────────────────────────────────────
  io.on('connection', (socket) => {
    const cfg = getConfig()
    socket.emit('atemStatus', atemConnected ? 'connected' : 'disconnected')
    socket.emit('bridgeStatus', bridgeWs ? 'connected' : 'disconnected')
    socket.emit('units', formatUnits(knownUnits, cfg))
    socket.emit('inputNames', lastInputNames)
    socket.emit('tally', { atemConnected, states: buildUnitStates(lastTallys, cfg.units) })

    socket.on('saveAssignments', (assignments) => {
      // assignments: [{ mac, unitId, role, atemInput }]
      const cfg = getConfig()
      const oldUnits = { ...cfg.units }

      cfg.units = {}
      for (const a of assignments) {
        cfg.units[a.mac] = {
          unitId: Number(a.unitId) || 0,
          role: a.role,
          atemInput: Number(a.atemInput) || 0,
        }
      }
      saveConfig(cfg)

      // Push updated role to any currently-connected devices whose assignment changed
      for (const [ws, deviceInfo] of connectedDevices) {
        if (!deviceInfo.mac) continue
        const newA = cfg.units[deviceInfo.mac]
        const oldA = oldUnits[deviceInfo.mac]
        if (!newA) continue
        if (JSON.stringify(newA) !== JSON.stringify(oldA)) {
          ws.send(JSON.stringify({ type: 'role', unitId: newA.unitId, role: newA.role }))
          if (newA.role === 'bridge') {
            bridgeWs = ws
            io.emit('bridgeStatus', 'connected')
          }
        }
      }

      pushTallyToBridge()
      io.emit('units', formatUnits(knownUnits, getConfig()))
    })

    socket.on('identifyUnit', ({ unitId }) => {
      const cfg = getConfig()
      const entry = Object.entries(cfg.units).find(([, v]) => v.unitId === unitId)
      if (!entry) return
      const [mac, assignment] = entry

      if (assignment.role === 'bridge') {
        // Send identify directly to bridge WebSocket
        if (bridgeWs && bridgeWs.readyState === 1) {
          bridgeWs.send(JSON.stringify({ type: 'identify' }))
        }
      } else {
        // Ask bridge to relay identify via ESP-NOW unicast
        if (bridgeWs && bridgeWs.readyState === 1) {
          bridgeWs.send(JSON.stringify({ type: 'identify', targetMac: mac }))
        }
      }
    })

    socket.on('saveSettings', (settings) => {
      const cfg = getConfig()
      const oldIp = cfg.atem.ip
      cfg.atem.ip = settings.ip ?? cfg.atem.ip
      cfg.leds = { ...cfg.leds, ...settings.leds }
      saveConfig(cfg)
      pushSettingsToBridge()
      if (settings.ip && settings.ip !== oldIp) {
        atemManager.disconnect()
        atemManager.connect(settings.ip)
      }
    })
  })

  return { io, wss, getKnownUnits: () => knownUnits, getInputNames: () => lastInputNames }
}

function formatUnits(knownUnits, cfg) {
  const now = Date.now()
  const result = {}

  // All seen units (have lastSeen data)
  for (const [mac, u] of Object.entries(knownUnits)) {
    result[mac] = {
      mac,
      ...(cfg.units[mac] ?? {}),
      online: (now - u.lastSeen) < 15000,
      lastSeen: u.lastSeen,
    }
  }

  // Configured units not yet seen
  for (const [mac, assignment] of Object.entries(cfg.units)) {
    if (!result[mac]) {
      result[mac] = { mac, ...assignment, online: false, lastSeen: null }
    }
  }

  return Object.values(result)
}
```

- [ ] **Step 2: Run all server tests — ensure still passing**

```bash
cd server && npm test
```

Expected: PASS (no socket.js unit tests exist; existing tests for tally.js and config.js should still pass)

- [ ] **Step 3: Commit**

```bash
git add server/src/socket.js
git commit -m "feat(server): rewrite socket.js — MAC-keyed devices, hello/role protocol, identifyUnit routing"
```

---

## Task 4 — Update `/assign` view

**Files:**
- Modify: `server/views/assign.html`

Adds Role dropdown, Identify button per row, multi-bridge warning banner. Removes dependency on `/api/config` fetch — all data comes from Socket.io `units` event.

- [ ] **Step 1: Replace `server/views/assign.html`**

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>ATEM Tally — Assign</title>
  <link rel="stylesheet" href="/app.css">
  <style>
    .bridge-warn {
      background: #c0392b; color: #fff;
      padding: 8px 14px; border-radius: 4px;
      margin-bottom: 14px; display: none;
    }
    select[disabled] { opacity: 0.35; pointer-events: none; }
    .identify-btn {
      background: #444; color: #fff; border: none;
      padding: 4px 10px; border-radius: 3px; cursor: pointer;
      font-size: 0.85em;
    }
    .identify-btn:hover { background: #666; }
    code { font-size: 0.9em; }
  </style>
</head>
<body>
  <nav>
    <a href="/">Dashboard</a>
    <a href="/assign" class="active">Assign</a>
    <a href="/settings">Settings</a>
  </nav>
  <h1>Unit Assignments</h1>
  <div class="bridge-warn" id="bridge-warn">
    ⚠ More than one unit is set to Bridge — only one bridge is supported.
  </div>
  <table>
    <thead><tr>
      <th>MAC</th>
      <th>Unit ID</th>
      <th>Role</th>
      <th>ATEM Input</th>
      <th>Last Seen</th>
      <th></th>
    </tr></thead>
    <tbody id="tbody"></tbody>
  </table>
  <button id="save-btn">Save Assignments</button>
  <span class="saved-msg" id="saved-msg" style="display:none">Saved ✓</span>

  <script src="/socket.io/socket.io.js"></script>
  <script>
    const socket = io()
    let units = [], inputNames = {}

    socket.on('units', u => { units = u; render() })
    socket.on('inputNames', n => { inputNames = n; render() })

    function shortMac(mac) {
      return mac ? mac.slice(-8) : '—'
    }

    function render() {
      const sorted = [...units].sort((a, b) => (a.unitId ?? 99) - (b.unitId ?? 99))
      const inputOptions = [
        ['0', 'Unassigned'],
        ...Object.entries(inputNames).map(([k, v]) => [k, v])
      ]

      document.getElementById('tbody').innerHTML = sorted.map(u => {
        const mac = u.mac
        const unitIdVal = u.unitId ?? ''
        const role = u.role ?? 'receiver'
        const isBridge = role === 'bridge'
        const ago = u.lastSeen
          ? Math.round((Date.now() - u.lastSeen) / 1000) + 's ago'
          : 'never'
        const dot = u.online ? '🟢' : '⚫'

        const inputOpts = inputOptions.map(([v, l]) =>
          `<option value="${v}" ${Number(v) === (u.atemInput ?? 0) ? 'selected' : ''}>${l}</option>`
        ).join('')

        const identifyBtn = u.unitId
          ? `<button class="identify-btn" data-unitid="${u.unitId}">Identify</button>`
          : ''

        return `<tr data-mac="${mac}">
          <td><code title="${mac}">${shortMac(mac)}</code></td>
          <td><input type="number" min="1" max="20" value="${unitIdVal}"
               class="unitid-input" style="width:52px"></td>
          <td>
            <select class="role-select">
              <option value="receiver" ${!isBridge ? 'selected' : ''}>Receiver</option>
              <option value="bridge" ${isBridge ? 'selected' : ''}>Bridge</option>
            </select>
          </td>
          <td>
            <select class="input-select" ${isBridge ? 'disabled' : ''}>
              ${inputOpts}
            </select>
          </td>
          <td>${dot} ${ago}</td>
          <td>${identifyBtn}</td>
        </tr>`
      }).join('')

      // Role dropdown → toggle ATEM input disabled state
      document.querySelectorAll('.role-select').forEach(sel => {
        sel.addEventListener('change', () => {
          sel.closest('tr').querySelector('.input-select').disabled =
            sel.value === 'bridge'
          checkMultiBridge()
        })
      })

      // Identify buttons
      document.querySelectorAll('.identify-btn').forEach(btn => {
        btn.addEventListener('click', () => {
          socket.emit('identifyUnit', { unitId: Number(btn.dataset.unitid) })
        })
      })

      checkMultiBridge()
    }

    function checkMultiBridge() {
      const count = [...document.querySelectorAll('.role-select')]
        .filter(s => s.value === 'bridge').length
      document.getElementById('bridge-warn').style.display =
        count > 1 ? 'block' : 'none'
    }

    document.getElementById('save-btn').addEventListener('click', () => {
      const assignments = []
      document.querySelectorAll('tr[data-mac]').forEach(row => {
        const mac = row.dataset.mac
        const unitId = Number(row.querySelector('.unitid-input').value) || 0
        const role = row.querySelector('.role-select').value
        const atemInput = Number(row.querySelector('.input-select').value) || 0
        if (mac) assignments.push({ mac, unitId, role, atemInput })
      })
      socket.emit('saveAssignments', assignments)
      const msg = document.getElementById('saved-msg')
      msg.style.display = 'inline'
      setTimeout(() => msg.style.display = 'none', 2000)
    })
  </script>
</body>
</html>
```

- [ ] **Step 2: Commit**

```bash
git add server/views/assign.html
git commit -m "feat(server): update /assign — Role dropdown, Identify button, multi-bridge warning"
```

---

## Task 5 — Update `firmware/platformio.ini`

**Files:**
- Modify: `firmware/platformio.ini`

Merge `camera` and `bridge` environments into a single `tally` env with all libraries.

- [ ] **Step 1: Replace `firmware/platformio.ini`**

```ini
[platformio]
default_envs = tally

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

- [ ] **Step 2: Commit**

```bash
git add firmware/platformio.ini
git commit -m "build: merge camera/bridge envs into single unified tally env"
```

---

## Task 6 — Add `IdentifyPacket` to `tally_packet.h` + native test

**Files:**
- Modify: `firmware/include/tally_packet.h`
- Modify: `firmware/test/native/test_packet/test_packet.cpp`

- [ ] **Step 1: Add `IdentifyPacket` to `firmware/include/tally_packet.h`**

Add after the `HeartbeatPacket` struct definition:

```cpp
// Identify: bridge → receiver (unicast) (1 byte)
typedef struct __attribute__((packed)) {
    uint8_t type;  // always 0x03
} IdentifyPacket;
```

- [ ] **Step 2: Add tests to `firmware/test/native/test_packet/test_packet.cpp`**

Add before `int main()`:

```cpp
void test_identify_packet_size_is_1_byte() {
    TEST_ASSERT_EQUAL(1, sizeof(IdentifyPacket));
}

void test_identify_packet_type_field() {
    IdentifyPacket pkt = { 0x03 };
    TEST_ASSERT_EQUAL(0x03, pkt.type);
}
```

Add inside `int main()` before `return UNITY_END()`:

```cpp
RUN_TEST(test_identify_packet_size_is_1_byte);
RUN_TEST(test_identify_packet_type_field);
```

- [ ] **Step 3: Run native tests**

```bash
cd firmware && pio test -e native_test
```

Expected: 10 tests PASS (8 original + 2 new)

- [ ] **Step 4: Commit**

```bash
git add firmware/include/tally_packet.h firmware/test/native/test_packet/test_packet.cpp
git commit -m "feat(firmware): add IdentifyPacket type to tally_packet.h"
```

---

## Task 7 — Add `LED_STATE_PAIRING` and `LED_STATE_IDENTIFY` to LED driver

**Files:**
- Modify: `firmware/include/led_driver.h`
- Create: `firmware/src/led_driver.cpp` (moved from `firmware/src/shared/led_driver.cpp`, then modified)

`LED_STATE_PAIRING` = blue breathing (same sine wave, blue channel only).
`LED_STATE_IDENTIFY` = rapid white flash at 100ms interval, auto-clears after 5 seconds.

- [ ] **Step 1: Update `firmware/include/led_driver.h`**

Replace the file contents:

```cpp
#pragma once
#include <stdint.h>

typedef enum {
    LED_STATE_AMBER_BREATH,   // no bridge / no WiFi
    LED_STATE_WHITE_BREATH,   // bridge ok, no ATEM
    LED_STATE_STANDBY,        // connected, not on any bus
    LED_STATE_PREVIEW,        // on preview
    LED_STATE_PROGRAM,        // on program (on air)
    LED_STATE_PAIRING,        // blue breathing — unprovisioned
    LED_STATE_IDENTIFY,       // rapid white flash 5s, then auto-reverts
} LedState;

typedef struct {
    uint8_t brightness;         // 0–100 global brightness %
    uint8_t standbyBrightness;  // 0–100 standby brightness %
    uint8_t animSpeedMs;        // breathing period ms / 10 (slow=40, med=25, fast=15)
} LedSettings;

void ledDriverInit(uint8_t pin, uint8_t count);
void ledDriverSetState(LedState state, const LedSettings* settings);
void ledDriverTick();  // call every loop() iteration
```

- [ ] **Step 2: Create `firmware/src/led_driver.cpp`**

```cpp
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
```

- [ ] **Step 3: Commit**

```bash
git add firmware/include/led_driver.h firmware/src/led_driver.cpp
git commit -m "feat(firmware): add LED_STATE_PAIRING (blue) and LED_STATE_IDENTIFY (5s flash)"
```

---

## Task 8 — Update `firmware/include/config.h`

**Files:**
- Modify: `firmware/include/config.h`

Remove `UNIT_ID` — no per-device values.

- [ ] **Step 1: Replace `firmware/include/config.h`**

```cpp
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
```

- [ ] **Step 2: Commit**

```bash
git add firmware/include/config.h
git commit -m "feat(firmware): remove UNIT_ID from config.h — all values now site-wide"
```

---

## Task 9 — Create `provisioning.h` + `provisioning.cpp`

**Files:**
- Create: `firmware/include/provisioning.h`
- Create: `firmware/src/provisioning.cpp`

In-memory only — stores role and unit ID received from server at runtime.

- [ ] **Step 1: Create `firmware/include/provisioning.h`**

```cpp
#pragma once
#include <stdint.h>

typedef enum {
    ROLE_UNKNOWN,        // not yet received from server
    ROLE_UNPROVISIONED,  // server doesn't know this device
    ROLE_RECEIVER,       // ESP-NOW receiver (WiFi off after role received)
    ROLE_BRIDGE,         // WiFi + WebSocket + ESP-NOW broadcaster
} DeviceRole;

// Set role and unit ID received from server
void provisioningSet(uint8_t unitId, DeviceRole role);

uint8_t    provisioningGetUnitId();
DeviceRole provisioningGetRole();
void       provisioningReset();  // clear to ROLE_UNKNOWN
```

- [ ] **Step 2: Create `firmware/src/provisioning.cpp`**

```cpp
#include "provisioning.h"

static uint8_t    storedUnitId = 0;
static DeviceRole storedRole   = ROLE_UNKNOWN;

void provisioningSet(uint8_t unitId, DeviceRole role) {
    storedUnitId = unitId;
    storedRole   = role;
}

uint8_t    provisioningGetUnitId() { return storedUnitId; }
DeviceRole provisioningGetRole()   { return storedRole; }
void       provisioningReset()     { storedUnitId = 0; storedRole = ROLE_UNKNOWN; }
```

- [ ] **Step 3: Commit**

```bash
git add firmware/include/provisioning.h firmware/src/provisioning.cpp
git commit -m "feat(firmware): add provisioning module — in-memory role/id from server"
```

---

## Task 10 — Update `wifi_manager`: add `wifiManagerStop()`

**Files:**
- Create: `firmware/src/wifi_manager.cpp` (moved from `firmware/src/bridge/wifi_manager.cpp`, modified)
- Modify: `firmware/include/wifi_manager.h`

`wifiManagerStop()` disconnects from the AP while keeping the radio in STA mode so ESP-NOW can continue using the preserved channel.

- [ ] **Step 1: Update `firmware/include/wifi_manager.h`**

```cpp
#pragma once

void wifiManagerInit();
bool wifiManagerIsConnected();
void wifiManagerTick();
void wifiManagerStop();  // disconnect WiFi, keep radio on for ESP-NOW channel
```

- [ ] **Step 2: Create `firmware/src/wifi_manager.cpp`**

```cpp
#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>

static bool connected = false;
static unsigned long lastAttempt = 0;
static const unsigned long RETRY_INTERVAL_MS = 5000;

void wifiManagerInit() {
    WiFi.mode(WIFI_AP_STA);  // AP_STA keeps ESP-NOW on same channel as AP
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastAttempt = millis();
}

bool wifiManagerIsConnected() { return connected; }

void wifiManagerTick() {
    bool nowConnected = WiFi.status() == WL_CONNECTED;
    if (nowConnected && !connected) {
        connected = true;
        Serial.printf("WiFi connected. IP: %s  Channel: %d\n",
            WiFi.localIP().toString().c_str(), WiFi.channel());
    }
    if (!nowConnected && connected) {
        connected = false;
        Serial.println("WiFi disconnected — retrying...");
    }
    if (!nowConnected && millis() - lastAttempt > RETRY_INTERVAL_MS) {
        lastAttempt = millis();
        WiFi.reconnect();
    }
}

void wifiManagerStop() {
    // Disconnect from AP but keep radio active in STA mode.
    // ESP-NOW preserves the channel after disconnect so peers remain reachable.
    WiFi.disconnect(false);  // false = keep radio on (wifioff=false)
    connected = false;
    Serial.println("WiFi stopped — radio on, channel preserved for ESP-NOW");
}
```

- [ ] **Step 3: Commit**

```bash
git add firmware/include/wifi_manager.h firmware/src/wifi_manager.cpp
git commit -m "feat(firmware): add wifiManagerStop() — disconnects WiFi, preserves ESP-NOW channel"
```

---

## Task 11 — Rewrite `ws_client.h` + `ws_client.cpp`

**Files:**
- Modify: `firmware/include/ws_client.h`
- Create: `firmware/src/ws_client.cpp` (replaces `firmware/src/bridge/ws_client.cpp`)

On connect: sends `hello` with MAC. Receives `role` → calls `onRole` callback. Receives `identify` → calls `onIdentify` callback with `targetMac` (empty string = identify self, non-empty = relay to that MAC). Tally and settings messages update `BridgeState` as before.

- [ ] **Step 1: Replace `firmware/include/ws_client.h`**

```cpp
#pragma once
#include <stdint.h>
#include "provisioning.h"

// State populated by server messages (bridge uses fully; receiver uses defaults)
typedef struct {
    bool    atemConnected;
    uint8_t states[20];      // index = unitId-1; 0=standby 1=preview 2=program
    uint8_t brightness;
    uint8_t standbyBrightness;
    uint8_t animSpeedMs;
    bool    updated;         // set true on new tally data
} BridgeState;

// onRole: called when server sends role assignment
typedef void (*RoleCallback)(uint8_t unitId, DeviceRole role);
// onIdentify: targetMac="" → identify self; non-empty → relay to that MAC via ESP-NOW
typedef void (*IdentifyCallback)(const char* targetMac);

void        wsClientInit(BridgeState* state, RoleCallback onRole, IdentifyCallback onIdentify);
void        wsClientTick();
bool        wsClientIsConnected();
void        wsClientSendText(const char* msg);
const char* wsClientGetMac();  // returns cached MAC string (set on first connect)
```

- [ ] **Step 2: Create `firmware/src/ws_client.cpp`**

```cpp
#include "ws_client.h"
#include "config.h"
#include "provisioning.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static WebSocketsClient ws;
static BridgeState*     bridgeState     = nullptr;
static RoleCallback     roleCallback    = nullptr;
static IdentifyCallback identifyCallback = nullptr;
static bool             connected       = false;
static char             cachedMac[18]   = {};  // "AA:BB:CC:DD:EE:FF\0"

static uint8_t animSpeedToMs(const char* speed) {
    if (strcmp(speed, "fast") == 0)   return 15;
    if (strcmp(speed, "medium") == 0) return 25;
    return 40;  // slow / default
}

static void onMessage(uint8_t* payload, size_t length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length) != DeserializationError::Ok) return;

    const char* type = doc["type"];
    if (!type) return;

    if (strcmp(type, "role") == 0) {
        const char* status = doc["status"];
        if (status && strcmp(status, "unprovisioned") == 0) {
            if (roleCallback) roleCallback(0, ROLE_UNPROVISIONED);
        } else {
            uint8_t     uid  = doc["unitId"] | 0;
            const char* role = doc["role"]   | "receiver";
            DeviceRole  r    = (strcmp(role, "bridge") == 0) ? ROLE_BRIDGE : ROLE_RECEIVER;
            if (roleCallback) roleCallback(uid, r);
        }
        return;
    }

    if (strcmp(type, "tally") == 0 && bridgeState) {
        bridgeState->atemConnected = doc["atemConnected"] | false;
        JsonObject states = doc["states"].as<JsonObject>();
        for (JsonPair kv : states) {
            int uid = atoi(kv.key().c_str());
            if (uid >= 1 && uid <= 20)
                bridgeState->states[uid - 1] = kv.value().as<uint8_t>();
        }
        bridgeState->updated = true;
        return;
    }

    if (strcmp(type, "settings") == 0 && bridgeState) {
        bridgeState->brightness        = doc["brightness"]        | 80;
        bridgeState->standbyBrightness = doc["standbyBrightness"] | 20;
        bridgeState->animSpeedMs       = animSpeedToMs(doc["animSpeed"] | "slow");
        return;
    }

    if (strcmp(type, "identify") == 0) {
        const char* targetMac = doc["targetMac"] | "";
        if (identifyCallback) identifyCallback(targetMac);
        return;
    }
}

static void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            connected = true;
            // Cache and send MAC in hello message
            uint8_t macBytes[6];
            WiFi.macAddress(macBytes);
            snprintf(cachedMac, sizeof(cachedMac),
                "%02X:%02X:%02X:%02X:%02X:%02X",
                macBytes[0], macBytes[1], macBytes[2],
                macBytes[3], macBytes[4], macBytes[5]);
            char msg[64];
            snprintf(msg, sizeof(msg), "{\"type\":\"hello\",\"mac\":\"%s\"}", cachedMac);
            ws.sendTXT(msg);
            Serial.printf("WS connected — MAC %s\n", cachedMac);
            break;
        }
        case WStype_DISCONNECTED:
            connected = false;
            Serial.println("WS disconnected");
            break;
        case WStype_TEXT:
            onMessage(payload, length);
            break;
        default: break;
    }
}

void wsClientInit(BridgeState* state, RoleCallback onRole, IdentifyCallback onIdentify) {
    bridgeState      = state;
    roleCallback     = onRole;
    identifyCallback = onIdentify;
    if (state) {
        state->brightness        = 80;
        state->standbyBrightness = 20;
        state->animSpeedMs       = 40;
    }
    ws.begin(SERVER_HOST, SERVER_PORT, "/bridge");
    ws.onEvent(wsEvent);
    ws.setReconnectInterval(3000);
}

void        wsClientTick()             { ws.loop(); }
bool        wsClientIsConnected()      { return connected; }
void        wsClientSendText(const char* msg) { ws.sendTXT(msg); }
const char* wsClientGetMac()           { return cachedMac; }
```

- [ ] **Step 3: Commit**

```bash
git add firmware/include/ws_client.h firmware/src/ws_client.cpp
git commit -m "feat(firmware): rewrite ws_client — hello+MAC, role/identify callbacks"
```

---

## Task 12 — Create `espnow.h` + `espnow.cpp`

**Files:**
- Create: `firmware/include/espnow.h`
- Create: `firmware/src/espnow.cpp`

Merges the old `espnow_bridge.cpp` and `espnow_camera.cpp`. Receives `TallyPacket`, `HeartbeatPacket`, and `IdentifyPacket` — distinguished by packet length. Uses a ring buffer for heartbeat relay (ISR-safe). Exposes polling functions for main loop to drain events.

- [ ] **Step 1: Create `firmware/include/espnow.h`**

```cpp
#pragma once
#include <stdint.h>
#include "tally_packet.h"

// Heartbeat entry from a receiver — produced by ESP-NOW recv callback
typedef struct {
    uint8_t unitId;
    char    mac[18];  // "AA:BB:CC:DD:EE:FF\0"
} HeartbeatEntry;

// Tally data received from bridge — for receiver mode
typedef struct {
    bool    atemConnected;
    uint8_t states[20];
} TallyData;

// Call after WiFi is initialised (uses current WiFi channel)
void espnowInit();

// Bridge: broadcast tally to all receivers
void espnowBroadcast(bool atemConnected, const uint8_t states[20]);

// Bridge: send unicast IdentifyPacket to a receiver by MAC string
void espnowSendIdentify(const char* targetMacStr);

// Receiver: send HeartbeatPacket to bridge
void espnowSendHeartbeat(uint8_t unitId);

// Receiver: true once bridge MAC is learned from first TallyPacket
bool espnowIsBridgeMacKnown();

// Receiver: returns true (and clears flag) if an IdentifyPacket was received
bool espnowHasIdentify();

// Bridge: drain one heartbeat from ring buffer — returns false when empty
bool espnowNextHeartbeat(HeartbeatEntry* out);

// Receiver: drain one tally update — returns false when none pending
bool espnowNextTally(TallyData* out);

// Receiver: millis() timestamp of last TallyPacket (0 if never received)
unsigned long espnowLastTallyMs();
```

- [ ] **Step 2: Create `firmware/src/espnow.cpp`**

```cpp
#include "espnow.h"
#include "tally_packet.h"
#include <esp_now.h>
#include <WiFi.h>
#include <string.h>
#include <stdio.h>

static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// Bridge: unitId→MAC for unicast (populated from received heartbeats)
static uint8_t unitMacs[20][6]  = {};
static bool    unitMacKnown[20] = {};

// Receiver: bridge MAC (learned from first TallyPacket)
static uint8_t bridgeMac[6]  = {};
static bool    bridgeMacKnown = false;

// Heartbeat ring buffer (ISR-safe: volatile write index, non-volatile data)
static HeartbeatEntry hbBuffer[20]   = {};
static volatile uint8_t hbWrite      = 0;
static uint8_t          hbRead       = 0;

// Tally data (receiver mode)
static TallyData       pendingTally  = {};
static volatile bool   hasTally      = false;
static unsigned long   lastTallyMs   = 0;

// Identify flag (receiver mode)
static volatile bool identifyFlag = false;

static void addPeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

static void parseMacStr(const char* str, uint8_t* out) {
    sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &out[0], &out[1], &out[2], &out[3], &out[4], &out[5]);
}

// Legacy ESP-IDF v4 callback signature (Arduino ESP32 framework)
static void onDataRecv(const uint8_t* mac_addr, const uint8_t* data, int len) {
    if (len == (int)sizeof(TallyPacket)) {
        // Receiver: learn bridge MAC, store tally
        if (!bridgeMacKnown) {
            memcpy(bridgeMac, mac_addr, 6);
            bridgeMacKnown = true;
            addPeer(bridgeMac);
        }
        const TallyPacket* pkt = (const TallyPacket*)data;
        pendingTally.atemConnected = (pkt->flags & 0x01) != 0;
        for (uint8_t i = 1; i <= 20; i++) {
            pendingTally.states[i-1] = tallyPacketGetState(pkt, i);
        }
        hasTally   = true;
        lastTallyMs = millis();

    } else if (len == (int)sizeof(HeartbeatPacket)) {
        // Bridge: relay heartbeat via ring buffer
        const HeartbeatPacket* hb = (const HeartbeatPacket*)data;
        if (hb->type != 0x01) return;
        uint8_t uid = hb->unitId;
        if (uid < 1 || uid > 20) return;

        uint8_t idx = hbWrite % 20;
        hbBuffer[idx].unitId = uid;
        snprintf(hbBuffer[idx].mac, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac_addr[0], mac_addr[1], mac_addr[2],
            mac_addr[3], mac_addr[4], mac_addr[5]);
        hbWrite++;

        // Track MAC for unicast identify
        if (!unitMacKnown[uid-1] || memcmp(unitMacs[uid-1], mac_addr, 6) != 0) {
            memcpy(unitMacs[uid-1], mac_addr, 6);
            unitMacKnown[uid-1] = true;
            addPeer(mac_addr);
        }

    } else if (len == (int)sizeof(IdentifyPacket)) {
        const IdentifyPacket* ip = (const IdentifyPacket*)data;
        if (ip->type == 0x03) identifyFlag = true;
    }
}

void espnowInit() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    esp_now_register_recv_cb(onDataRecv);
    addPeer(BROADCAST_MAC);
}

void espnowBroadcast(bool atemConnected, const uint8_t states[20]) {
    TallyPacket pkt = {};
    pkt.flags = atemConnected ? 0x01 : 0x00;
    for (uint8_t i = 1; i <= 20; i++) tallyPacketSetState(&pkt, i, states[i-1]);
    esp_now_send(BROADCAST_MAC, (uint8_t*)&pkt, sizeof(pkt));
}

void espnowSendIdentify(const char* targetMacStr) {
    uint8_t mac[6];
    parseMacStr(targetMacStr, mac);
    addPeer(mac);
    IdentifyPacket pkt = { 0x03 };
    esp_now_send(mac, (uint8_t*)&pkt, sizeof(pkt));
}

void espnowSendHeartbeat(uint8_t unitId) {
    if (!bridgeMacKnown) return;
    HeartbeatPacket hb = { 0x01, unitId };
    esp_now_send(bridgeMac, (uint8_t*)&hb, sizeof(hb));
}

bool espnowIsBridgeMacKnown() { return bridgeMacKnown; }

bool espnowHasIdentify() {
    bool v = identifyFlag;
    identifyFlag = false;
    return v;
}

bool espnowNextHeartbeat(HeartbeatEntry* out) {
    if (hbRead == (uint8_t)hbWrite) return false;
    uint8_t idx = hbRead % 20;
    *out = hbBuffer[idx];
    hbRead++;
    return true;
}

bool espnowNextTally(TallyData* out) {
    if (!hasTally) return false;
    *out = pendingTally;
    hasTally = false;
    return true;
}

unsigned long espnowLastTallyMs() { return lastTallyMs; }
```

- [ ] **Step 3: Commit**

```bash
git add firmware/include/espnow.h firmware/src/espnow.cpp
git commit -m "feat(firmware): unified espnow module — broadcast, heartbeat relay, identify, tally receive"
```

---

## Task 13 — Create unified `firmware/src/main.cpp`

**Files:**
- Create: `firmware/src/main.cpp`

State machine: `WIFI_CONNECTING → SERVER_CONNECTING → {PROVISIONING | BRIDGE | RECEIVER}`. Bridge relays heartbeats and broadcasts tally. Receiver uses ESP-NOW only after WiFi is stopped.

- [ ] **Step 1: Create `firmware/src/main.cpp`**

```cpp
#include <Arduino.h>
#include "config.h"
#include "led_driver.h"
#include "wifi_manager.h"
#include "ws_client.h"
#include "espnow.h"
#include "provisioning.h"
#include "tally_packet.h"

typedef enum {
    STATE_WIFI_CONNECTING,
    STATE_SERVER_CONNECTING,
    STATE_PROVISIONING,
    STATE_BRIDGE,
    STATE_RECEIVER,
} DeviceState;

static DeviceState deviceState = STATE_WIFI_CONNECTING;
static BridgeState bridgeState = {};  // populated by ws_client (bridge) or tally callback (receiver)

// ── Callbacks ────────────────────────────────────────────────────────────────

static void onRole(uint8_t unitId, DeviceRole role) {
    provisioningSet(unitId, role);

    if (role == ROLE_BRIDGE) {
        espnowInit();
        deviceState = STATE_BRIDGE;
        Serial.printf("Bridge unit %d ready\n", unitId);

    } else if (role == ROLE_RECEIVER) {
        wifiManagerStop();  // disconnect WiFi, keep radio on for ESP-NOW channel
        espnowInit();
        deviceState = STATE_RECEIVER;
        Serial.printf("Receiver unit %d ready\n", unitId);

    } else {
        // ROLE_UNPROVISIONED
        deviceState = STATE_PROVISIONING;
        Serial.println("Unprovisioned — awaiting assignment in /assign");
    }
}

static void onIdentify(const char* targetMac) {
    if (targetMac == nullptr || targetMac[0] == '\0') {
        // Identify this device (bridge itself)
        ledDriverSetState(LED_STATE_IDENTIFY, nullptr);
    } else {
        // Relay identify to a receiver via ESP-NOW unicast
        espnowSendIdentify(targetMac);
    }
}

// ── Timing ───────────────────────────────────────────────────────────────────

static unsigned long lastBroadcastMs  = 0;
static unsigned long lastHeartbeatMs  = 0;
static const unsigned long BROADCAST_INTERVAL_MS = 2000;
static const unsigned long HEARTBEAT_INTERVAL_MS = 5000;

// ── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    ledDriverInit(LED_PIN, LED_COUNT);
    ledDriverSetState(LED_STATE_AMBER_BREATH, nullptr);

    wifiManagerInit();

    Serial.print("Connecting to WiFi");
    while (!wifiManagerIsConnected()) {
        wifiManagerTick();
        ledDriverTick();
        Serial.print(".");
        delay(100);
    }
    Serial.println(" connected");

    wsClientInit(&bridgeState, onRole, onIdentify);
    deviceState = STATE_SERVER_CONNECTING;
}

// ── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    unsigned long now = millis();

    switch (deviceState) {

        case STATE_WIFI_CONNECTING:
            // Handled in setup() — should never reach here
            break;

        case STATE_SERVER_CONNECTING:
            wifiManagerTick();
            wsClientTick();
            ledDriverSetState(LED_STATE_AMBER_BREATH, nullptr);
            ledDriverTick();
            break;

        case STATE_PROVISIONING:
            wifiManagerTick();
            wsClientTick();
            ledDriverSetState(LED_STATE_PAIRING, nullptr);
            ledDriverTick();
            break;

        case STATE_BRIDGE: {
            wifiManagerTick();
            wsClientTick();

            // Drain heartbeat ring buffer — relay each to server
            HeartbeatEntry hb;
            while (espnowNextHeartbeat(&hb)) {
                if (wsClientIsConnected()) {
                    char msg[80];
                    snprintf(msg, sizeof(msg),
                        "{\"type\":\"heartbeat_relay\",\"unitId\":%u,\"mac\":\"%s\"}",
                        hb.unitId, hb.mac);
                    wsClientSendText(msg);
                }
            }

            // Bridge own heartbeat (keeps server lastSeen fresh)
            if (wsClientIsConnected() && now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
                lastHeartbeatMs = now;
                char msg[80];
                snprintf(msg, sizeof(msg),
                    "{\"type\":\"heartbeat\",\"unitId\":%u,\"mac\":\"%s\"}",
                    provisioningGetUnitId(), wsClientGetMac());
                wsClientSendText(msg);
            }

            // Broadcast tally on update or keepalive interval
            if (bridgeState.updated || now - lastBroadcastMs >= BROADCAST_INTERVAL_MS) {
                espnowBroadcast(bridgeState.atemConnected, bridgeState.states);
                bridgeState.updated = false;
                lastBroadcastMs = now;
            }

            // Bridge LED: reflects own tally assignment
            LedState ownState;
            if (!wsClientIsConnected()) {
                ownState = LED_STATE_AMBER_BREATH;
            } else if (!bridgeState.atemConnected) {
                ownState = LED_STATE_WHITE_BREATH;
            } else {
                uint8_t uid = provisioningGetUnitId();
                uint8_t s = (uid >= 1 && uid <= 20) ? bridgeState.states[uid-1] : TALLY_STANDBY;
                switch (s) {
                    case TALLY_PROGRAM: ownState = LED_STATE_PROGRAM; break;
                    case TALLY_PREVIEW: ownState = LED_STATE_PREVIEW; break;
                    default:            ownState = LED_STATE_STANDBY; break;
                }
            }
            LedSettings settings = {
                bridgeState.brightness,
                bridgeState.standbyBrightness,
                bridgeState.animSpeedMs,
            };
            ledDriverSetState(ownState, &settings);
            ledDriverTick();
            break;
        }

        case STATE_RECEIVER: {
            // No WiFi — ESP-NOW only

            // Check for identify from bridge
            if (espnowHasIdentify()) {
                ledDriverSetState(LED_STATE_IDENTIFY, nullptr);
            }

            // Send heartbeat every 5s once bridge MAC is known
            if (espnowIsBridgeMacKnown() && now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
                lastHeartbeatMs = now;
                espnowSendHeartbeat(provisioningGetUnitId());
            }

            // Drain incoming tally data
            TallyData td;
            if (espnowNextTally(&td)) {
                bridgeState.atemConnected = td.atemConnected;
                memcpy(bridgeState.states, td.states, 20);
            }

            // Receiver LED state
            LedState ownState;
            unsigned long lastPkt = espnowLastTallyMs();
            if (lastPkt == 0 || now - lastPkt > 10000) {
                ownState = LED_STATE_AMBER_BREATH;
            } else if (!bridgeState.atemConnected) {
                ownState = LED_STATE_WHITE_BREATH;
            } else {
                uint8_t uid = provisioningGetUnitId();
                uint8_t s = (uid >= 1 && uid <= 20) ? bridgeState.states[uid-1] : TALLY_STANDBY;
                switch (s) {
                    case TALLY_PROGRAM: ownState = LED_STATE_PROGRAM; break;
                    case TALLY_PREVIEW: ownState = LED_STATE_PREVIEW; break;
                    default:            ownState = LED_STATE_STANDBY; break;
                }
            }
            // Receivers use default brightness (no settings push from server)
            LedSettings settings = { 80, 20, 40 };
            ledDriverSetState(ownState, &settings);
            ledDriverTick();
            break;
        }
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add firmware/src/main.cpp
git commit -m "feat(firmware): unified main.cpp — WiFi→server→role state machine"
```

---

## Task 14 — Move shared source files, delete old directories and headers

**Files:**
- Create: `firmware/src/tally_packet.cpp` (copy of `firmware/src/shared/tally_packet.cpp`)
- Delete: `firmware/src/shared/` (entire directory)
- Delete: `firmware/src/camera/` (entire directory)
- Delete: `firmware/src/bridge/` (entire directory)
- Delete: `firmware/include/espnow_camera.h`
- Delete: `firmware/include/espnow_bridge.h`

- [ ] **Step 1: Copy `tally_packet.cpp` to flat `src/`**

PowerShell:
```powershell
Copy-Item firmware\src\shared\tally_packet.cpp firmware\src\tally_packet.cpp
```

- [ ] **Step 2: Delete old source directories and headers (run from repo root)**

```bash
git rm -r firmware/src/shared firmware/src/camera firmware/src/bridge
git rm firmware/include/espnow_camera.h firmware/include/espnow_bridge.h
```

- [ ] **Step 3: Stage new file and commit**

```bash
git add firmware/src/tally_packet.cpp
git commit -m "refactor(firmware): remove camera/bridge/shared dirs — all source now flat in src/"
```

---

## Task 15 — Verify build compiles

- [ ] **Step 1: Build tally firmware**

```bash
cd firmware && pio run -e tally
```

Expected: `SUCCESS` with no errors. If linker reports duplicate symbols, check for any remaining files in the old subdirectories.

- [ ] **Step 2: Run native tests**

```bash
cd firmware && pio test -e native_test
```

Expected: 10 tests PASS

- [ ] **Step 3: Run all server tests**

```bash
cd server && npm test
```

Expected: all PASS

- [ ] **Step 4: Commit final verification note and push**

```bash
cd .. && git add -A && git commit -m "build: verify unified firmware and server build clean" && git push
```

---

## Notes for the executor

- **Ordering matters for firmware:** Tasks 5–14 must run in order. The build (Task 15) will fail until old directories are removed in Task 14.
- **Server tasks (1–4) are independent** of firmware tasks — they can be started first while waiting on firmware compilation.
- **First flash after this change:** all devices boot, connect to WiFi, connect to server, appear as unprovisioned (blue pulse). Go to `/assign`, assign each device a Unit ID and role (one bridge, rest receivers), click Save. Devices receive their roles in real time if they're still connected; otherwise on next boot.
- **Config on the Docker host:** if an existing `config.json` has the old unitId-keyed format, it is automatically cleared on server restart (Task 2 migration). Re-assign all units after first start.
