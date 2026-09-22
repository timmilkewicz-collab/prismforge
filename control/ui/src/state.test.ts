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
        audio: { rms: -1, peak: 3 },
        sceneCatalog: [{ id: 'ink-tide', name: 'Ink Tide', category: 'procedural' }],
      },
    }
    const state = viewReducer(initialViewState(), { type: 'engine-envelope', envelope })
    expect(state.engine.revision).toBe(7)
    expect(state.engine.crossfader).toBe(1)
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

  it('tracks the host connection independently of engine state', () => {
    const message: HostStatusMessage = {
      kind: 'HostStatus',
      payload: { status: 'disconnected', attempt: 2, message: 'Engine offline' },
    }
    const state = viewReducer(initialViewState(), { type: 'host-status', message })
    expect(state.connection).toBe('disconnected')
    expect(state.connectionMessage).toBe('Engine offline')
  })
})
