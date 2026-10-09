import { useCallback, useEffect, useMemo, useReducer, useRef, useState } from 'react'
import { requestShutdownAll, sendCommand, subscribeToHost } from './bridge'
import { initialViewState, viewReducer } from './state'
import { emptyDeck } from './types'
import type {
  DeckId,
  DeckState,
  EngineEnvelope,
  HostStatusMessage,
  ModulationState,
  SceneDescriptor,
} from './types'

const clamp = (value: number, minimum = 0, maximum = 1) => Math.min(maximum, Math.max(minimum, value))
const percentage = (value: number) => `${Math.round(clamp(value) * 100)}%`
const masterMacros = [
  { name: 'Motion', hint: 'Drive the scene' },
  { name: 'Warp', hint: 'Bend the image' },
  { name: 'Trails', hint: 'Build momentum' },
  { name: 'Color', hint: 'Push the palette' },
] as const

function useThrottledCommand(intervalMs = 32) {
  const pending = useRef(new Map<string, { payload: Record<string, unknown>; timer: number }>())

  useEffect(
    () => () => {
      for (const entry of pending.current.values()) window.clearTimeout(entry.timer)
      pending.current.clear()
    },
    [],
  )

  const send = useCallback(
    (key: string, payload: Record<string, unknown>) => {
      const active = pending.current.get(key)
      if (active) {
        active.payload = payload
        return
      }

      const entry = {
        payload,
        timer: window.setTimeout(() => {
          const latest = pending.current.get(key)
          if (latest) sendCommand(latest.payload)
          pending.current.delete(key)
        }, intervalMs),
      }
      pending.current.set(key, entry)
    },
    [intervalMs],
  )

  const cancelPrefix = useCallback((prefix: string) => {
    for (const [key, entry] of pending.current) {
      if (!key.startsWith(prefix)) continue
      window.clearTimeout(entry.timer)
      pending.current.delete(key)
    }
  }, [])

  return { send, cancelPrefix }
}

interface DeckPanelProps {
  deckId: DeckId
  deck: DeckState
  scenes: SceneDescriptor[]
  online: boolean
  onScene: (deck: DeckId, sceneId: string) => void
  onSceneParameter: (deck: DeckId, sceneId: string, index: number, amount: number) => void
  onEffect: (deck: DeckId, index: number, amount: number) => void
}

