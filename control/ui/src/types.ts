export type DeckId = 'A' | 'B'
export type HostConnection = 'connecting' | 'connected' | 'disconnected' | 'error'
export type EnvelopeType = 'Command' | 'StateSnapshot' | 'SignalFrame' | 'DeviceEvent' | 'ErrorEvent'

export interface EngineEnvelope<T = Record<string, unknown>> {
  version: 1
  type: EnvelopeType
  requestId?: string
  payload: T
}

export interface SceneDescriptor {
  id: string
  name: string
  category?: string
  parameters?: SceneParameterDescriptor[]
}

export interface SceneParameterDescriptor {
  id: string
  name: string
  type: 'float'
  index: number
  min: number
  max: number
  default: number
}

export interface EffectState {
  id: string | number
  name: string
  amount: number
}

export interface ModulationState {
  slot: number
  source: string
  target: string
  amount: number
  smoothing: number
  enabled: boolean
}

export interface DeckState {
  sceneId: string
  sceneParams: number[]
  effects: EffectState[]
  modulations: ModulationState[]
}

export interface AudioSource {
  id: string
  name: string
  kind?: string
  connected?: boolean
}

export interface AudioState {
  sourceId: string
  sources: AudioSource[]
  connected: boolean
  receiving: boolean
  rms: number
  peak: number
  low: number
  mid: number
  high: number
  clipping: boolean
}

export interface CueState {
  index: number
  saved: boolean
  label?: string
}

export interface PerformanceState {
  fps: number
  targetFps: number
  frameTimeMs: number
  adaptiveQuality: boolean
}

export interface OutputState {
  spout: {
    ready: boolean
    connected: boolean
    senderName: string
  }
  width: number
  height: number
}

export interface EngineState {
  revision: number
  decks: Record<DeckId, DeckState>
  crossfader: number
  masterEffects: number[]
  blackout: boolean
  panicDim: boolean
  sceneCatalog: SceneDescriptor[]
  audio: AudioState
  cues: CueState[]
  performance: PerformanceState
  output: OutputState
}

export interface HostStatusMessage {
  kind: 'HostStatus'
  payload: {
    status: HostConnection
    attempt: number
    message: string
  }
}

export interface HostViewState {
  connection: HostConnection
  connectionMessage: string
  engine: EngineState
  masterEffectsAvailable: boolean
  notice?: { kind: 'info' | 'error'; message: string }
}

export const emptyDeck = (): DeckState => ({
  sceneId: '',
  sceneParams: [0.5, 0.5, 0.5, 0.5],
  effects: [
    { id: 0, name: 'Bloom', amount: 0 },
    { id: 1, name: 'Feedback', amount: 0 },
    { id: 2, name: 'Kaleidoscope', amount: 0 },
    { id: 3, name: 'Pixelate', amount: 0 },
  ],
  modulations: [
    { slot: 0, source: 'audio.kick', target: 'deck.effect.0', amount: 0, smoothing: 0.15, enabled: false },
    { slot: 1, source: 'audio.low', target: 'deck.effect.1', amount: 0, smoothing: 0.25, enabled: false },
  ],
})

export const initialEngineState = (): EngineState => ({
  revision: 0,
  decks: { A: emptyDeck(), B: emptyDeck() },
  crossfader: 0.5,
  masterEffects: [0, 0, 0, 0],
  blackout: false,
  panicDim: false,
  sceneCatalog: [],
  audio: {
    sourceId: '',
    sources: [],
    connected: false,
    receiving: false,
    rms: 0,
    peak: 0,
    low: 0,
    mid: 0,
    high: 0,
    clipping: false,
  },
  cues: Array.from({ length: 8 }, (_, index) => ({ index, saved: false })),
  performance: { fps: 0, targetFps: 60, frameTimeMs: 0, adaptiveQuality: false },
  output: { spout: { ready: false, connected: false, senderName: 'PrismForge' }, width: 0, height: 0 },
})
