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
          } else if (oldA?.role === 'bridge' && bridgeWs === ws) {
            bridgeWs = null
            io.emit('bridgeStatus', 'disconnected')
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