function DeckPanel({ deckId, deck, scenes, online, onScene, onSceneParameter, onEffect }: DeckPanelProps) {
  const accent = deckId === 'A' ? 'cyan' : 'magenta'
  const activeScene = scenes.find((scene) => scene.id === deck.sceneId)
  const featuredScene = scenes.find((scene) => scene.id === 'mirror-cathedral')

  return (
    <section className={`panel deck-panel deck-${accent}`} aria-label={`Deck ${deckId}`}>
      <div className="panel-heading">
        <div>
          <span className="eyebrow">Live layer</span>
          <h2>Deck {deckId}</h2>
        </div>
        <div className={`deck-glyph ${accent}`}>{deckId}</div>
      </div>

      <div className="scene-now">
        <span className="scene-kicker">On air</span>
        <strong>{activeScene?.name ?? (online ? 'No scene selected' : 'Engine offline')}</strong>
        <small>{activeScene?.category ?? 'Awaiting scene catalog'}</small>
      </div>

      {featuredScene && deck.sceneId !== featuredScene.id && (
        <button className="featured-scene" disabled={!online}
          onClick={() => onScene(deckId, featuredScene.id)}>
          <span>FEATURED · PERFORMANCE SCENE</span>
          <b>{featuredScene.name}</b>
          <small>Play symmetry, depth, aperture and line width</small>
        </button>
      )}

      <div className="section-title">
        <span>Scene browser</span>
        <span className="count">{scenes.length}</span>
      </div>
      <div className="scene-grid" role="listbox" aria-label={`Deck ${deckId} scenes`}>
        {scenes.length === 0 ? (
          <div className="empty-state">Scene choices arrive from the engine.</div>
        ) : (
          scenes.map((scene, index) => (
            <button
              key={scene.id}
              className={`scene-card ${scene.id === deck.sceneId ? 'selected' : ''}`}
              disabled={!online}
              onClick={() => onScene(deckId, scene.id)}
              role="option"
              aria-selected={scene.id === deck.sceneId}
            >
              <span className="scene-index">{String(index + 1).padStart(2, '0')}</span>
              <span className="scene-name">{scene.name}</span>
              <span className="scene-category">{scene.category ?? 'Visual'}</span>
            </button>
          ))
        )}
      </div>

      {activeScene?.parameters && activeScene.parameters.length > 0 && (
        <>
          <div className="section-title scene-controls-title">
            <span>Shape this scene</span>
            <span className="scene-controls-live">LIVE</span>
          </div>
          <div className="scene-control-grid">
            {activeScene.parameters.map((parameter) => (
              <label className="parameter scene-control" key={parameter.id}>
                <span>
                  <b>{parameter.name}</b>
                  <output>{percentage(deck.sceneParams[parameter.index] ?? parameter.default)}</output>
                </span>
                <input
                  type="range"
                  min={parameter.min}
                  max={parameter.max}
                  step="0.005"
                  value={clamp(deck.sceneParams[parameter.index] ?? parameter.default)}
                  disabled={!online}
                  onChange={(event) => onSceneParameter(
                    deckId, activeScene.id, parameter.index, Number(event.currentTarget.value))}
                />
              </label>
            ))}
          </div>
        </>
      )}

      <div className="section-title effects-title">
        <span>Effect rack</span>
        <span className="count">{deck.effects.length}</span>
      </div>
      <div className="effect-rack">
        {deck.effects.length === 0 ? (
          <div className="empty-state compact">No effects in this deck.</div>
        ) : (
          deck.effects.map((effect, index) => (
            <label className="parameter" key={`${effect.id}-${index}`}>
              <span>
                <b>{effect.name}</b>
                <output>{percentage(effect.amount)}</output>
              </span>
              <input
                type="range"
                min="0"
                max="1"
                step="0.005"
                value={clamp(effect.amount)}
                disabled={!online}
                onChange={(event) => onEffect(deckId, index, Number(event.currentTarget.value))}
              />
            </label>
          ))
        )}
      </div>
    </section>
  )
}

function Meter({ label, value, peak = false, danger = false }: { label: string; value: number; peak?: boolean; danger?: boolean }) {
  const normalized = clamp(value)
  return (
    <div className={`meter ${peak ? 'peak' : ''} ${danger ? 'danger' : ''}`}>
      <div className="meter-label">
        <span>{label}</span>
        <span>{Math.round(normalized * 100)}</span>
      </div>
      <div className="meter-track" aria-label={`${label} ${Math.round(normalized * 100)} percent`}>
        <i style={{ transform: `scaleX(${normalized})` }} />
        <span className="meter-mark mark-50" />
        <span className="meter-mark mark-80" />
      </div>
    </div>
  )
}

interface MasterPanelProps {
  online: boolean
  masterEffectsAvailable: boolean
  crossfader: number
  masterEffects: number[]
  blackout: boolean
  panicDim: boolean
  onCrossfader: (value: number) => void
  onMasterEffect: (index: number, amount: number) => void
  onMasterReset: () => void
  onSafety: (control: 'blackout' | 'panicDim', enabled: boolean) => void
}

