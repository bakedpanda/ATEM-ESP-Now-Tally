import { createServer } from 'http'
import { networkInterfaces } from 'os'
import express from 'express'
import { fileURLToPath } from 'url'
import { join, dirname } from 'path'
import { existsSync } from 'fs'
import mdns from 'multicast-dns'
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
  advertiseMdns()
})

function advertiseMdns() {
  const m = mdns()
  const hostname = 'atem-tally.local'

  m.on('error', (err) => console.error('mDNS error:', err))

  function localIPs() {
    return Object.values(networkInterfaces())
      .flat()
      .filter(n => n.family === 'IPv4' && !n.internal)
      .map(n => n.address)
  }

  m.on('query', (query) => {
    const ips = localIPs()
    if (!ips.length) return
    const answers = []
    for (const q of query.questions) {
      if ((q.type === 'A' || q.type === 'ANY') && q.name === hostname) {
        for (const ip of ips) {
          answers.push({ name: hostname, type: 'A', ttl: 300, data: ip })
        }
      }
    }
    if (answers.length) m.respond({ answers })
  })

  console.log(`mDNS: advertising as ${hostname}`)
}

// Auto-connect to ATEM on startup
if (config.atem.ip) {
  console.log(`Connecting to ATEM at ${config.atem.ip}...`)
  atemManager.connect(config.atem.ip)
}
