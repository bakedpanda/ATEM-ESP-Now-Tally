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
