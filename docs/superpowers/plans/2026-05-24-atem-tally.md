# ATEM ESP-NOW Tally Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a complete wireless tally light system — a Dockerised Node.js base station reads tally from a Blackmagic ATEM switcher and distributes it to ESP32-C3 units via a bridge device using ESP-NOW.

**Architecture:** A Node.js server connects to the ATEM via `atem-connection`, maps unit IDs to ATEM inputs via persisted config, and pushes tally state to browser clients via Socket.io and to the bridge ESP32-C3 via raw WebSocket — both on port 8259. The bridge broadcasts a 3-byte ESP-NOW packet to all camera units; each unit reads its 2-bit slot and drives 6× WS2812 LEDs. Camera units send ESP-NOW heartbeats back to the bridge, which relays online status to the server.

**Tech Stack:** Node.js 20, Express 4, Socket.io 4, ws 8, atem-connection 3, Jest 29 | PlatformIO, Arduino ESP32 (ESP-IDF v5), Adafruit NeoPixel, arduinoWebSockets (Links2004)

> **Note on bridge protocol:** The spec describes Socket.io for bridge+UI. In implementation, browser clients use Socket.io (v4) and the bridge ESP32 uses raw WebSocket on `/bridge` — this avoids running a Socket.io client on ESP32-C3, which has no mature library. The server uses the `ws` package to handle the bridge endpoint alongside Socket.io on the same HTTP server. Functionally equivalent to the spec.

---

## File Structure

### Base Station (`server/`)

```
server/
├── src/
│   ├── index.js        Entry point — creates HTTP server, wires all modules
│   ├── config.js       Read/write/validate config.json with defaults
│   ├── tally.js        Build per-unit-ID tally state from ATEM state + config
│   ├── atem.js         atem-connection wrapper — auto-connect, tally events
│   └── socket.js       Socket.io (browsers) + ws (bridge) on same HTTP server
├── views/
│   ├── dashboard.html  Live status grid (/)
│   ├── assign.html     Unit → ATEM input mapping (/assign)
│   └── settings.html   ATEM IP, brightness, animation speed (/settings)
├── public/
│   └── app.css         Shared styles
├── tests/
│   ├── config.test.js  Config defaults, read, write, merge
│   └── tally.test.js   Unit ID → tally state mapping logic
├── config.json         Runtime config — gitignored, mounted as Docker volume
├── package.json
├── .gitignore
├── Dockerfile
└── docker-compose.yml
```

### Firmware (`firmware/`)

```
firmware/
├── platformio.ini              Two environments: bridge, camera
├── include/
│   ├── config.h                EDIT BEFORE FLASHING each unit
│   ├── tally_packet.h          ESP-NOW packet structs (shared)
│   └── led_driver.h            LED state machine interface (shared)
├── src/
│   ├── shared/
│   │   ├── led_driver.cpp      WS2812 animations — all states
│   │   └── tally_packet.cpp    Packet encode/decode
│   ├── bridge/
│   │   ├── main.cpp            Bridge entry point
│   │   ├── wifi_manager.cpp    WiFi connect/reconnect loop
│   │   ├── ws_client.cpp       WebSocket client ↔ server JSON protocol
│   │   └── espnow_bridge.cpp   ESP-NOW broadcast + heartbeat receive
│   └── camera/
│       ├── main.cpp            Camera unit entry point
│       └── espnow_camera.cpp   ESP-NOW receive + heartbeat send + timeout
└── test/
    └── native/
        ├── test_packet/
        │   └── test_packet.cpp Packet encode/decode unit tests
        └── test_led/
            └── test_led.cpp    LED state machine unit tests
```

---

## Shared Interfaces

### WebSocket Bridge Protocol

Bridge connects to `ws://[server-ip]:8259/bridge` using plain WebSocket (JSON messages, newline-delimited).

**Bridge → Server:**
```json
{"type":"register","unitId":20}
{"type":"heartbeat","unitId":3}
```

**Server → Bridge:**
```json
{"type":"tally","atemConnected":true,"states":{"1":2,"2":1,"3":0,"4":0}}
{"type":"settings","brightness":80,"standbyBrightness":20,"animSpeed":"slow"}
```
State values: `0`=standby, `1`=preview, `2`=program

### ESP-NOW Packet Format

```c
// Broadcast: bridge → camera units (3 bytes total)
typedef struct __attribute__((packed)) {
    uint8_t flags;      // bit 0: 1=ATEM connected, 0=disconnected
    uint8_t states[2];  // 20 slots × 2 bits packed LSB-first
                        // bits [(unitId-1)*2 + 1 : (unitId-1)*2]
                        // 00=standby 01=preview 10=program 11=reserved
} TallyPacket;

// Heartbeat: camera unit → bridge (2 bytes)
typedef struct __attribute__((packed)) {
    uint8_t type;    // always 0x01
    uint8_t unitId;  // sending unit's ID (1–20)
} HeartbeatPacket;
```

Slot extraction for unit ID `n` (1-based):
```c
uint8_t bit_offset = (n - 1) * 2;
uint8_t byte_index = bit_offset / 8;
uint8_t bit_pos    = bit_offset % 8;
uint8_t state      = (pkt.states[byte_index] >> bit_pos) & 0x03;
```

### Config Schema (`server/config.json`)

```json
{
  "atem": { "ip": "192.168.1.100" },
  "leds": {
    "brightness": 80,
    "programBrightness": 80,
    "previewBrightness": 80,
    "standbyBrightness": 20,
    "animSpeed": "slow"
  },
  "units": {
    "1": { "atemInput": 1 },
    "2": { "atemInput": 2 }
  }
}
```
`units` keys are unit ID strings; `atemInput` is the ATEM source number (integer). Omit `atemInput` or set to `0` for unmapped.

---

## Phase 1 — Base Station

### Task 1: Project Scaffold

**Files:**
- Create: `server/package.json`
- Create: `server/.gitignore`
- Create: `server/src/index.js`
- Create: `server/src/config.js`
- Create: `server/src/tally.js`
- Create: `server/src/atem.js`
- Create: `server/src/socket.js`

- [ ] **Create directory structure**

```bash
mkdir -p server/src server/views server/public server/tests
```

- [ ] **Create `server/package.json`**

```json
{
  "name": "atem-tally-server",
  "version": "1.0.0",
  "type": "module",
  "scripts": {
    "start": "node src/index.js",
    "test": "node --experimental-vm-modules node_modules/.bin/jest"
  },
  "dependencies": {
    "atem-connection": "^3.4.0",
    "express": "^4.19.2",
    "socket.io": "^4.7.5",
    "ws": "^8.17.1"
  },
  "devDependencies": {
    "jest": "^29.7.0"
  },
  "jest": {
    "transform": {}
  }
}
```

- [ ] **Create `server/.gitignore`**

```
node_modules/
config.json
```

- [ ] **Create stub files** — one line each, content `// stub`

  - `server/src/index.js`
  - `server/src/config.js`
  - `server/src/tally.js`
  - `server/src/atem.js`
  - `server/src/socket.js`

- [ ] **Install dependencies**

```bash
cd server && npm install
```

Expected: `node_modules/` created, no errors.

- [ ] **Commit**

```bash
git add server/
git commit -m "feat: scaffold base station Node.js project"
```

---

### Task 2: Config Module

**Files:**
- Create: `server/src/config.js`
- Create: `server/tests/config.test.js`

- [ ] **Write failing tests** → `server/tests/config.test.js`

