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
