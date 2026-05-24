// buildUnitStates: returns { [unitId: string]: 0|1|2 }
// 0=standby, 1=preview, 2=program
export function buildUnitStates(atemTallys, unitMap) {
  const states = {}
  for (const [unitId, cfg] of Object.entries(unitMap)) {
    const input = cfg.atemInput ?? 0
    if (!input) { states[unitId] = 0; continue }
    const tally = atemTallys[input]
    if (!tally) { states[unitId] = 0; continue }
    states[unitId] = tally.program ? 2 : tally.preview ? 1 : 0
  }
  return states
}