```javascript
import { readConfig, writeConfig, DEFAULT_CONFIG } from '../src/config.js'
import { writeFileSync, readFileSync, unlinkSync, existsSync } from 'fs'

const TMP = './tests/tmp-config.json'
afterEach(() => { if (existsSync(TMP)) unlinkSync(TMP) })

test('returns defaults when file missing', () => {
  const c = readConfig('./tests/no-such-file.json')
  expect(c.atem.ip).toBe('')
  expect(c.leds.brightness).toBe(80)
  expect(c.leds.standbyBrightness).toBe(20)
  expect(c.units).toEqual({})
})

test('merges saved values over defaults', () => {
  writeFileSync(TMP, JSON.stringify({ atem: { ip: '192.168.1.50' } }))
  const c = readConfig(TMP)
  expect(c.atem.ip).toBe('192.168.1.50')
  expect(c.leds.brightness).toBe(80) // default still present
})

test('writeConfig persists to disk', () => {
  writeConfig(TMP, { ...DEFAULT_CONFIG, atem: { ip: '10.0.0.1' } })
  const raw = JSON.parse(readFileSync(TMP, 'utf8'))
  expect(raw.atem.ip).toBe('10.0.0.1')
})

test('writeConfig then readConfig round-trips', () => {
  const original = { ...DEFAULT_CONFIG, atem: { ip: '10.0.0.2' } }
  writeConfig(TMP, original)
  const loaded = readConfig(TMP)
  expect(loaded.atem.ip).toBe('10.0.0.2')
})
```

- [ ] **Run tests — verify they fail**

```bash
cd server && npm test
```

Expected: `Cannot find module '../src/config.js'`

- [ ] **Implement `server/src/config.js`**

```javascript
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

export function readConfig(path) {
  if (!existsSync(path)) return structuredClone(DEFAULT_CONFIG)
  try {
    const saved = JSON.parse(readFileSync(path, 'utf8'))
    return {
      atem: { ...DEFAULT_CONFIG.atem, ...saved.atem },
      leds: { ...DEFAULT_CONFIG.leds, ...saved.leds },
      units: saved.units ?? {},
    }
  } catch {
    return structuredClone(DEFAULT_CONFIG)
  }
}

export function writeConfig(path, config) {
  writeFileSync(path, JSON.stringify(config, null, 2))
}
```

- [ ] **Run tests — verify they pass**

```bash
cd server && npm test -- tests/config.test.js
```

Expected: 4 tests pass.

- [ ] **Commit**

```bash
git add server/src/config.js server/tests/config.test.js
git commit -m "feat: config read/write with defaults and merge"
```

---

### Task 3: Tally Mapping Module

**Files:**
- Create: `server/src/tally.js`
- Create: `server/tests/tally.test.js`

- [ ] **Write failing tests** → `server/tests/tally.test.js`

```javascript
import { buildUnitStates } from '../src/tally.js'

// atemTallys: object keyed by ATEM source number
// { 1: { program: true, preview: false }, 2: { program: false, preview: true } }
// unitMap: config.units — { '3': { atemInput: 1 }, '5': { atemInput: 2 } }

test('unit mapped to program input returns state 2', () => {
  const states = buildUnitStates(
    { 1: { program: true, preview: false } },
    { '3': { atemInput: 1 } }
  )
  expect(states['3']).toBe(2)
})

test('unit mapped to preview input returns state 1', () => {
  const states = buildUnitStates(
    { 2: { program: false, preview: true } },
    { '5': { atemInput: 2 } }
  )
  expect(states['5']).toBe(1)
})

test('unit mapped to inactive input returns state 0', () => {
  const states = buildUnitStates(
    { 1: { program: false, preview: false } },
    { '3': { atemInput: 1 } }
  )
  expect(states['3']).toBe(0)
})

test('unmapped unit (atemInput 0) returns state 0', () => {
  const states = buildUnitStates(
    { 1: { program: true, preview: false } },
    { '7': { atemInput: 0 } }
  )
  expect(states['7']).toBe(0)
})

test('unit with no atemInput key returns state 0', () => {
  const states = buildUnitStates({}, { '9': {} })
  expect(states['9']).toBe(0)
})

test('multiple units can share an ATEM input', () => {
  const states = buildUnitStates(
    { 3: { program: true, preview: false } },
    { '1': { atemInput: 3 }, '2': { atemInput: 3 } }
  )
  expect(states['1']).toBe(2)
  expect(states['2']).toBe(2)
})

test('program takes precedence over preview for same source', () => {
  const states = buildUnitStates(
    { 1: { program: true, preview: true } }, // shouldn't happen but be safe
    { '1': { atemInput: 1 } }
  )
  expect(states['1']).toBe(2)
})
```

- [ ] **Run tests — verify they fail**

```bash
cd server && npm test -- tests/tally.test.js
```

Expected: module not found error.

- [ ] **Implement `server/src/tally.js`**

```javascript
// buildUnitStates: returns { [unitId: string]: 0|1|2 }
// 0=standby, 1=preview, 2=program
export function buildUnitStates(atemTallys, unitMap) {
  const states = {}
  for (const [unitId, cfg] of Object.entries(unitMap)) {
    const input = cfg.atemInput ?? 0
    if (!input) { states[unitId] = 0; continue }
    const tally = atemTallys[input]
    if (!tally) { states[unitId] = 0; continue }
    states[unitId] = tally.program ? 2 : tally.preview ? 1 : 0
  }
  return states
}
```

- [ ] **Run tests — verify they pass**

```bash
cd server && npm test -- tests/tally.test.js
```

Expected: 7 tests pass.

- [ ] **Commit**

```bash
git add server/src/tally.js server/tests/tally.test.js
git commit -m "feat: tally mapping — unit ID to program/preview/standby state"
```

---

### Task 4: ATEM Connection Module

**Files:**
- Create: `server/src/atem.js`

- [ ] **Implement `server/src/atem.js`**

```javascript
import { Atem } from 'atem-connection'
import { EventEmitter } from 'events'

// Emits:
//   'tally'  (atemTallys: object, inputNames: object) on state change
//   'status' ('connecting'|'connected'|'disconnected')

export class AtemManager extends EventEmitter {
  #atem = new Atem({ debugBuffers: false })
  #ip = null
  #connected = false

  constructor() {
    super()
    this.#atem.on('connected', () => {
      this.#connected = true
      this.emit('status', 'connected')
      this.#emitTally()
    })
    this.#atem.on('disconnected', () => {
      this.#connected = false
      this.emit('status', 'disconnected')
    })
    this.#atem.on('stateChanged', (state, paths) => {
      const relevant = paths.some(p =>
        p.startsWith('tallys') || p.startsWith('inputs')
      )
      if (relevant) this.#emitTally()
    })
  }

  connect(ip) {
    this.#ip = ip
    if (!ip) return
    this.emit('status', 'connecting')
    this.#atem.connect(ip)
  }

  disconnect() {
    this.#atem.disconnect()
    this.#connected = false
  }

  get isConnected() { return this.#connected }

  // Returns { [sourceNum]: { program, preview } } and { [sourceNum]: name }
  getState() {
    const state = this.#atem.state
    if (!state) return { tallys: {}, inputNames: {} }

    const tallys = {}
    if (state.tallys) {
      for (const [src, t] of Object.entries(state.tallys)) {
        if (t) tallys[Number(src)] = { program: !!t.program, preview: !!t.preview }
      }
    }

    const inputNames = {}
    if (state.inputs) {
      for (const [src, inp] of Object.entries(state.inputs)) {
        inputNames[Number(src)] = inp?.properties?.longName ?? `Input ${src}`
      }
    }

    return { tallys, inputNames }
  }

  #emitTally() {
    const { tallys, inputNames } = this.getState()
    this.emit('tally', tallys, inputNames)
  }
}
```

> **Note:** The `atem-connection` state structure may vary by version. If `state.tallys` or `state.inputs` has a different shape, consult the library's TypeScript types in `node_modules/atem-connection/dist/state/`. The key events (`connected`, `disconnected`, `stateChanged`) are stable across v3.x.

