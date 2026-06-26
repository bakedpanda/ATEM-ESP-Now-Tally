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

// MAC addresses match XX:XX:XX:XX:XX:XX (case-insensitive hex pairs)
export const MAC_RE = /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/

function isMacKeyed(units) {
  const keys = Object.keys(units)
  return keys.length === 0 || keys.every(k => MAC_RE.test(k))
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
