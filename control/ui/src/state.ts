import type {
  AudioState,
  DeckId,
  EngineEnvelope,
  EngineState,
  HostStatusMessage,
  HostViewState,
  PerformanceState,
} from './types'
import { initialEngineState } from './types'

type ViewAction =
  | { type: 'host-status'; message: HostStatusMessage }
  | { type: 'engine-envelope'; envelope: EngineEnvelope }
  | { type: 'optimistic-crossfader'; value: number }
  | { type: 'optimistic-scene'; deck: DeckId; sceneId: string }
  | { type: 'optimistic-effect'; deck: DeckId; index: number; amount: number }
  | { type: 'optimistic-master-effect'; index: number; amount: number }
  | { type: 'optimistic-safety'; control: 'blackout' | 'panicDim'; enabled: boolean }
  | { type: 'clear-notice' }

export const initialViewState = (): HostViewState => ({
  connection: 'connecting',
  connectionMessage: 'Looking for PrismForge engine',
  engine: initialEngineState(),
  masterEffectsAvailable: false,
})

const clamp01 = (value: number) => Math.min(1, Math.max(0, Number.isFinite(value) ? value : 0))

const normalizeMasterEffects = (current: number[], candidate?: number[]): number[] =>
  Array.from({ length: 4 }, (_, index) =>
    clamp01(Array.isArray(candidate) && index in candidate ? candidate[index] : current[index] ?? 0),
  )

const normalizeAudio = (current: AudioState, candidate?: Partial<AudioState>): AudioState => ({
  ...current,
  ...candidate,
  sources: candidate?.sources ?? current.sources,
  rms: clamp01(candidate?.rms ?? current.rms),
  peak: clamp01(candidate?.peak ?? current.peak),
  low: clamp01(candidate?.low ?? current.low),
  mid: clamp01(candidate?.mid ?? current.mid),
  high: clamp01(candidate?.high ?? current.high),
})

const normalizePerformance = (
  current: PerformanceState,
  candidate?: Partial<PerformanceState>,
): PerformanceState => ({
  ...current,
  ...candidate,
  fps: Math.max(0, candidate?.fps ?? current.fps),
  targetFps: Math.max(1, candidate?.targetFps ?? current.targetFps),
  frameTimeMs: Math.max(0, candidate?.frameTimeMs ?? current.frameTimeMs),
})

export function viewReducer(state: HostViewState, action: ViewAction): HostViewState {
  switch (action.type) {
    case 'host-status':
      return {
        ...state,
        connection: action.message.payload.status,
        connectionMessage: action.message.payload.message,
      }
    case 'engine-envelope': {
      const { envelope } = action
      if (envelope.version !== 1) return state

      if (envelope.type === 'StateSnapshot') {
        const snapshot = envelope.payload as unknown as Partial<EngineState>
        const decks = snapshot.decks ?? state.engine.decks
        const masterEffectsAvailable = Array.isArray(snapshot.masterEffects) &&
          snapshot.masterEffects.length === 4
        return {
          ...state,
          masterEffectsAvailable,
          engine: {
            ...state.engine,
            ...snapshot,
            decks: {
              A: { ...state.engine.decks.A, ...decks.A },
              B: { ...state.engine.decks.B, ...decks.B },
            },
            sceneCatalog: snapshot.sceneCatalog ?? state.engine.sceneCatalog,
            audio: normalizeAudio(state.engine.audio, snapshot.audio),
            cues: snapshot.cues ?? state.engine.cues,
            performance: normalizePerformance(state.engine.performance, snapshot.performance),
            output: {
              ...state.engine.output,
              ...snapshot.output,
              spout: { ...state.engine.output.spout, ...snapshot.output?.spout },
            },
            crossfader: clamp01(snapshot.crossfader ?? state.engine.crossfader),
            masterEffects: normalizeMasterEffects([0, 0, 0, 0], snapshot.masterEffects),
          },
        }
      }

      if (envelope.type === 'SignalFrame') {
        const signal = envelope.payload as { audio?: Partial<AudioState>; performance?: Partial<PerformanceState> }
        return {
          ...state,
          engine: {
            ...state.engine,
            audio: normalizeAudio(state.engine.audio, signal.audio),
            performance: normalizePerformance(state.engine.performance, signal.performance),
          },
        }
      }

      if (envelope.type === 'ErrorEvent' || envelope.type === 'DeviceEvent') {
        const payload = envelope.payload as { message?: string }
        return {
          ...state,
          notice: {
            kind: envelope.type === 'ErrorEvent' ? 'error' : 'info',
            message: payload.message ?? (envelope.type === 'ErrorEvent' ? 'Engine error' : 'Device changed'),
          },
        }
      }
      return state
    }
    case 'optimistic-crossfader':
      return { ...state, engine: { ...state.engine, crossfader: clamp01(action.value) } }
    case 'optimistic-scene':
      return {
        ...state,
        engine: {
          ...state.engine,
          decks: {
            ...state.engine.decks,
            [action.deck]: { ...state.engine.decks[action.deck], sceneId: action.sceneId },
          },
        },
      }
    case 'optimistic-effect': {
      const deck = state.engine.decks[action.deck]
      return {
        ...state,
        engine: {
          ...state.engine,
          decks: {
            ...state.engine.decks,
            [action.deck]: {
              ...deck,
              effects: deck.effects.map((effect, index) =>
                index === action.index ? { ...effect, amount: clamp01(action.amount) } : effect,
              ),
            },
          },
        },
      }
    }
    case 'optimistic-master-effect': {
      if (!state.masterEffectsAvailable || !Number.isInteger(action.index) ||
        action.index < 0 || action.index >= 4) return state
      const masterEffects = normalizeMasterEffects(state.engine.masterEffects)
      masterEffects[action.index] = clamp01(action.amount)
      return { ...state, engine: { ...state.engine, masterEffects } }
    }
    case 'optimistic-safety':
      return { ...state, engine: { ...state.engine, [action.control]: action.enabled } }
    case 'clear-notice':
      return { ...state, notice: undefined }
  }
}