function MasterPanel({ online, masterEffectsAvailable, crossfader, masterEffects, blackout, panicDim, onCrossfader,
  onMasterEffect, onMasterReset, onSafety }: MasterPanelProps) {
  return (
    <section className="panel master-panel" aria-label="Master mixer">
      <div className="panel-heading centered">
        <div>
          <span className="eyebrow">Program bus</span>
          <h2>Master</h2>
        </div>
      </div>

      <div className="mix-orb" style={{ '--mix': crossfader } as React.CSSProperties}>
        <div className="mix-orb-core">
          <span>MIX</span>
          <strong>{Math.round(crossfader * 100)}</strong>
        </div>
      </div>

      <div className="crossfader-block">
        <div className="crossfader-labels">
          <span className="deck-a-label">A</span>
          <span>Crossfader</span>
          <span className="deck-b-label">B</span>
        </div>
        <input
          className="crossfader"
          type="range"
          min="0"
          max="1"
          step="0.002"
          value={clamp(crossfader)}
          disabled={!online}
          onChange={(event) => onCrossfader(Number(event.currentTarget.value))}
          aria-label="Deck crossfader"
        />
        <div className="crossfader-values">
          <span>{Math.round((1 - crossfader) * 100)}%</span>
          <span>{Math.round(crossfader * 100)}%</span>
        </div>
      </div>

      <div className="performance-macros">
        <div className="macro-heading">
          <span>Performance macros</span>
          <button type="button" disabled={!online || !masterEffectsAvailable || masterEffects.every((amount) => amount === 0)}
            onClick={onMasterReset} title="Return all performance macros to neutral">RESET</button>
        </div>
        {!masterEffectsAvailable && <small className="macro-unavailable">Requires matching Engine</small>}
        <div className="macro-list">
          {masterMacros.map((macro, index) => (
            <label className="macro-row" key={macro.name}>
              <span className="macro-label"><b>{macro.name}</b><small>{macro.hint}</small></span>
              <input type="range" min="0" max="1" step="0.005" value={clamp(masterEffects[index] ?? 0)}
                disabled={!online || !masterEffectsAvailable}
                onChange={(event) => onMasterEffect(index, Number(event.currentTarget.value))}
                aria-label={`${macro.name} performance macro`} />
              <output>{percentage(masterEffects[index] ?? 0)}</output>
            </label>
          ))}
        </div>
      </div>

      <div className="safety-stack">
        <button
          className={`safety-button panic ${panicDim ? 'active' : ''}`}
          disabled={!online}
          onClick={() => onSafety('panicDim', !panicDim)}
          aria-pressed={panicDim}
        >
          <span className="safety-icon">◐</span>
          <span><b>Panic dim</b><small>Reduce intensity</small></span>
        </button>
        <button
          className={`safety-button blackout ${blackout ? 'active' : ''}`}
          disabled={!online}
          onClick={() => onSafety('blackout', !blackout)}
          aria-pressed={blackout}
        >
          <span className="safety-icon">■</span>
          <span><b>{blackout ? 'Restore output' : 'Blackout'}</b><small>Immediate master cut</small></span>
        </button>
      </div>
    </section>
  )
}

function ModulationRoute({
  deck,
  route,
  online,
}: {
  deck: DeckId
  route: ModulationState
  online: boolean
}) {
  const sources = [
    { id: 'audio.kick', label: 'audio · kick' },
    { id: 'audio.accent', label: 'audio · accent' },
    { id: 'audio.low', label: 'audio · low' },
    { id: 'audio.mid', label: 'audio · mid' },
    { id: 'audio.high', label: 'audio · high' },
    { id: 'audio.rms', label: 'audio · rms' },
    { id: 'tempo.phase', label: 'tempo · phase' },
    { id: 'kinect.motion', label: 'kinect · motion (bridge unavailable)', disabled: true },
  ]
  const targets = ['master.crossfader', 'deck.effect.0', 'deck.effect.1', 'deck.effect.2', 'deck.effect.3']

  const update = (next: Partial<ModulationState>) => {
    const merged = { ...route, ...next }
    sendCommand({ action: 'setModulation', deck, ...merged })
  }

  return (
    <div className={`mod-route deck-${deck.toLowerCase()}`}>
      <button
        className={`route-toggle ${route.enabled ? 'enabled' : ''}`}
        disabled={!online}
        onClick={() => update({ enabled: !route.enabled })}
        aria-label={`${route.enabled ? 'Disable' : 'Enable'} modulation route ${route.slot + 1}`}
      >
        {route.slot + 1}
      </button>
      <select value={route.source} disabled={!online} onChange={(event) => update({ source: event.currentTarget.value })}>
        {sources.map((source) => <option key={source.id} value={source.id} disabled={source.disabled}>{source.label}</option>)}
      </select>
      <span className="route-arrow">→</span>
      <select value={route.target} disabled={!online} onChange={(event) => update({ target: event.currentTarget.value })}>
        {targets.map((target) => <option key={target} value={target}>{target.replace('.', ' · ')}</option>)}
      </select>
      <label className="mini-parameter">
        <span>Amount</span>
        <input type="range" min="-1" max="1" step="0.01" value={clamp(route.amount, -1, 1)} disabled={!online}
          onChange={(event) => update({ amount: Number(event.currentTarget.value) })} />
        <output>{route.amount.toFixed(2)}</output>
      </label>
      <label className="mini-parameter">
        <span>Smooth</span>
        <input type="range" min="0" max="1" step="0.01" value={clamp(route.smoothing)} disabled={!online}
          onChange={(event) => update({ smoothing: Number(event.currentTarget.value) })} />
        <output>{Math.round(route.smoothing * 100)}</output>
      </label>
    </div>
  )
}

