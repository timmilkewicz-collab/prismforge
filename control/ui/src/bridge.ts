import type { EngineEnvelope, HostStatusMessage } from './types'

declare global {
  interface Window {
    chrome?: {
      webview?: {
        postMessage(message: unknown): void
        addEventListener(type: 'message', listener: (event: MessageEvent) => void): void
        removeEventListener(type: 'message', listener: (event: MessageEvent) => void): void
      }
    }
  }
}
export type HostMessage = EngineEnvelope | HostStatusMessage

const makeRequestId = () =>
  globalThis.crypto?.randomUUID?.() ?? `${Date.now()}-${Math.random().toString(16).slice(2)}`

export function makeCommand(payload: Record<string, unknown>): EngineEnvelope {
  return {
    version: 1,
    type: 'Command',
    requestId: makeRequestId(),
    payload,
  }
}

export function sendCommand(payload: Record<string, unknown>): void {
  const envelope = makeCommand(payload)
  window.chrome?.webview?.postMessage({ kind: 'EngineEnvelope', envelope })
}

export function requestShutdownAll(): void {
  window.chrome?.webview?.postMessage({ kind: 'HostRequest', action: 'shutdownAll' })
}

export function subscribeToHost(listener: (message: HostMessage) => void): () => void {
  const webview = window.chrome?.webview
  if (!webview) return () => undefined

  const handler = (event: MessageEvent) => {
    const data = typeof event.data === 'string' ? JSON.parse(event.data) : event.data
    if (data?.kind === 'HostStatus' || (data?.version === 1 && typeof data?.type === 'string')) {
      listener(data as HostMessage)
    }
  }
  webview.addEventListener('message', handler)
  return () => webview.removeEventListener('message', handler)
}