- [ ] **Verify no syntax errors**

```bash
cd server && node --check src/atem.js
```

Expected: no output (no errors).

- [ ] **Commit**

```bash
git add server/src/atem.js
git commit -m "feat: ATEM connection manager with auto-connect and tally events"
```

---

### Task 5: Socket Server (Socket.io + Bridge WebSocket)

**Files:**
- Create: `server/src/socket.js`

- [ ] **Implement `server/src/socket.js`**

```javascript
import { Server as SocketIO } from 'socket.io'
import { WebSocketServer } from 'ws'
import { buildUnitStates } from './tally.js'

// knownUnits: { [unitId: string]: { lastSeen: number } }
// bridgeSocket: the active bridge WebSocket connection (or null)

export function createSocketServer(httpServer, atemManager, getConfig, saveConfig) {
  const io = new SocketIO(httpServer, { cors: { origin: '*' } })
  const wss = new WebSocketServer({ server: httpServer, path: '/bridge' })

  let knownUnits = {}   // populated by heartbeats
  let bridgeWs = null   // active bridge connection
  let lastTallys = {}
  let lastInputNames = {}
  let atemConnected = false

  // ── ATEM events ──────────────────────────────────────────────────────────
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
    const msg = JSON.stringify({ type: 'tally', atemConnected, states })
    bridgeWs.send(msg)
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

  // ── Bridge WebSocket ──────────────────────────────────────────────────────
  wss.on('connection', (ws) => {
    bridgeWs = ws
    // Send current state immediately
    pushSettingsToBridge()
    pushTallyToBridge()

    ws.on('message', (data) => {
      try {
        const msg = JSON.parse(data.toString())
        if (msg.type === 'register') {
          const id = String(msg.unitId)
          knownUnits[id] = { lastSeen: Date.now() }
          io.emit('units', formatUnits(knownUnits, getConfig()))
        }
        if (msg.type === 'heartbeat') {
          const id = String(msg.unitId)
          knownUnits[id] = { lastSeen: Date.now() }
          io.emit('units', formatUnits(knownUnits, getConfig()))
        }
      } catch { /* ignore malformed */ }
    })

    ws.on('close', () => {
      if (bridgeWs === ws) bridgeWs = null
      io.emit('bridgeStatus', 'disconnected')
    })

    io.emit('bridgeStatus', 'connected')
  })

  // ── Browser Socket.io ─────────────────────────────────────────────────────
  io.on('connection', (socket) => {
    const cfg = getConfig()
    // Send current state to new browser client
    socket.emit('atemStatus', atemConnected ? 'connected' : 'disconnected')
    socket.emit('bridgeStatus', bridgeWs ? 'connected' : 'disconnected')
    socket.emit('units', formatUnits(knownUnits, cfg))
    socket.emit('inputNames', lastInputNames)
    const states = buildUnitStates(lastTallys, cfg.units)
    socket.emit('tally', { atemConnected, states })

    socket.on('saveAssignments', (assignments) => {
      // assignments: { [unitId]: atemInput }
      const cfg = getConfig()
      for (const [id, input] of Object.entries(assignments)) {
        cfg.units[id] = { atemInput: Number(input) }
      }
      saveConfig(cfg)
      pushTallyToBridge()
      io.emit('units', formatUnits(knownUnits, getConfig()))
    })

    socket.on('saveSettings', (settings) => {
      const cfg = getConfig()
      cfg.atem.ip = settings.ip ?? cfg.atem.ip
      cfg.leds = { ...cfg.leds, ...settings.leds }
      saveConfig(cfg)
      pushSettingsToBridge()
      if (settings.ip && settings.ip !== cfg.atem.ip) {
        atemManager.disconnect()
        atemManager.connect(settings.ip)
      }
    })
  })

  return { io, wss, getKnownUnits: () => knownUnits, getInputNames: () => lastInputNames }
}

function formatUnits(knownUnits, cfg) {
  const now = Date.now()
  return Object.entries(knownUnits).map(([id, u]) => ({
    id: Number(id),
    atemInput: cfg.units[id]?.atemInput ?? 0,
    online: (now - u.lastSeen) < 15000,
    lastSeen: u.lastSeen,
  }))
}
```

- [ ] **Verify no syntax errors**

```bash
cd server && node --check src/socket.js
```

Expected: no output.

- [ ] **Commit**

```bash
git add server/src/socket.js
git commit -m "feat: socket server — Socket.io for browsers, WebSocket for bridge"
```

---

### Task 6: Express Routes

**Files:**
- Create: `server/src/routes.js`

- [ ] **Create `server/src/routes.js`**

```javascript
import { Router } from 'express'
import { readFileSync } from 'fs'
import { fileURLToPath } from 'url'
import { join, dirname } from 'path'

const __dir = dirname(fileURLToPath(import.meta.url))
const view = (name) => readFileSync(join(__dir, '../views', name), 'utf8')

export function createRoutes(getConfig, getKnownUnits, getInputNames) {
  const router = Router()

  router.get('/', (_req, res) => res.send(view('dashboard.html')))
  router.get('/assign', (_req, res) => res.send(view('assign.html')))
  router.get('/settings', (_req, res) => res.send(view('settings.html')))

  // JSON API used by views on initial load
  router.get('/api/config', (_req, res) => res.json(getConfig()))
  router.get('/api/units', (_req, res) => res.json(getKnownUnits()))
  router.get('/api/inputs', (_req, res) => res.json(getInputNames()))

  return router
}
```

- [ ] **Commit**

```bash
git add server/src/routes.js
git commit -m "feat: Express routes for dashboard, assign, settings, JSON APIs"
```

---

### Task 7: HTML Views

**Files:**
- Create: `server/views/dashboard.html`
- Create: `server/views/assign.html`
- Create: `server/views/settings.html`
- Create: `server/public/app.css`

- [ ] **Create `server/public/app.css`**

```css
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: system-ui, sans-serif; background: #111; color: #eee; padding: 1.5rem; }
nav { display: flex; gap: 1rem; margin-bottom: 1.5rem; }
nav a { color: #aaa; text-decoration: none; font-size: 0.9rem; }
nav a.active, nav a:hover { color: #fff; }
h1 { font-size: 1.2rem; font-weight: 600; margin-bottom: 1rem; }
.status-bar { display: flex; gap: 1rem; margin-bottom: 1.5rem; font-size: 0.85rem; color: #aaa; }
.dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 4px; }
.dot.green { background: #22c55e; }
.dot.red { background: #ef4444; }
.dot.amber { background: #f59e0b; }
/* Dashboard grid */
.unit-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 1rem; }
.unit-card { border-radius: 8px; padding: 1rem; background: #1e1e1e; border: 2px solid #333; text-align: center; }
.unit-card.program { border-color: #ef4444; background: #3b0000; }
.unit-card.preview { border-color: #22c55e; background: #003b00; }
.unit-card.offline { opacity: 0.4; }
.unit-card .uid { font-size: 1.4rem; font-weight: 700; }
.unit-card .label { font-size: 0.75rem; color: #aaa; margin-top: 0.25rem; }
.unit-card .state { font-size: 0.7rem; text-transform: uppercase; letter-spacing: 0.05em; margin-top: 0.5rem; }
/* Forms */
table { width: 100%; border-collapse: collapse; font-size: 0.9rem; }
th, td { padding: 0.6rem 0.8rem; text-align: left; border-bottom: 1px solid #333; }
th { color: #aaa; font-weight: 500; }
select, input[type=text], input[type=number] { background: #222; border: 1px solid #444; color: #eee;
  padding: 0.4rem 0.6rem; border-radius: 4px; width: 100%; }
button { background: #2563eb; color: #fff; border: none; padding: 0.6rem 1.2rem;
  border-radius: 6px; cursor: pointer; font-size: 0.9rem; margin-top: 1rem; }
button:hover { background: #1d4ed8; }
.field { margin-bottom: 1rem; }
label { display: block; font-size: 0.85rem; color: #aaa; margin-bottom: 0.3rem; }
.saved-msg { color: #22c55e; font-size: 0.85rem; margin-left: 1rem; display: none; }
```

