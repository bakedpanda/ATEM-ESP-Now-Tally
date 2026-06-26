// buildUnitStates: returns { [unitId: string]: 0|1|2 }
// units: { [mac]: { role: 'receiver'|'bridge', unitId: number, atemInput: number } }
export function buildUnitStates(atemTallys, units) {
  const states = {}
  for (const [, cfg] of Object.entries(units)) {
    if (cfg.role !== 'receiver') continue
    if (!cfg.unitId) continue
    const id = String(cfg.unitId)
    const input = cfg.atemInput ?? 0
    if (!input) { states[id] = 0; continue }
    const tally = atemTallys[input]
    if (!tally) { states[id] = 0; continue }
    states[id] = tally.program ? 2 : tally.preview ? 1 : 0
  }
  return states
}
