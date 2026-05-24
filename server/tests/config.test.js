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

test('mixed key formats are cleared on load (safety)', () => {
  writeFileSync(TMP, JSON.stringify({
    units: { '3': { atemInput: 2 }, 'AA:BB:CC:DD:EE:01': { atemInput: 1 } }
  }))
  const c = readConfig(TMP)
  expect(c.units).toEqual({})
})