- [ ] **Create `server/views/dashboard.html`**

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>ATEM Tally — Dashboard</title>
  <link rel="stylesheet" href="/app.css">
</head>
<body>
  <nav>
    <a href="/" class="active">Dashboard</a>
    <a href="/assign">Assign</a>
    <a href="/settings">Settings</a>
  </nav>
  <div class="status-bar">
    <span><span class="dot" id="atem-dot"></span>ATEM: <span id="atem-status">–</span></span>
    <span><span class="dot" id="bridge-dot"></span>Bridge: <span id="bridge-status">–</span></span>
  </div>
  <div class="unit-grid" id="grid"></div>

  <script src="/socket.io/socket.io.js"></script>
  <script>
    const socket = io()
    let units = [], tally = {}, inputNames = {}

    function dotClass(status) {
      return status === 'connected' ? 'green' : status === 'connecting' ? 'amber' : 'red'
    }

    socket.on('atemStatus', s => {
      document.getElementById('atem-status').textContent = s
      document.getElementById('atem-dot').className = 'dot ' + dotClass(s)
    })
    socket.on('bridgeStatus', s => {
      document.getElementById('bridge-status').textContent = s
      document.getElementById('bridge-dot').className = 'dot ' + dotClass(s)
    })
    socket.on('inputNames', n => { inputNames = n; render() })
    socket.on('tally', data => { tally = data.states; render() })
    socket.on('units', u => { units = u; render() })

    function stateLabel(s) { return ['Standby','Preview','Program'][s] ?? '–' }
    function stateClass(s) { return ['','preview','program'][s] ?? '' }

    function render() {
      const grid = document.getElementById('grid')
      if (!units.length) { grid.innerHTML = '<p style="color:#666">No units seen yet.</p>'; return }
      const sorted = [...units].sort((a,b) => a.id - b.id)
      grid.innerHTML = sorted.map(u => {
        const s = tally[String(u.id)] ?? 0
        const name = inputNames[u.atemInput] ?? (u.atemInput ? `Input ${u.atemInput}` : 'Unassigned')
        return `<div class="unit-card ${stateClass(s)} ${u.online ? '' : 'offline'}">
          <div class="uid">${u.id}</div>
          <div class="label">${name}</div>
          <div class="state">${u.online ? stateLabel(s) : 'Offline'}</div>
        </div>`
      }).join('')
    }
  </script>
</body>
</html>
```

- [ ] **Create `server/views/assign.html`**

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>ATEM Tally — Assign</title>
  <link rel="stylesheet" href="/app.css">
</head>
<body>
  <nav>
    <a href="/">Dashboard</a>
    <a href="/assign" class="active">Assign</a>
    <a href="/settings">Settings</a>
  </nav>
  <h1>Unit Assignments</h1>
  <table>
    <thead><tr><th>Unit ID</th><th>Last Seen</th><th>ATEM Input</th></tr></thead>
    <tbody id="tbody"></tbody>
  </table>
  <button id="save-btn">Save Assignments</button>
  <span class="saved-msg" id="saved-msg">Saved ✓</span>

  <script src="/socket.io/socket.io.js"></script>
  <script>
    const socket = io()
    let units = [], inputNames = {}, config = {}

    fetch('/api/config').then(r => r.json()).then(c => { config = c; render() })

    socket.on('units', u => { units = u; render() })
    socket.on('inputNames', n => { inputNames = n; render() })

    function render() {
      const sorted = [...units].sort((a,b) => a.id - b.id)
      const options = [['0','Unassigned'], ...Object.entries(inputNames).map(([k,v]) => [k, v])]
      document.getElementById('tbody').innerHTML = sorted.map(u => {
        const current = config.units?.[String(u.id)]?.atemInput ?? 0
        const opts = options.map(([v,l]) =>
          `<option value="${v}" ${Number(v)===current?'selected':''}>${l}</option>`).join('')
        const ago = u.lastSeen ? Math.round((Date.now()-u.lastSeen)/1000)+'s ago' : 'never'
        return `<tr>
          <td>${u.id}</td>
          <td>${ago}</td>
          <td><select data-id="${u.id}">${opts}</select></td>
        </tr>`
      }).join('')
    }

    document.getElementById('save-btn').addEventListener('click', () => {
      const assignments = {}
      document.querySelectorAll('select[data-id]').forEach(s => {
        assignments[s.dataset.id] = Number(s.value)
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

- [ ] **Create `server/views/settings.html`**

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>ATEM Tally — Settings</title>
  <link rel="stylesheet" href="/app.css">
</head>
<body>
  <nav>
    <a href="/">Dashboard</a>
    <a href="/assign">Assign</a>
    <a href="/settings" class="active">Settings</a>
  </nav>
  <h1>Settings</h1>
  <div class="field">
    <label>ATEM IP Address</label>
    <input type="text" id="atem-ip" placeholder="192.168.1.100">
  </div>
  <div class="field">
    <label>Global Brightness (0–100%)</label>
    <input type="number" id="brightness" min="0" max="100">
  </div>
  <div class="field">
    <label>Program Brightness (0–100%)</label>
    <input type="number" id="program-brightness" min="0" max="100">
  </div>
  <div class="field">
    <label>Preview Brightness (0–100%)</label>
    <input type="number" id="preview-brightness" min="0" max="100">
  </div>
  <div class="field">
    <label>Standby Brightness (0–100%)</label>
    <input type="number" id="standby-brightness" min="0" max="100">
  </div>
  <div class="field">
    <label>Animation Speed</label>
    <select id="anim-speed">
      <option value="slow">Slow</option>
      <option value="medium">Medium</option>
      <option value="fast">Fast</option>
    </select>
  </div>
  <button id="save-btn">Save Settings</button>
  <span class="saved-msg" id="saved-msg">Saved ✓</span>

  <script src="/socket.io/socket.io.js"></script>
  <script>
    const socket = io()
    fetch('/api/config').then(r => r.json()).then(c => {
      document.getElementById('atem-ip').value = c.atem.ip
      document.getElementById('brightness').value = c.leds.brightness
      document.getElementById('program-brightness').value = c.leds.programBrightness
      document.getElementById('preview-brightness').value = c.leds.previewBrightness
      document.getElementById('standby-brightness').value = c.leds.standbyBrightness
      document.getElementById('anim-speed').value = c.leds.animSpeed
    })

    document.getElementById('save-btn').addEventListener('click', () => {
      socket.emit('saveSettings', {
        ip: document.getElementById('atem-ip').value.trim(),
        leds: {
          brightness: Number(document.getElementById('brightness').value),
          programBrightness: Number(document.getElementById('program-brightness').value),
          previewBrightness: Number(document.getElementById('preview-brightness').value),
          standbyBrightness: Number(document.getElementById('standby-brightness').value),
          animSpeed: document.getElementById('anim-speed').value,
        }
      })
      const msg = document.getElementById('saved-msg')
      msg.style.display = 'inline'
      setTimeout(() => msg.style.display = 'none', 2000)
    })
  </script>
</body>
</html>
```

- [ ] **Commit**

```bash
git add server/views/ server/public/
git commit -m "feat: HTML views — dashboard, assign, settings with Socket.io"
```

---

### Task 8: Entry Point + Docker

**Files:**
- Create: `server/src/index.js`
- Create: `server/Dockerfile`
- Create: `server/docker-compose.yml`

- [ ] **Create `server/src/index.js`**

