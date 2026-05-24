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
