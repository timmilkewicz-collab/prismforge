import { describe, expect, it } from 'vitest'
import { makeCommand } from './bridge'

describe('makeCommand', () => {
  it('creates a v1 Command with a request id', () => {
    const command = makeCommand({ action: 'setCrossfader', value: 0.5 })
    expect(command.version).toBe(1)
    expect(command.type).toBe('Command')
    expect(command.requestId).toBeTruthy()
    expect(command.payload).toEqual({ action: 'setCrossfader', value: 0.5 })
  })
})