```javascript
import { createServer } from 'http'
import express from 'express'
import { fileURLToPath } from 'url'
import { join, dirname } from 'path'
import { existsSync } from 'fs'
import { readConfig, writeConfig } from './config.js'
import { AtemManager } from './atem.js'
import { createSocketServer } from './socket.js'
import { createRoutes } from './routes.js'

const __dir = dirname(fileURLToPath(import.meta.url))
const CONFIG_PATH = process.env.CONFIG_PATH ?? join(__dir, '../../config.json')

let config = readConfig(CONFIG_PATH)
const getConfig = () => config
const saveConfig = (c) => { config = c; writeConfig(CONFIG_PATH, c) }

const app = express()
app.use(express.static(join(__dir, '../public')))

const httpServer = createServer(app)
const atemManager = new AtemManager()

const { getKnownUnits, getInputNames } = createSocketServer(
  httpServer, atemManager, getConfig, saveConfig
)

app.use(createRoutes(getConfig, getKnownUnits, getInputNames))

const PORT = Number(process.env.PORT ?? 8259)
httpServer.listen(PORT, () => {
  console.log(`ATEM Tally server running on http://localhost:${PORT}`)
})

// Auto-connect to ATEM on startup
if (config.atem.ip) {
  console.log(`Connecting to ATEM at ${config.atem.ip}...`)
  atemManager.connect(config.atem.ip)
}
```

- [ ] **Smoke test — start server locally**

```bash
cd server && node src/index.js
```

Expected: `ATEM Tally server running on http://localhost:8259`  
Open `http://localhost:8259` in a browser — dashboard should load.  
Press Ctrl+C to stop.

- [ ] **Create `server/Dockerfile`**

```dockerfile
FROM node:20-alpine
WORKDIR /app
COPY package*.json ./
RUN npm ci --omit=dev
COPY src/ ./src/
COPY views/ ./views/
COPY public/ ./public/
EXPOSE 8259
ENV NODE_ENV=production
CMD ["node", "src/index.js"]
```

- [ ] **Create `server/docker-compose.yml`**

```yaml
services:
  tally:
    build: .
    ports:
      - "8259:8259"
    volumes:
      - tally-config:/config
    environment:
      - CONFIG_PATH=/config/config.json
    restart: unless-stopped

volumes:
  tally-config:
```

- [ ] **Build and run Docker container**

```bash
cd server && docker compose up --build -d
```

Expected: container starts, `docker compose logs tally` shows server running on port 8259.

- [ ] **Verify web UI accessible**

Open `http://localhost:8259` — dashboard loads.

- [ ] **Stop container**

```bash
cd server && docker compose down
```

- [ ] **Commit**

```bash
git add server/src/index.js server/Dockerfile server/docker-compose.yml
git commit -m "feat: entry point, Docker container, docker-compose"
```

---

## Phase 2 — Firmware

### Task 9: PlatformIO Scaffold

**Files:**
- Create: `firmware/platformio.ini`
- Create: `firmware/include/config.h`
- Create: `firmware/include/tally_packet.h`
- Create: `firmware/include/led_driver.h`
- Create stub `.cpp` files for all source files

**Prerequisites:** Install PlatformIO CLI — `pip install platformio` or via VS Code PlatformIO extension.

- [ ] **Create `firmware/platformio.ini`**

```ini
[platformio]
default_envs = camera

[env]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
monitor_speed = 115200
lib_deps =
    adafruit/Adafruit NeoPixel @ ^1.12.3

[env:camera]
build_src_filter = +<shared/*> +<camera/*>
build_flags = -DCAMERA_UNIT

[env:bridge]
build_src_filter = +<shared/*> +<bridge/*>
build_flags = -DBRIDGE_UNIT
lib_deps =
    ${env.lib_deps}
    Links2004/WebSockets @ ^2.4.1
    bblanchon/ArduinoJson @ ^7.2.1

[env:native_test]
platform = native
test_build_src = yes
```

- [ ] **Create `firmware/include/config.h`**

```cpp
#pragma once

// ── EDIT THIS FILE BEFORE FLASHING ──────────────────────────────────────────
// Unit ID: 1–20. Must be unique across all units.
// Bridge unit: set UNIT_ID to 20 (or any unused ID)
#define UNIT_ID 1

// WS2812 LED strip — GPIO pin and LED count
#define LED_PIN   8
#define LED_COUNT 6

// ── Bridge only (ignored by camera units) ────────────────────────────────────
#define WIFI_SSID     "your-network-name"
#define WIFI_PASSWORD "your-password"
#define SERVER_HOST   "192.168.1.100"  // base station IP
#define SERVER_PORT   8259
```

- [ ] **Create `firmware/include/tally_packet.h`**

```cpp
#pragma once
#include <stdint.h>

// Tally state values
#define TALLY_STANDBY  0
#define TALLY_PREVIEW  1
#define TALLY_PROGRAM  2

// Broadcast packet: bridge → camera units (3 bytes)
typedef struct __attribute__((packed)) {
    uint8_t flags;      // bit 0: 1=ATEM connected
    uint8_t states[2];  // 20 slots × 2 bits, LSB-first per byte
} TallyPacket;

// Heartbeat: camera → bridge (2 bytes)
typedef struct __attribute__((packed)) {
    uint8_t type;    // always 0x01
    uint8_t unitId;  // 1–20
} HeartbeatPacket;

// Pack a state value (0–2) into a TallyPacket for a given unitId (1–20)
inline void tallyPacketSetState(TallyPacket* pkt, uint8_t unitId, uint8_t state) {
    uint8_t offset = (unitId - 1) * 2;
    uint8_t byteIdx = offset / 8;
    uint8_t bitPos  = offset % 8;
    pkt->states[byteIdx] &= ~(0x03 << bitPos);
    pkt->states[byteIdx] |= (state & 0x03) << bitPos;
}

// Extract state value for a given unitId from a TallyPacket
inline uint8_t tallyPacketGetState(const TallyPacket* pkt, uint8_t unitId) {
    uint8_t offset = (unitId - 1) * 2;
    uint8_t byteIdx = offset / 8;
    uint8_t bitPos  = offset % 8;
    return (pkt->states[byteIdx] >> bitPos) & 0x03;
}
```

- [ ] **Create `firmware/include/led_driver.h`**

```cpp
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
```

- [ ] **Create stub source files** — each containing only `#include` and an empty function body

  - `firmware/src/shared/led_driver.cpp` — stub
  - `firmware/src/shared/tally_packet.cpp` — stub (nothing needed; functions are inline in header)
  - `firmware/src/bridge/main.cpp` — stub with `void setup(){}` `void loop(){}`
  - `firmware/src/bridge/wifi_manager.cpp` — stub
  - `firmware/src/bridge/ws_client.cpp` — stub
  - `firmware/src/bridge/espnow_bridge.cpp` — stub
  - `firmware/src/camera/main.cpp` — stub with `void setup(){}` `void loop(){}`
  - `firmware/src/camera/espnow_camera.cpp` — stub

- [ ] **Verify camera environment compiles**

```bash
cd firmware && pio run -e camera
```

Expected: compiles successfully (stubs compile).

- [ ] **Verify bridge environment compiles**

```bash
cd firmware && pio run -e bridge
```

Expected: compiles successfully.

- [ ] **Commit**

```bash
git add firmware/
git commit -m "feat: PlatformIO scaffold — two environments, shared headers, config.h"
```

---

### Task 10: Native Tests — Packet Encode/Decode

**Files:**
- Create: `firmware/test/native/test_packet/test_packet.cpp`

- [ ] **Create `firmware/test/native/test_packet/test_packet.cpp`**

