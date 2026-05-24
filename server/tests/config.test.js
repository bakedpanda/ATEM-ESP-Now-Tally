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
