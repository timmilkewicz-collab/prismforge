import { describe, expect, it } from 'vitest'
import { initialViewState, viewReducer } from './state'
import type { EngineEnvelope, HostStatusMessage } from './types'

describe('viewReducer', () => {
  it('applies and clamps a StateSnapshot', () => {
    const envelope: EngineEnvelope = {
      version: 1,
      type: 'StateSnapshot',
      payload: {
        revision: 7,
        crossfader: 2,
        masterEffects: [-1, 0.25, 2, Number.NaN],
        audio: { rms: -1, peak: 3 },
        sceneCatalog: [{ id: 'ink-tide', name: 'Ink Tide', category: 'procedural' }],
      },
    }
    const state = viewReducer(initialViewState(), { type: 'engine-envelope', envelope })
    expect(state.engine.revision).toBe(7)
    expect(state.engine.crossfader).toBe(1)
    expect(state.engine.masterEffects).toEqual([0, 0.25, 1, 0])
    expect(state.masterEffectsAvailable).toBe(true)
    expect(state.engine.audio.rms).toBe(0)
    expect(state.engine.audio.peak).toBe(1)
    expect(state.engine.sceneCatalog[0].id).toBe('ink-tide')
  })

  it('merges a SignalFrame without erasing snapshot state', () => {
    const base = initialViewState()
    base.engine.audio.sourceId = 'system-default'
    const envelope: EngineEnvelope = {
      version: 1,
      type: 'SignalFrame',
      payload: { audio: { rms: 0.4 }, performance: { fps: 59.8 } },
    }
    const state = viewReducer(base, { type: 'engine-envelope', envelope })
    expect(state.engine.audio.sourceId).toBe('system-default')
    expect(state.engine.audio.rms).toBe(0.4)
    expect(state.engine.performance.fps).toBe(59.8)
  })

  it('clears live telemetry on disconnect and waits for fresh data after reconnect', () => {
    const hostStatus = (status: HostStatusMessage['payload']['status'], message: string): HostStatusMessage => ({
      kind: 'HostStatus', payload: { status, attempt: 1, message },
    })
    const online = viewReducer(initialViewState(), {
      type: 'host-status', message: hostStatus('connected', 'Engine connected'),
    })
    const live = viewReducer(online, { type: 'engine-envelope', envelope: {
      version: 1, type: 'StateSnapshot', payload: {
        crossfader: 0.7,
        masterEffects: [0.2, 0.3, 0.4, 0.5],
        audio: { sourceId: 'system-default', connected: true, receiving: true,
          rms: 0.4, peak: 0.8, low: 0.3, mid: 0.2, high: 0.1, clipping: true },
        performance: { fps: 59.8, frameTimeMs: 8.4 },
        output: { width: 1920, height: 1080, spout: { ready: true, connected: true } },
      },
    } })
    const lost = viewReducer(live, {
      type: 'host-status', message: hostStatus('disconnected', 'Engine disconnected'),
    })
    expect(lost.connection).toBe('disconnected')
    expect(lost.connectionMessage).toBe('Engine disconnected')
    expect(lost.masterEffectsAvailable).toBe(false)
    expect(lost.engine.crossfader).toBe(0.7)
    expect(lost.engine.audio.sourceId).toBe('system-default')
    expect(lost.engine.audio).toMatchObject({ connected: false, receiving: false,
      rms: 0, peak: 0, low: 0, mid: 0, high: 0, clipping: false })
    expect(lost.engine.performance).toMatchObject({ fps: 0, frameTimeMs: 0 })
    expect(lost.engine.output).toMatchObject({ width: 0, height: 0,
      spout: { ready: false, connected: false } })
    expect(live.engine.audio.peak).toBe(0.8)
    expect(viewReducer(lost, { type: 'engine-envelope', envelope: {
      version: 1, type: 'SignalFrame', payload: { audio: { peak: 1 }, performance: { fps: 144 } },
    } })).toBe(lost)

    const reconnecting = viewReducer(lost, {
      type: 'host-status', message: hostStatus('connecting', 'Looking for Engine'),
    })
    const reconnected = viewReducer(reconnecting, {
      type: 'host-status', message: hostStatus('connected', 'Engine connected'),
    })
    expect(reconnected.engine.audio.receiving).toBe(false)
    expect(reconnected.masterEffectsAvailable).toBe(false)
    expect(reconnected.engine.performance.fps).toBe(0)
    expect(reconnected.engine.output.spout.ready).toBe(false)

    const fresh = viewReducer(reconnected, { type: 'engine-envelope', envelope: {
      version: 1, type: 'StateSnapshot', payload: {
        audio: { connected: true, receiving: true, rms: 0.2, peak: 0.5 },
        performance: { fps: 60 },
        output: { width: 1920, height: 1080, spout: { ready: true } },
      },
    } })
    expect(fresh.engine.audio).toMatchObject({ connected: true, receiving: true, rms: 0.2, peak: 0.5 })
    expect(fresh.engine.performance.fps).toBe(60)
    expect(fresh.engine.output.spout.ready).toBe(true)
  })

  it('optimistically clamps and isolates a master macro change', () => {
    const initial = viewReducer(initialViewState(), {
      type: 'engine-envelope',
      envelope: { version: 1, type: 'StateSnapshot', payload: { masterEffects: [0, 0, 0, 0] } },
    })
    const updated = viewReducer(initial, { type: 'optimistic-master-effect', index: 2, amount: 1.8 })
    expect(updated.engine.masterEffects).toEqual([0, 0, 1, 0])
    expect(initial.engine.masterEffects).toEqual([0, 0, 0, 0])
    expect(viewReducer(updated, { type: 'optimistic-master-effect', index: 4, amount: 0.5 })).toBe(updated)
  })

  it('disables and clears macros when an older Engine omits them', () => {
    const supported = viewReducer(initialViewState(), {
      type: 'engine-envelope',
      envelope: { version: 1, type: 'StateSnapshot', payload: { masterEffects: [0, 0, 0, 0] } },
    })
    const withMacro = viewReducer(supported, {
      type: 'optimistic-master-effect', index: 1, amount: 0.6,
    })
    const envelope: EngineEnvelope = { version: 1, type: 'StateSnapshot', payload: { revision: 8 } }
    const legacy = viewReducer(withMacro, { type: 'engine-envelope', envelope })
    expect(legacy.engine.masterEffects).toEqual([0, 0, 0, 0])
    expect(legacy.masterEffectsAvailable).toBe(false)
    expect(viewReducer(legacy, { type: 'optimistic-master-effect', index: 1, amount: 0.6 })).toBe(legacy)
  })

  it('applies scene controls only to the currently selected scene', () => {
    const initial = viewReducer(initialViewState(), {
      type: 'engine-envelope',
      envelope: { version: 1, type: 'StateSnapshot', payload: {
        decks: { A: { sceneId: 'mirror-cathedral', sceneParams: [0.2, 0.3, 0.4, 0.5] } },
        sceneCatalog: [{ id: 'mirror-cathedral', name: 'Mirror Cathedral', parameters: [
          { id: 'symmetry', name: 'Symmetry', type: 'float', index: 0, min: 0, max: 1, default: 0.5 },
        ] }],
      } },
    })
    expect(initial.engine.decks.A.sceneParams).toEqual([0.2, 0.3, 0.4, 0.5])
    const changed = viewReducer(initial, { type: 'optimistic-scene-parameter',
      deck: 'A', sceneId: 'mirror-cathedral', index: 0, amount: 0.85 })
    expect(changed.engine.decks.A.sceneParams).toEqual([0.85, 0.3, 0.4, 0.5])
    expect(changed.engine.decks.B.sceneParams).toEqual([0.5, 0.5, 0.5, 0.5])
    expect(viewReducer(changed, { type: 'optimistic-scene-parameter',
      deck: 'A', sceneId: 'mirror-cathedral', index: 1, amount: 0.9 })).toBe(changed)
    const switched = viewReducer(changed, { type: 'optimistic-scene', deck: 'A', sceneId: 'ink-tide' })
    expect(viewReducer(switched, { type: 'optimistic-scene-parameter',
      deck: 'A', sceneId: 'mirror-cathedral', index: 0, amount: 0.1 })).toBe(switched)
  })

  it('uses neutral controls when an older Engine omits sceneParams', () => {
    const envelope: EngineEnvelope = { version: 1, type: 'StateSnapshot', payload: {
      decks: { A: { sceneId: 'ink-tide' } },
    } }
    const state = viewReducer(initialViewState(), { type: 'engine-envelope', envelope })
    expect(state.engine.decks.A.sceneParams).toEqual([0.5, 0.5, 0.5, 0.5])
  })
})