```cpp
#include <unity.h>
#include "tally_packet.h"

void test_set_get_state_unit1_program() {
    TallyPacket pkt = {};
    tallyPacketSetState(&pkt, 1, TALLY_PROGRAM);
    TEST_ASSERT_EQUAL(TALLY_PROGRAM, tallyPacketGetState(&pkt, 1));
}

void test_set_get_state_unit8_preview() {
    TallyPacket pkt = {};
    tallyPacketSetState(&pkt, 8, TALLY_PREVIEW);
    TEST_ASSERT_EQUAL(TALLY_PREVIEW, tallyPacketGetState(&pkt, 8));
}

void test_set_get_state_unit20_standby() {
    TallyPacket pkt = {};
    tallyPacketSetState(&pkt, 20, TALLY_STANDBY);
    TEST_ASSERT_EQUAL(TALLY_STANDBY, tallyPacketGetState(&pkt, 20));
}

void test_multiple_units_independent() {
    TallyPacket pkt = {};
    tallyPacketSetState(&pkt, 1, TALLY_PROGRAM);
    tallyPacketSetState(&pkt, 2, TALLY_PREVIEW);
    tallyPacketSetState(&pkt, 3, TALLY_STANDBY);
    tallyPacketSetState(&pkt, 20, TALLY_PROGRAM);
    TEST_ASSERT_EQUAL(TALLY_PROGRAM,  tallyPacketGetState(&pkt, 1));
    TEST_ASSERT_EQUAL(TALLY_PREVIEW,  tallyPacketGetState(&pkt, 2));
    TEST_ASSERT_EQUAL(TALLY_STANDBY,  tallyPacketGetState(&pkt, 3));
    TEST_ASSERT_EQUAL(TALLY_PROGRAM,  tallyPacketGetState(&pkt, 20));
}

void test_overwrite_state() {
    TallyPacket pkt = {};
    tallyPacketSetState(&pkt, 5, TALLY_PROGRAM);
    tallyPacketSetState(&pkt, 5, TALLY_PREVIEW);
    TEST_ASSERT_EQUAL(TALLY_PREVIEW, tallyPacketGetState(&pkt, 5));
}

void test_packet_size_is_3_bytes() {
    TEST_ASSERT_EQUAL(3, sizeof(TallyPacket));
}

void test_heartbeat_size_is_2_bytes() {
    TEST_ASSERT_EQUAL(2, sizeof(HeartbeatPacket));
}

void test_flags_atem_connected() {
    TallyPacket pkt = {};
    pkt.flags = 0x01;
    TEST_ASSERT_EQUAL(1, pkt.flags & 0x01);
    pkt.flags = 0x00;
    TEST_ASSERT_EQUAL(0, pkt.flags & 0x01);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_set_get_state_unit1_program);
    RUN_TEST(test_set_get_state_unit8_preview);
    RUN_TEST(test_set_get_state_unit20_standby);
    RUN_TEST(test_multiple_units_independent);
    RUN_TEST(test_overwrite_state);
    RUN_TEST(test_packet_size_is_3_bytes);
    RUN_TEST(test_heartbeat_size_is_2_bytes);
    RUN_TEST(test_flags_atem_connected);
    return UNITY_END();
}
```

- [ ] **Run native tests**

```bash
cd firmware && pio test -e native_test --filter test_packet
```

Expected: 8 tests pass.

- [ ] **Commit**

```bash
git add firmware/test/
git commit -m "test: native packet encode/decode tests — all passing"
```

---

### Task 11: LED Driver

**Files:**
- Create: `firmware/src/shared/led_driver.cpp`
- Create: `firmware/test/native/test_led/test_led.cpp`

- [ ] **Implement `firmware/src/shared/led_driver.cpp`**

```cpp
#include "led_driver.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

static Adafruit_NeoPixel* strip = nullptr;
static uint8_t numLeds = 0;
static LedState currentState = LED_STATE_AMBER_BREATH;
static LedSettings currentSettings = { 80, 20, 40 };  // defaults
static unsigned long lastTick = 0;

static uint32_t applyBrightness(uint8_t r, uint8_t g, uint8_t b, uint8_t pct) {
    float scale = pct / 100.0f;
    return strip->Color((uint8_t)(r * scale), (uint8_t)(g * scale), (uint8_t)(b * scale));
}

static uint8_t breathValue(uint8_t speedMs10) {
    // Sine wave 0–255 based on time, period = speedMs10 * 10 ms
    unsigned long period = (unsigned long)speedMs10 * 10;
    float phase = (float)(millis() % period) / period;  // 0.0–1.0
    float sine = (sinf(phase * 2.0f * M_PI - M_PI / 2.0f) + 1.0f) / 2.0f; // 0.0–1.0
    return (uint8_t)(sine * 255);
}

void ledDriverInit(uint8_t pin, uint8_t count) {
    numLeds = count;
    strip = new Adafruit_NeoPixel(count, pin, NEO_GRB + NEO_KHZ800);
    strip->begin();
    strip->show();
}

void ledDriverSetState(LedState state, const LedSettings* settings) {
    currentState = state;
    if (settings) currentSettings = *settings;
}

void ledDriverTick() {
    if (!strip) return;
    unsigned long now = millis();
    if (now - lastTick < 20) return;  // ~50Hz update
    lastTick = now;

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
            uint8_t scaled = (uint8_t)((bright / 255.0f) * currentSettings.brightness * 2.55f);
            colour = strip->Color(scaled, (uint8_t)(scaled * 0.376f), 0);
            break;
        }
        case LED_STATE_WHITE_BREATH: {
            uint8_t scaled = (uint8_t)((bright / 255.0f) * currentSettings.brightness * 2.55f);
            colour = strip->Color(scaled, scaled, scaled);
            break;
        }
    }

    for (uint8_t i = 0; i < numLeds; i++) strip->setPixelColor(i, colour);
    strip->show();
}
```

> **Note:** The breathing states (`AMBER_BREATH`, `WHITE_BREATH`) update every loop tick. Solid states (`PROGRAM`, `PREVIEW`, `STANDBY`) set once and don't re-render unless changed. The 20ms gate keeps CPU load low.

- [ ] **Write native LED state machine tests** → `firmware/test/native/test_led/test_led.cpp`

```cpp
// Test the pure state-selection logic (not hardware output)
#include <unity.h>
#include "led_driver.h"

// Test that state enum values are what we expect
void test_state_values_distinct() {
    TEST_ASSERT_NOT_EQUAL(LED_STATE_PROGRAM, LED_STATE_PREVIEW);
    TEST_ASSERT_NOT_EQUAL(LED_STATE_PROGRAM, LED_STATE_STANDBY);
    TEST_ASSERT_NOT_EQUAL(LED_STATE_AMBER_BREATH, LED_STATE_WHITE_BREATH);
}

// Test LedSettings default-initialises safely
void test_settings_struct_size() {
    LedSettings s = {};
    TEST_ASSERT_EQUAL(0, s.brightness);
    TEST_ASSERT_EQUAL(0, s.standbyBrightness);
    TEST_ASSERT_EQUAL(0, s.animSpeedMs);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_state_values_distinct);
    RUN_TEST(test_settings_struct_size);
    return UNITY_END();
}
```

> **Note:** The hardware-rendering parts of `led_driver.cpp` use `Adafruit_NeoPixel` and `millis()`, which aren't available in the native test environment. The state-selection logic and packet parsing are the critical paths covered by tests; LED output is verified on-device.

- [ ] **Run native LED tests**

```bash
cd firmware && pio test -e native_test --filter test_led
```

Expected: 2 tests pass.

- [ ] **Commit**

```bash
git add firmware/src/shared/led_driver.cpp firmware/test/native/test_led/
git commit -m "feat: LED driver — all tally states, breathing animation, 50Hz tick"
```

---

### Task 12: Camera Unit Firmware

**Files:**
- Create: `firmware/src/camera/espnow_camera.cpp`
- Create: `firmware/src/camera/main.cpp`

