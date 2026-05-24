import { Atem } from 'atem-connection'
import { EventEmitter } from 'events'

// Emits:
//   'tally'  (atemTallys: object, inputNames: object) on state change
//   'status' ('connecting'|'connected'|'disconnected')

export class AtemManager extends EventEmitter {
  #atem = new Atem({ debugBuffers: false })
  #ip = null
  #connected = false

  constructor() {
    super()
    this.#atem.on('connected', () => {
      this.#connected = true
      this.emit('status', 'connected')
      this.#emitTally()
    })
    this.#atem.on('disconnected', () => {
      this.#connected = false
      this.emit('status', 'disconnected')
    })
    this.#atem.on('stateChanged', (_state, paths) => {
      const relevant = paths.some(p =>
        p.startsWith('tallys') || p.startsWith('inputs') || p.startsWith('video')
      )
      if (relevant) this.#emitTally()
    })
  }

  connect(ip) {
    this.#ip = ip
    if (!ip) return
    this.emit('status', 'connecting')
    this.#atem.connect(ip)
  }

  disconnect() {
    this.#atem.disconnect()
    this.#connected = false
  }

  get isConnected() { return this.#connected }

  // Returns { [sourceNum]: { program, preview } } and { [sourceNum]: name }
  getState() {
    const state = this.#atem.state
    if (!state) return { tallys: {}, inputNames: {} }

    const tallys = {}
    if (state.tallys) {
      for (const [src, t] of Object.entries(state.tallys)) {
        if (t) tallys[Number(src)] = { program: !!t.program, preview: !!t.preview }
      }
    }

    const inputNames = {}
    if (state.inputs) {
      for (const [src, inp] of Object.entries(state.inputs)) {
        const n = Number(src)
        const name = inp?.properties?.longName
        if (!name) continue
        // Skip internal sources: Black (0), Bars/Color/ME outputs have no longName
        // or have internalPortType 0 = External. Keep everything with a name —
        // the user can ignore internal sources in the UI.
        inputNames[n] = name
      }
    }

    return { tallys, inputNames }
  }

  #emitTally() {
    const { tallys, inputNames } = this.getState()
    this.emit('tally', tallys, inputNames)
  }
}
