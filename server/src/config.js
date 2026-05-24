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