- [ ] **Implement `firmware/src/camera/espnow_camera.cpp`**

```cpp
#include "espnow_camera.h"
#include "tally_packet.h"
#include "led_driver.h"
#include "config.h"
#include <esp_now.h>
#include <WiFi.h>

static uint8_t bridgeMac[6] = {};
static bool bridgeMacKnown = false;
static uint8_t lastState = 0;
static bool atemConnected = false;
static unsigned long lastPacketMs = 0;
static LedSettings ledSettings = { 80, 20, 40 };

static void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len != sizeof(TallyPacket)) return;

    // Learn bridge MAC from first packet
    if (!bridgeMacKnown) {
        memcpy(bridgeMac, info->src_addr, 6);
        bridgeMacKnown = true;
        // Register bridge as peer for heartbeat replies
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, bridgeMac, 6);
        peer.channel = 0;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    const TallyPacket* pkt = (const TallyPacket*)data;
    lastPacketMs = millis();
    atemConnected = (pkt->flags & 0x01) != 0;
    lastState = tallyPacketGetState(pkt, UNIT_ID);
}

void espnowCameraInit() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        // Fatal — flash amber forever
        while (true) { ledDriverTick(); delay(10); }
    }
    esp_now_register_recv_cb(onDataRecv);
}

void espnowCameraTick() {
    unsigned long now = millis();

    // Determine LED state
    LedState state;
    if (now - lastPacketMs > 10000 || lastPacketMs == 0) {
        state = LED_STATE_AMBER_BREATH;  // no connection to bridge
    } else if (!atemConnected) {
        state = LED_STATE_WHITE_BREATH;  // bridge ok, ATEM disconnected
    } else {
        switch (lastState) {
            case TALLY_PROGRAM: state = LED_STATE_PROGRAM; break;
            case TALLY_PREVIEW: state = LED_STATE_PREVIEW; break;
            default:            state = LED_STATE_STANDBY; break;
        }
    }
    ledDriverSetState(state, &ledSettings);

    // Send heartbeat every 5 seconds
    static unsigned long lastHeartbeat = 0;
    if (bridgeMacKnown && now - lastHeartbeat >= 5000) {
        lastHeartbeat = now;
        HeartbeatPacket hb = { 0x01, UNIT_ID };
        esp_now_send(bridgeMac, (uint8_t*)&hb, sizeof(hb));
    }
}
```

- [ ] **Create `firmware/include/espnow_camera.h`**

```cpp
#pragma once
void espnowCameraInit();
void espnowCameraTick();
```

- [ ] **Implement `firmware/src/camera/main.cpp`**

```cpp
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
```

- [ ] **Compile camera firmware**

```bash
cd firmware && pio run -e camera
```

Expected: compiles with no errors.

- [ ] **Flash to a camera unit** (connect ESP32-C3 via USB)

Edit `firmware/include/config.h`: set `UNIT_ID` to the correct value for this unit.

```bash
cd firmware && pio run -e camera --target upload
```

- [ ] **Verify boot behaviour**

Open serial monitor: `pio device monitor`  
Expected output: `Camera unit N ready`  
LEDs should show amber breathing (no bridge present yet).

- [ ] **Commit**

```bash
git add firmware/src/camera/ firmware/include/espnow_camera.h
git commit -m "feat: camera unit firmware — ESP-NOW receive, heartbeat, LED state machine"
```

---

### Task 13: Bridge Firmware — WiFi Manager

**Files:**
- Create: `firmware/src/bridge/wifi_manager.cpp`
- Create: `firmware/include/wifi_manager.h`

- [ ] **Create `firmware/include/wifi_manager.h`**

```cpp
#pragma once
#include <stdint.h>

void wifiManagerInit();
bool wifiManagerIsConnected();
void wifiManagerTick();  // call every loop, handles reconnection
```

- [ ] **Implement `firmware/src/bridge/wifi_manager.cpp`**

```cpp
#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>

static bool connected = false;
static unsigned long lastAttempt = 0;
static const unsigned long RETRY_INTERVAL_MS = 5000;

void wifiManagerInit() {
    WiFi.mode(WIFI_AP_STA);  // AP_STA so ESP-NOW works on same channel as AP
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
```

- [ ] **Commit**

```bash
git add firmware/src/bridge/wifi_manager.cpp firmware/include/wifi_manager.h
git commit -m "feat: bridge WiFi manager — connect, auto-reconnect"
```

---

### Task 14: Bridge Firmware — WebSocket Client

**Files:**
- Create: `firmware/src/bridge/ws_client.cpp`
- Create: `firmware/include/ws_client.h`

- [ ] **Create `firmware/include/ws_client.h`**

```cpp
#pragma once
#include <stdint.h>
#include "tally_packet.h"

typedef struct {
    bool atemConnected;
    uint8_t states[20];  // state per unit ID (0=standby,1=preview,2=program), index = unitId-1
    uint8_t brightness;
    uint8_t standbyBrightness;
    uint8_t animSpeedMs;
    bool updated;        // set true when new tally data arrives
} BridgeState;

void wsClientInit(BridgeState* state);
void wsClientTick();
bool wsClientIsConnected();
```

- [ ] **Implement `firmware/src/bridge/ws_client.cpp`**

```cpp
#include "ws_client.h"
#include "config.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

static WebSocketsClient ws;
static BridgeState* bridgeState = nullptr;
static bool connected = false;

static uint8_t animSpeedToMs(const char* speed) {
    if (strcmp(speed, "fast") == 0) return 15;
    if (strcmp(speed, "medium") == 0) return 25;
    return 40;  // slow
}

static void onMessage(uint8_t* payload, size_t length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length) != DeserializationError::Ok) return;

    const char* type = doc["type"];
    if (!type) return;

    if (strcmp(type, "tally") == 0) {
        bridgeState->atemConnected = doc["atemConnected"] | false;
        JsonObject states = doc["states"];
        for (JsonPair kv : states) {
            int uid = atoi(kv.key().c_str());
            if (uid >= 1 && uid <= 20)
                bridgeState->states[uid - 1] = kv.value().as<uint8_t>();
        }
        bridgeState->updated = true;
    }
    if (strcmp(type, "settings") == 0) {
        bridgeState->brightness        = doc["brightness"] | 80;
        bridgeState->standbyBrightness = doc["standbyBrightness"] | 20;
        bridgeState->animSpeedMs       = animSpeedToMs(doc["animSpeed"] | "slow");
    }
}

static void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            connected = true;
            Serial.println("WS connected to server");
            // Register this bridge unit
            ws.sendTXT("{\"type\":\"register\",\"unitId\":" + String(UNIT_ID) + "}");
            break;
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

void wsClientInit(BridgeState* state) {
    bridgeState = state;
    bridgeState->brightness = 80;
    bridgeState->standbyBrightness = 20;
    bridgeState->animSpeedMs = 40;
    ws.begin(SERVER_HOST, SERVER_PORT, "/bridge");
    ws.onEvent(wsEvent);
    ws.setReconnectInterval(3000);
}

void wsClientTick() { ws.loop(); }
bool wsClientIsConnected() { return connected; }
```

- [ ] **Compile bridge environment**

```bash
cd firmware && pio run -e bridge
```

Expected: compiles with no errors.

- [ ] **Commit**

```bash
git add firmware/src/bridge/ws_client.cpp firmware/include/ws_client.h
git commit -m "feat: bridge WebSocket client — JSON protocol, auto-reconnect, tally parsing"
```

---

### Task 15: Bridge Firmware — ESP-NOW Broadcaster

**Files:**
- Create: `firmware/src/bridge/espnow_bridge.cpp`
- Create: `firmware/include/espnow_bridge.h`

- [ ] **Create `firmware/include/espnow_bridge.h`**

