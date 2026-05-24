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
    this.#atem.on('stateChanged', (state, paths) => {
      const relevant = paths.some(p =>
        p.startsWith('video') || p.startsWith('inputs')
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

    // In atem-connection v3, tally comes from video.mixEffects — there is no state.tallys
    const tallys = {}
    if (state.video?.mixEffects) {
      for (const me of state.video.mixEffects) {
        if (!me) continue
        const prog = me.programInput
        const prev = me.previewInput
        if (prog) {
          if (!tallys[prog]) tallys[prog] = { program: false, preview: false }
          tallys[prog].program = true
        }
        if (prev) {
          if (!tallys[prev]) tallys[prev] = { program: false, preview: false }
          tallys[prev].preview = true
        }
      }
    }

    const inputNames = {}
    if (state.inputs) {
      for (const [src, inp] of Object.entries(state.inputs)) {
        inputNames[Number(src)] = inp?.properties?.longName ?? `Input ${src}`
      }
    }

    return { tallys, inputNames }
  }

  #emitTally() {
    const { tallys, inputNames } = this.getState()
    this.emit('tally', tallys, inputNames)
  }
}
