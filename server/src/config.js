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

export function readConfig(path) {
  if (!existsSync(path)) return structuredClone(DEFAULT_CONFIG)
  try {
    const saved = JSON.parse(readFileSync(path, 'utf8'))
    return {
      atem: { ...DEFAULT_CONFIG.atem, ...saved.atem },
      leds: { ...DEFAULT_CONFIG.leds, ...saved.leds },
      units: saved.units ?? {},
    }
  } catch {
    return structuredClone(DEFAULT_CONFIG)
  }
}

export function writeConfig(path, config) {
  writeFileSync(path, JSON.stringify(config, null, 2))
}