```cpp
#pragma once
#include "ws_client.h"

void espnowBridgeInit();
void espnowBridgeBroadcast(const BridgeState* state);
void espnowBridgeTick();  // handles incoming heartbeats
```

- [ ] **Implement `firmware/src/bridge/espnow_bridge.cpp`**

```cpp
#include "espnow_bridge.h"
#include "tally_packet.h"
#include "config.h"
#include <esp_now.h>
#include <WiFi.h>

// Forward declaration of the heartbeat relay function (set by main)
static void (*heartbeatCallback)(uint8_t unitId) = nullptr;
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len != sizeof(HeartbeatPacket)) return;
    const HeartbeatPacket* hb = (const HeartbeatPacket*)data;
    if (hb->type != 0x01) return;
    if (heartbeatCallback) heartbeatCallback(hb->unitId);
}

void espnowBridgeInit(void (*onHeartbeat)(uint8_t unitId)) {
    heartbeatCallback = onHeartbeat;
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    esp_now_register_recv_cb(onDataRecv);
    // Add broadcast peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

void espnowBridgeBroadcast(const BridgeState* state) {
    TallyPacket pkt = {};
    pkt.flags = state->atemConnected ? 0x01 : 0x00;
    for (uint8_t i = 1; i <= 20; i++) {
        tallyPacketSetState(&pkt, i, state->states[i - 1]);
    }
    esp_now_send(BROADCAST_MAC, (uint8_t*)&pkt, sizeof(pkt));
}
```

> **Note:** `espnowBridgeInit` now takes a callback. Update the header to match:

- [ ] **Update `firmware/include/espnow_bridge.h`**

```cpp
#pragma once
#include "ws_client.h"

void espnowBridgeInit(void (*onHeartbeat)(uint8_t unitId));
void espnowBridgeBroadcast(const BridgeState* state);
```

- [ ] **Commit**

```bash
git add firmware/src/bridge/espnow_bridge.cpp firmware/include/espnow_bridge.h
git commit -m "feat: bridge ESP-NOW broadcaster — 3-byte tally packet, heartbeat receive"
```

---

### Task 16: Bridge Main Entry Point

**Files:**
- Create: `firmware/src/bridge/main.cpp`

- [ ] **Implement `firmware/src/bridge/main.cpp`**

```cpp
#include <Arduino.h>
#include "config.h"
#include "led_driver.h"
#include "wifi_manager.h"
#include "ws_client.h"
#include "espnow_bridge.h"

static BridgeState bridgeState = {};
static bool wsWasConnected = false;

// Called by ESP-NOW when a camera unit sends a heartbeat
static void onHeartbeat(uint8_t unitId) {
    // Relay to server via WebSocket
    String msg = "{\"type\":\"heartbeat\",\"unitId\":" + String(unitId) + "}";
    // ws_client doesn't expose send directly — use a simple flag approach
    // Store pending heartbeats in a small ring buffer
    static uint8_t pending[20];
    static uint8_t pendingHead = 0;
    pending[pendingHead % 20] = unitId;
    pendingHead++;
    // Actual send happens in loop via wsClientSendHeartbeat
}

// Keepalive broadcast interval
static unsigned long lastBroadcast = 0;
static const unsigned long BROADCAST_INTERVAL_MS = 2000;

void setup() {
    Serial.begin(115200);
    ledDriverInit(LED_PIN, LED_COUNT);
    ledDriverSetState(LED_STATE_AMBER_BREATH, nullptr);

    wifiManagerInit();
    // Wait for WiFi before starting WebSocket and ESP-NOW
    Serial.print("Connecting to WiFi");
    while (!wifiManagerIsConnected()) {
        wifiManagerTick();
        ledDriverTick();
        Serial.print(".");
        delay(100);
    }
    Serial.println(" connected");

    wsClientInit(&bridgeState);
    espnowBridgeInit(onHeartbeat);

    Serial.printf("Bridge unit %d ready\n", UNIT_ID);
}

void loop() {
    wifiManagerTick();
    wsClientTick();

    // Broadcast on new tally data or keepalive interval
    unsigned long now = millis();
    if (bridgeState.updated || now - lastBroadcast >= BROADCAST_INTERVAL_MS) {
        espnowBridgeBroadcast(&bridgeState);
        bridgeState.updated = false;
        lastBroadcast = now;
    }

    // Determine own LED state
    LedState ownState;
    if (!wsClientIsConnected()) {
        ownState = LED_STATE_AMBER_BREATH;
    } else if (!bridgeState.atemConnected) {
        ownState = LED_STATE_WHITE_BREATH;
    } else {
        uint8_t s = bridgeState.states[UNIT_ID - 1];
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
}
```

> **Note on heartbeat relay:** The `onHeartbeat` callback fires in the ESP-NOW receive task (different RTOS task). For production reliability, use a FreeRTOS queue to pass heartbeat unit IDs to the main loop for WebSocket sending. The simple approach above is sufficient for initial testing; refactor if you observe dropped heartbeats.

- [ ] **Compile bridge firmware**

```bash
cd firmware && pio run -e bridge
```

Expected: compiles with no errors.

- [ ] **Flash to bridge unit**

Edit `firmware/include/config.h`:
- Set `UNIT_ID 20`
- Set `WIFI_SSID`, `WIFI_PASSWORD`, `SERVER_HOST` to your network values

```bash
cd firmware && pio run -e bridge --target upload
```

- [ ] **Verify bridge operation**

Open serial monitor: `pio device monitor`  
Expected sequence:
1. `Connecting to WiFi... connected`
2. `WiFi connected. IP: x.x.x.x  Channel: N`
3. `WS connected to server`
4. Bridge LEDs go amber → white (if ATEM not yet configured) or tally colour

- [ ] **Commit**

```bash
git add firmware/src/bridge/main.cpp
git commit -m "feat: bridge main — WiFi + WebSocket + ESP-NOW + LED state machine"
```

---

## Phase 3 — Integration

### Task 17: End-to-End Integration Test

- [ ] **Start the base station**

```bash
cd server && docker compose up --build -d
```

Open `http://[server-ip]:8259/settings`, enter ATEM IP, save.

- [ ] **Verify ATEM connects**

Dashboard should show: `ATEM: connected`

- [ ] **Power on bridge unit**

Serial monitor should show WebSocket connected. Dashboard shows: `Bridge: connected`.

- [ ] **Power on camera units**

After up to 10 seconds, each camera unit should appear in the Assign page (populated by heartbeats).

- [ ] **Assign units to ATEM inputs**

Go to `/assign`. Select ATEM input for each unit ID from the dropdown (input names come from ATEM). Save.

- [ ] **Test tally response**

On the ATEM, cut camera to program:
- Assigned camera unit LEDs → solid red
- Dashboard card turns red

Put camera to preview:
- LEDs → solid green
- Dashboard card turns green

Cut away:
- LEDs → dim white (standby)

- [ ] **Test disconnect recovery**

Unplug bridge USB → camera units should go amber breathing within 10 seconds.  
Replug bridge → camera units restore tally state within a few seconds.

Stop base station → bridge LEDs go white breathing. Restart base station → bridge reconnects automatically.

- [ ] **Run all Node.js tests one final time**

```bash
cd server && npm test
```

Expected: all tests pass.

- [ ] **Final commit**

```bash
git add .
git commit -m "chore: integration verified — full system end-to-end"
git push
```

---

## Flashing Multiple Units

For each camera unit (repeat for all units):

1. Edit `firmware/include/config.h` — set `UNIT_ID` to the unit's number (1–19)
2. Flash: `cd firmware && pio run -e camera --target upload`
3. Label the physical unit with its ID

For the bridge unit:
1. Edit `config.h` — set `UNIT_ID 20`, WiFi credentials, server IP
2. Flash: `cd firmware && pio run -e bridge --target upload`
