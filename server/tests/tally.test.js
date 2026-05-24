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