export default function App() {
  const [view, dispatch] = useReducer(viewReducer, undefined, initialViewState)
  const [showName, setShowName] = useState('')
  const [showFeedback, setShowFeedback] = useState('')
  const { send: sendThrottled, cancelPrefix: cancelThrottledPrefix } = useThrottledCommand()
  const online = view.connection === 'connected'

  useEffect(() => subscribeToHost((message) => {
    if ('kind' in message && message.kind === 'HostStatus') {
      dispatch({ type: 'host-status', message: message as HostStatusMessage })
    } else {
      dispatch({ type: 'engine-envelope', envelope: message as EngineEnvelope })
    }
  }), [])

  useEffect(() => {
    if (!view.notice) return
    const timer = window.setTimeout(() => dispatch({ type: 'clear-notice' }), 5000)
    return () => window.clearTimeout(timer)
  }, [view.notice])

  const onScene = useCallback((deck: DeckId, sceneId: string) => {
    cancelThrottledPrefix(`scene-${deck}-`)
    dispatch({ type: 'optimistic-scene', deck, sceneId })
    sendCommand({ action: 'setScene', deck, sceneId })
  }, [cancelThrottledPrefix])

  const onEffect = useCallback((deck: DeckId, index: number, amount: number) => {
    dispatch({ type: 'optimistic-effect', deck, index, amount })
    sendThrottled(`effect-${deck}-${index}`, { action: 'setEffect', deck, effectIndex: index, amount })
  }, [sendThrottled])

  const onSceneParameter = useCallback((deck: DeckId, sceneId: string, index: number, amount: number) => {
    dispatch({ type: 'optimistic-scene-parameter', deck, sceneId, index, amount })
    sendThrottled(`scene-${deck}-${sceneId}-${index}`,
      { action: 'setSceneParameter', deck, sceneId, index, amount })
  }, [sendThrottled])

  const onCrossfader = useCallback((value: number) => {
    dispatch({ type: 'optimistic-crossfader', value })
    sendThrottled('crossfader', { action: 'setCrossfader', value })
  }, [sendThrottled])

  const onMasterEffect = useCallback((index: number, amount: number) => {
    if (!online || !view.masterEffectsAvailable) return
    dispatch({ type: 'optimistic-master-effect', index, amount })
    sendThrottled(`master-${index}`, { action: 'setMasterEffect', index, amount })
  }, [online, view.masterEffectsAvailable, sendThrottled])

  const onMasterReset = useCallback(() => {
    for (let index = 0; index < masterMacros.length; index += 1) onMasterEffect(index, 0)
  }, [onMasterEffect])

  const onSafety = useCallback((control: 'blackout' | 'panicDim', enabled: boolean) => {
    dispatch({ type: 'optimistic-safety', control, enabled })
    sendCommand({ action: control === 'blackout' ? 'setBlackout' : 'setPanicDim', enabled })
  }, [])

  const cueMap = useMemo(() => new Map(view.engine.cues.map((cue) => [cue.index, cue])), [view.engine.cues])
  const outputResolution = view.engine.output.width > 0
    ? `${view.engine.output.width}×${view.engine.output.height}`
    : 'No output'

  const deckAModulations = view.engine.decks.A.modulations.length
    ? view.engine.decks.A.modulations
    : emptyDeck().modulations
  const deckBModulations = view.engine.decks.B.modulations.length
    ? view.engine.decks.B.modulations
    : emptyDeck().modulations

  return (
    <div className={`app-shell ${view.engine.blackout ? 'is-blackout' : ''}`}>
      <header className="topbar">
        <div className="brand">
          <div className="brand-mark" aria-hidden="true"><i /><i /><i /></div>
          <div>
            <strong>PRISMFORGE</strong>
            <span>Live visual instrument</span>
          </div>
          <span className="alpha-badge">ALPHA · NOT SHOW CLEARED</span>
        </div>
        <div className="topbar-metrics">
          <div className={`connection-pill ${view.connection}`} title={view.connectionMessage}>
            <i />
            <span>{view.connection === 'connected' ? 'Engine online' : view.connectionMessage}</span>
          </div>
          <div className="metric"><span>FPS</span><b>{online ? view.engine.performance.fps.toFixed(1) : '—'}</b></div>
          <div className="metric"><span>Output</span><b>{online ? outputResolution : 'Unknown'}</b></div>
          <div className={`metric spout ${online && view.engine.output.spout.ready ? 'ready' : ''}`}>
            <span>Spout</span><b>{online ? (view.engine.output.spout.ready ? 'Ready' : 'Offline') : 'Unknown'}</b>
          </div>
          <button type="button" className="quit-all-button" onClick={() => requestShutdownAll()}
            title="Stop the engine and close Control">
            Quit all
          </button>
        </div>
      </header>

      {view.notice && <div className={`notice ${view.notice.kind}`}>{view.notice.message}</div>}

      <main>
        <div className="mixer-grid">
          <DeckPanel deckId="A" deck={view.engine.decks.A} scenes={view.engine.sceneCatalog} online={online}
            onScene={onScene} onSceneParameter={onSceneParameter} onEffect={onEffect} />
          <MasterPanel online={online} masterEffectsAvailable={view.masterEffectsAvailable}
            crossfader={view.engine.crossfader} masterEffects={view.engine.masterEffects}
            blackout={view.engine.blackout} panicDim={view.engine.panicDim} onCrossfader={onCrossfader}
            onMasterEffect={onMasterEffect} onMasterReset={onMasterReset} onSafety={onSafety} />
          <DeckPanel deckId="B" deck={view.engine.decks.B} scenes={view.engine.sceneCatalog} online={online}
            onScene={onScene} onSceneParameter={onSceneParameter} onEffect={onEffect} />
        </div>

        <div className="lower-grid">
          <section className="panel audio-panel">
            <div className="panel-heading compact-heading">
              <div><span className="eyebrow">Signal bus</span><h2>Audio input</h2></div>
              <span className={`clip-light ${online && view.engine.audio.clipping ? 'active' : ''}`}>CLIP</span>
            </div>
            <label className="source-select">
              <span>Source</span>
              <select
                value={view.engine.audio.sourceId}
                disabled={!online || view.engine.audio.sources.length === 0}
                onChange={(event) => sendCommand({ action: 'setAudioSource', id: event.currentTarget.value })}
              >
                {view.engine.audio.sources.length === 0 && <option value="">Waiting for devices</option>}
                {view.engine.audio.sources.map((source) => (
                  <option key={source.id} value={source.id}>{source.name}{source.connected === false ? ' (offline)' : ''}</option>
                ))}
              </select>
            </label>
            <div className={`audio-readiness ${online && view.engine.audio.connected && view.engine.audio.receiving ?
              (view.engine.audio.peak > 0.003 ? 'active' : 'silent') : 'offline'}`}>
              {!online
                ? 'Engine link unavailable · audio status unknown'
                : !view.engine.audio.connected
                ? 'Capture offline · visuals are not reacting to audio'
                : !view.engine.audio.receiving
                  ? 'Capture open · waiting for audio blocks'
                  : view.engine.audio.peak > 0.003
                    ? 'Signal detected'
                    : 'Capture running · no level detected'}
            </div>
            <div className="meters">
              <Meter label="RMS" value={online ? view.engine.audio.rms : 0} />
              <Meter label="PEAK" value={online ? view.engine.audio.peak : 0} peak danger={online && view.engine.audio.clipping} />
              <Meter label="LOW" value={online ? view.engine.audio.low : 0} />
              <Meter label="MID" value={online ? view.engine.audio.mid : 0} />
              <Meter label="HIGH" value={online ? view.engine.audio.high : 0} />
            </div>
          </section>

          <section className="panel modulation-panel">
            <div className="panel-heading compact-heading">
              <div><span className="eyebrow">Reactive routing</span><h2>Modulation matrix</h2></div>
              <span className="quality-state">AQ {view.engine.performance.adaptiveQuality ? 'ON' : 'OFF'}</span>
            </div>
            <div className="mod-list">
              {deckAModulations.map((route) => <ModulationRoute key={`A-${route.slot}`} deck="A" route={route} online={online} />)}
              {deckBModulations.map((route) => <ModulationRoute key={`B-${route.slot}`} deck="B" route={route} online={online} />)}
            </div>
          </section>

          <section className="panel cues-panel">
            <div className="panel-heading compact-heading">
              <div><span className="eyebrow">Performance memory</span><h2>Cue bank</h2></div>
              <span className="revision">REV {view.engine.revision}</span>
            </div>
            <div className="show-library">
              <label>
                <span>Show name</span>
                <input
                  type="text"
                  value={showName}
                  maxLength={64}
                  disabled={!online}
                  placeholder="Festival Set 01"
                  onChange={(event) => setShowName(event.currentTarget.value.replace(/[^A-Za-z0-9 _-]/g, ''))}
                />
              </label>
              <button disabled={!online || !showName.trim()} onClick={() => {
                sendCommand({ action: 'saveShow', name: showName.trim() })
                setShowFeedback('Save queued')
              }}>Save show</button>
              <button disabled={!online || !showName.trim()} onClick={() => {
                sendCommand({ action: 'loadShow', name: showName.trim() })
                setShowFeedback('Load requested')
              }}>Load show</button>
              {showFeedback && <small className="show-feedback">{showFeedback}</small>}
            </div>
            <div className="cue-grid">
              {Array.from({ length: 8 }, (_, index) => {
                const cue = cueMap.get(index)
                return (
                  <div className={`cue ${cue?.saved ? 'saved' : ''}`} key={index}>
                    <button disabled={!online || !cue?.saved} onClick={() => sendCommand({ action: 'recallCue', index })}>
                      <span>{String(index + 1).padStart(2, '0')}</span>
                      <b>{cue?.label ?? (cue?.saved ? 'Saved' : 'Empty')}</b>
                    </button>
                    <button className="cue-save" disabled={!online} title={`Save current look to cue ${index + 1}`}
                      onClick={() => sendCommand({ action: 'saveCue', index })}>SAVE</button>
                  </div>
                )
              })}
            </div>
          </section>
        </div>
      </main>

      <footer>
        <span>LOCAL CONTROL</span>
        <span>{view.engine.output.spout.senderName || 'PrismForge'}</span>
        <span>{online ? `${view.engine.performance.frameTimeMs.toFixed(1)} ms/frame` : 'Frame time unknown'}</span>
      </footer>

      {view.engine.blackout && (
        <div className="blackout-banner" role="status">
          <span>■</span><b>OUTPUT BLACKED OUT</b><small>Use Restore output to resume the program feed</small>
        </div>
      )}
    </div>
  )
}
